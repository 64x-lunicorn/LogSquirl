/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

// A QuickFind without a match over a large Log File (#287): every Log Line
// it searches is read and matched, forwards and backwards, over every Log
// Line as the main view searches and over every tenth one as a Filtered View
// does, with the Log File kept open and kept closed.
//
// Uses only what QuickFind and LogData offered before #287, so the same file
// measures both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "generated_log_file.h"
#include "test_policies.h"

#include "linetypes.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "persistentinfo.h"
#include "quickfind.h"
#include "quickfindpattern.h"
#include "selection.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "isolated_settings.h"

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

using namespace logdatabenchmark;

// Attaches the Log File and returns once its indexing has finished.
LoadingStatus indexLogFile( LogData& logData, const QString& fileName )
{
    auto status = LoadingStatus::Interrupted;
    QEventLoop loop;
    QObject::connect( &logData, &LogData::loadingFinished, &loop,
                      [ &loop, &status ]( LoadingStatus finished ) {
                          status = finished;
                          loop.quit();
                      } );
    logData.attachFile( fileName );
    loop.exec();
    return status;
}

// The generated Log File of short Log Lines, written on first use into a
// temporary directory that is removed at exit.
class GeneratedLogFile {
public:
    const QString& fileName()
    {
        if ( fileName_.isEmpty() ) {
            REQUIRE( directory_.isValid() );
            const auto bytes = generatedLogFileBytes();
            std::cerr << "Generating a Log File of " << ( bytes / ( 1024 * 1024 ) ) << " MiB of "
                      << shapeName( LogFileShape::ShortLines ) << " in "
                      << directory_.path().toStdString() << std::endl;
            fileName_ = directory_.filePath( "short_lines.log" );
            bool written = false;
            lineCount_
                = writeGeneratedLogFile( fileName_, LogFileShape::ShortLines, bytes, written );
            REQUIRE( written );
        }
        return fileName_;
    }

    std::uint64_t lineCount()
    {
        fileName();
        return lineCount_;
    }

private:
    QTemporaryDir directory_{ QDir::tempPath() + "/logsquirl_quickfind_benchmark_XXXXXX" };
    QString fileName_;
    std::uint64_t lineCount_ = 0;
};

// Owned by main(), so that every LogData is gone before the QCoreApplication.
GeneratedLogFile* generatedLogFile = nullptr;

// The Log File indexed, kept open or kept closed between reads.
std::unique_ptr<LogData> indexedLogData( bool keepFileClosed )
{
    auto policies = testSettingsPolicies();
    policies.fileAccess.keepFileClosed = keepFileClosed;
    auto logData = std::make_unique<LogData>( policies.indexing, policies.search,
                                              policies.fileAccess, policies.decoding );
    REQUIRE( indexLogFile( *logData, generatedLogFile->fileName() ) == LoadingStatus::Successful );
    REQUIRE( logData->getNbLine().get() == generatedLogFile->lineCount() );
    return logData;
}

// Runs one QuickFind for text that no Log Line holds, from the first Log Line
// forwards or from the last one backwards, and returns once it is done.
bool quickFindWithoutMatch( QuickFind& quickFind, LineNumber from, bool forward )
{
    QuickFindPattern pattern;
    pattern.changeSearchPattern( QStringLiteral( "no Log Line holds this text" ),
                                 /* useExtendedRegexp */ true );
    Selection selection;
    selection.selectLine( from );

    bool hasMatch = true;
    QEventLoop loop;
    const auto connection = QObject::connect( &quickFind, &QuickFind::searchDone, &loop,
                                              [ &loop, &hasMatch ]( bool matched, Portion ) {
                                                  hasMatch = matched;
                                                  loop.quit();
                                              } );
    // A QuickFind that found nothing remembers it, and the next one in the
    // same direction would not search at all.
    quickFind.resetLimits();
    if ( forward ) {
        quickFind.searchForward( selection, pattern.getMatcher() );
    }
    else {
        quickFind.searchBackward( selection, pattern.getMatcher() );
    }
    loop.exec();
    QObject::disconnect( connection );
    return hasMatch;
}

void benchmarkQuickFind( const std::string& name, const std::function<QuickFindLines()>& lines )
{
    QuickFind quickFind( lines, []( LineNumber ) { return true; } );
    const auto last = LineNumber( generatedLogFile->lineCount() - 1 );
    REQUIRE( lines().count().get() > 0 );

    // Each run checks that it found nothing: a match would end it early.
    BENCHMARK( name + ", forwards" )
    {
        const auto hasMatch = quickFindWithoutMatch( quickFind, 0_lnum, true );
        REQUIRE_FALSE( hasMatch );
        return hasMatch;
    };

    BENCHMARK( name + ", backwards" )
    {
        const auto hasMatch = quickFindWithoutMatch( quickFind, last, false );
        REQUIRE_FALSE( hasMatch );
        return hasMatch;
    };
}

// Every tenth Log Line, as a Filtered View displays the Matches of a Search.
SearchResultArray everyTenthLogLine( const LogData& logData )
{
    SearchResultArray lines;
    for ( std::uint64_t line = 0; line < logData.getNbLine().get(); line += 10 ) {
        lines.add( line );
    }
    return lines;
}

void benchmarkLogFile( bool keepFileClosed )
{
    // Written and indexed before the first case, so no report is interrupted.
    const auto logData = indexedLogData( keepFileClosed );
    const std::string file = keepFileClosed ? "file kept closed" : "file kept open";

    benchmarkQuickFind( file + ", every Log Line",
                        [ & ]() { return QuickFindLines::everyLogLine( *logData ); } );

    const auto tenth = everyTenthLogLine( *logData );
    benchmarkQuickFind( file + ", every tenth Log Line",
                        [ & ]() { return QuickFindLines::someLogLines( *logData, tenth ); } );
}

} // namespace

TEST_CASE( "QuickFind without a match, the Log File kept open",
           "[quickfind-benchmark][file-kept-open]" )
{
    benchmarkLogFile( false );
}

// Before #287 QuickFind reopened the Log File for every Log Line it read:
// over a Log File of 1 GB a single run takes minutes there. Hidden, so a run
// of every benchmark leaves it out; ask for [file-kept-closed] to run it.
TEST_CASE( "QuickFind without a match, the Log File kept closed",
           "[.][quickfind-benchmark][file-kept-closed]" )
{
    benchmarkLogFile( true );
}

int main( int argc, char* argv[] )
{
    // The test cases run beside a settings file of this process's own, so
    // that no test binary reads or writes the one in the build directory and
    // no case inherits what an earlier one left behind (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

    QCoreApplication app( argc, argv );

    GeneratedLogFile file;
    generatedLogFile = &file;
    const auto result = Catch::Session().run( argc, argv );
    generatedLogFile = nullptr;
    return result;
}
