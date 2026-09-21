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

// Benchmarks for the readers of a Search's Displayed Lines (#288): the
// Filtered View reading the rows it paints, a save of displayed lines, the
// command line tool printing its matches, and removing the longest of many
// Marks. They run on a generated Log File of short Log Lines, of which a
// Search matches every eighth.
//
// Uses only what LogData, LogFilteredData, FilteredViewLines and the lines
// saver offered before #288, so the same file measures both sides of an A/B
// comparison (see README.md).

#include "generated_log_file.h"
#include "test_policies.h"

#include "linemapping.h"
#include "linessaver.h"
#include "linetypes.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "persistentinfo.h"
#include "regularexpressionpattern.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include "isolated_settings.h"

// The settings store the linked user interface library reads, as for the
// other benchmarks linking it.
const bool PersistentInfo::ForcePortable = true;

namespace {

using namespace logdatabenchmark;

// The pattern a Search matches every eighth generated Log Line with.
const QString EveryEighthLine = QStringLiteral( "\\[worker-3\\]" );

// How many rows the Filtered View paints at once, and at how many Scroll
// Positions across the matches it is painted.
constexpr std::uint64_t RowsOnScreen = 60;
constexpr std::uint64_t ScrollPositions = 200;
// How many displayed lines a save writes, at most: fewer in a Log File
// written smaller for a quick run.
constexpr std::uint64_t SavedLines = 100'000;
// How many Marks there are when the longest one is removed.
constexpr std::uint64_t MarkCount = 10'000;

// The size of the generated Log File: 256 MiB, or the number of MiB in
// LOGSQUIRL_BENCHMARK_LOG_FILE_MB, for a quick run.
std::uint64_t logFileBytes()
{
    constexpr std::uint64_t Mib = 1024 * 1024;
    return qEnvironmentVariableIsSet( "LOGSQUIRL_BENCHMARK_LOG_FILE_MB" ) ? generatedLogFileBytes()
                                                                          : 256 * Mib;
}

// A generated Log File, indexed, with a Search for every eighth Log Line
// completed on it; written on first use and removed at exit.
class SearchedLogFile {
public:
    SearchedLogFile()
    {
        REQUIRE( directory_.isValid() );
        fileName_ = directory_.filePath( "short_lines.log" );
        std::cerr << "Generating a Log File of " << ( logFileBytes() / ( 1024 * 1024 ) )
                  << " MiB in " << directory_.path().toStdString() << std::endl;
        bool written = false;
        writeGeneratedLogFile( fileName_, LogFileShape::ShortLines, logFileBytes(), written );
        REQUIRE( written );

        // No Context Lines: rebuilding them on every Mark would hide what
        // remembering the length of a Mark saves.
        auto policies = testSettingsPolicies();
        policies.search.contextLinesCount = 0;
        logData_ = std::make_unique<LogData>( policies.indexing, policies.search,
                                              policies.fileAccess, policies.decoding );
        {
            QEventLoop loop;
            QObject::connect( logData_.get(), &LogData::loadingFinished, &loop,
                              [ &loop ]( LoadingStatus ) { loop.quit(); } );
            logData_->attachFile( fileName_ );
            loop.exec();
        }
        REQUIRE( logData_->getNbLine().get() > 0 );

        search_ = logData_->getNewFilteredData();
        {
            QEventLoop loop;
            QObject::connect( search_.get(), &LogFilteredData::searchStateChanged, &loop,
                              [ &loop ]( const SearchSession::State& state ) {
                                  if ( state.phase != SearchSession::Phase::Running ) {
                                      loop.quit();
                                  }
                              } );
            search_->request( RegularExpressionPattern( EveryEighthLine ) );
            loop.exec();
        }
        REQUIRE( search_->getNbLine().get() > RowsOnScreen + ScrollPositions );
        std::cerr << search_->getNbLine().get() << " displayed lines of "
                  << logData_->getNbLine().get() << " Log Lines" << std::endl;
    }

    ~SearchedLogFile()
    {
        search_.reset();
        logData_.reset();
    }

    const QString& fileName() const
    {
        return fileName_;
    }
    LogData& logData() const
    {
        return *logData_;
    }
    LogFilteredData& search() const
    {
        return *search_;
    }

private:
    QTemporaryDir directory_{ QDir::tempPath() + "/logsquirl_filteredview_benchmark_XXXXXX" };
    QString fileName_;
    std::unique_ptr<LogData> logData_;
    std::unique_ptr<LogFilteredData> search_;
};

// Owned by main(), so that every LogData is gone before the QCoreApplication.
std::unique_ptr<SearchedLogFile>* searchedLogFileHolder = nullptr;

// The searched Log File, written, indexed and searched in the first case
// that uses it.
SearchedLogFile& searchedLogFile()
{
    if ( !*searchedLogFileHolder ) {
        *searchedLogFileHolder = std::make_unique<SearchedLogFile>();
    }
    return **searchedLogFileHolder;
}

} // namespace

TEST_CASE( "The Filtered View reading the rows it paints", "[filteredview-benchmark][paint]" )
{
    const auto& search = searchedLogFile().search();
    const auto displayed = search.getNbLine().get();
    const auto stride = ( displayed - RowsOnScreen ) / ScrollPositions;

    BENCHMARK( "getLines, a screen at each of 200 Scroll Positions" )
    {
        qsizetype characters = 0;
        for ( std::uint64_t scroll = 0; scroll < ScrollPositions; ++scroll ) {
            for ( const auto& text :
                  search.getLines( LineNumber( scroll * stride ), LinesCount( RowsOnScreen ) ) ) {
                characters += text.size();
            }
        }
        return characters;
    };
}

TEST_CASE( "Saving displayed lines", "[filteredview-benchmark][save]" )
{
    const FilteredViewLines lines{ &searchedLogFile().search() };
    const auto savedLines
        = LineNumber( std::min( SavedLines, searchedLogFile().search().getNbLine().get() ) );

    BENCHMARK( "the first 100,000 displayed lines, as UTF-8" )
    {
        QBuffer output;
        output.open( QIODevice::WriteOnly );
        AtomicFlag interrupt;
        const bool saved = saveDisplayedLines( lines.linesToSave(), 0_lnum, savedLines, nullptr,
                                               output, interrupt, []( int ) {} );
        REQUIRE( saved );
        return output.size();
    };
}

TEST_CASE( "Removing the longest of many Marks", "[filteredview-benchmark][marks]" )
{
    // Marks on a Log File without a Search, evenly spread over it.
    const auto& logData = searchedLogFile().logData();
    const auto marks = logData.getNewFilteredData();
    const auto stride = logData.getNbLine().get() / MarkCount;
    for ( std::uint64_t mark = 0; mark < MarkCount; ++mark ) {
        marks->addMark( LineNumber( mark * stride ) );
    }

    auto longest = 0_lnum;
    for ( const auto mark : marks->getMarks() ) {
        if ( logData.getLineLength( mark ) > logData.getLineLength( longest ) ) {
            longest = mark;
        }
    }
    REQUIRE( marks->getMaxLength() == logData.getLineLength( longest ) );

    BENCHMARK( "remove the longest Mark and add it back" )
    {
        marks->toggleMark( longest );
        const auto withoutIt = marks->getMaxLength();
        marks->toggleMark( longest );
        return withoutIt;
    };
}

TEST_CASE( "The command line tool printing its matches", "[filteredview-benchmark][grep]" )
{
    // logsquirl_grep sits next to this benchmark, in a build and in the
    // Benchmarks workflow alike.
    const auto grep = QDir( QCoreApplication::applicationDirPath() )
                          .filePath( QStringLiteral( "logsquirl_grep" )
#if defined( Q_OS_WIN )
                                     + QStringLiteral( ".exe" )
#endif
                          );
    if ( !QFileInfo::exists( grep ) ) {
        WARN( "logsquirl_grep is not next to the benchmark: not measured" );
        return;
    }

    BENCHMARK( "logsquirl_grep, every eighth Log Line" )
    {
        QProcess process;
        process.setStandardOutputFile( QProcess::nullDevice() );
        process.setStandardErrorFile( QProcess::nullDevice() );
        process.start( grep,
                       { searchedLogFile().fileName(), QStringLiteral( "-e" ), EveryEighthLine } );
        REQUIRE( process.waitForFinished( 600'000 ) );
        REQUIRE( process.exitCode() == 0 );
        return process.exitCode();
    };
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

    std::unique_ptr<SearchedLogFile> logFile;
    searchedLogFileHolder = &logFile;
    const auto result = Catch::Session().run( argc, argv );
    logFile.reset();
    searchedLogFileHolder = nullptr;
    return result;
}
