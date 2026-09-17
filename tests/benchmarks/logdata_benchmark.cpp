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

// Micro-benchmarks for indexing a Log File and reading its Log Lines (#275),
// on generated Log Files of about 1 GB: one of short Log Lines, one of tabs
// and long Log Lines. Links logsquirl_logdata only and creates a
// QCoreApplication, whose event loop hears that indexing finished.
//
// Every case runs on both Log Files. To measure a new way of doing the same
// thing, add a BENCHMARK next to the one it replaces, over the same Log Lines
// (contiguousRange(), sparseLogLines()), so both show up in one run.
//
// See tests/benchmarks/README.md for how to run this and compare two runs.

#include "generated_log_file.h"
#include "test_policies.h"

#include "atomicflag.h"
#include "displayedlines.h"
#include "linetypes.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logdataworker.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

namespace {

using namespace logdatabenchmark;

constexpr std::array Shapes{ LogFileShape::ShortLines, LogFileShape::TabsAndLongLines };

// How many Log Lines the read cases read, and how far apart the sparse ones
// are: every hundredth, as a Search result or a Quick Find walks them.
constexpr std::uint64_t ReadLineCount = 10'000;
constexpr std::uint64_t SparseStride = 100;

struct GeneratedLogFile {
    QString fileName;
    std::uint64_t lineCount = 0;
};

std::unique_ptr<LogData> newLogData()
{
    auto policies = testSettingsPolicies();
    // Every indexing run indexes the Log File, instead of taking the Index
    // an earlier one left in the Index Cache.
    policies.indexing.useIndexCache = false;
    return std::make_unique<LogData>( policies.indexing, policies.search, policies.fileAccess,
                                      policies.decoding );
}

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

// The generated Log Files, written on first use into a temporary directory
// that is removed at exit, and each one indexed once for the read cases.
class GeneratedLogFiles {
public:
    GeneratedLogFile& logFile( LogFileShape shape )
    {
        auto generated = files_.find( shape );
        if ( generated == files_.end() ) {
            REQUIRE( directory_.isValid() );
            const auto bytes = generatedLogFileBytes();
            std::cerr << "Generating a Log File of " << ( bytes / ( 1024 * 1024 ) ) << " MiB of "
                      << shapeName( shape ) << " in " << directory_.path().toStdString()
                      << std::endl;

            GeneratedLogFile file;
            file.fileName = directory_.filePath(
                shape == LogFileShape::ShortLines ? "short_lines.log" : "tabs_and_long_lines.log" );
            bool written = false;
            file.lineCount = writeGeneratedLogFile( file.fileName, shape, bytes, written );
            REQUIRE( written );
            generated = files_.emplace( shape, file ).first;
        }
        return generated->second;
    }

    const LogData& indexedLogData( LogFileShape shape )
    {
        auto indexed = indexed_.find( shape );
        if ( indexed == indexed_.end() ) {
            const auto& file = logFile( shape );
            auto logData = newLogData();
            REQUIRE( indexLogFile( *logData, file.fileName ) == LoadingStatus::Successful );
            REQUIRE( logData->getNbLine().get() == file.lineCount );
            indexed = indexed_.emplace( shape, std::move( logData ) ).first;
        }
        return *indexed->second;
    }

private:
    QTemporaryDir directory_{ QDir::tempPath() + "/logsquirl_logdata_benchmark_XXXXXX" };
    std::map<LogFileShape, GeneratedLogFile> files_;
    std::map<LogFileShape, std::unique_ptr<LogData>> indexed_;
};

// Owned by main(), so that every LogData is gone before the QCoreApplication.
GeneratedLogFiles* generatedLogFiles = nullptr;

struct LineRange {
    LineNumber first;
    LinesCount count;
};

// ReadLineCount Log Lines in a row from the middle of the Log File.
LineRange contiguousRange( const LogData& logData )
{
    const auto lines = logData.getNbLine().get();
    const auto count = std::min( ReadLineCount, lines );
    return { LineNumber( ( lines - count ) / 2 ), LinesCount( count ) };
}

// Every SparseStride-th Log Line from the start, ReadLineCount of them or as
// many as the Log File holds.
std::vector<LineNumber> sparseLogLines( const LogData& logData )
{
    std::vector<LineNumber> lines;
    for ( std::uint64_t line = 0; line < logData.getNbLine().get() && lines.size() < ReadLineCount;
          line += SparseStride ) {
        lines.emplace_back( line );
    }
    return lines;
}

std::string caseName( LogFileShape shape, const char* how )
{
    return std::string{ shapeName( shape ) } + ": " + how;
}

} // namespace

TEST_CASE( "Indexing a Log File", "[logdata-benchmark][indexing]" )
{
    // Written before the first case, so that no report is interrupted.
    for ( const auto shape : Shapes ) {
        generatedLogFiles->logFile( shape );
    }

    for ( const auto shape : Shapes ) {
        const auto& file = generatedLogFiles->logFile( shape );

        // Only attaching and indexing is measured: building the LogData
        // beforehand and destroying it with its Index afterwards is not.
        BENCHMARK_ADVANCED( caseName( shape, "whole Log File" ) )(
            Catch::Benchmark::Chronometer meter )
        {
            std::vector<std::unique_ptr<LogData>> runs;
            std::generate_n( std::back_inserter( runs ), meter.runs(), newLogData );
            std::vector<LoadingStatus> statuses( runs.size(), LoadingStatus::Interrupted );

            meter.measure( [ & ]( int run ) {
                statuses[ static_cast<std::size_t>( run ) ]
                    = indexLogFile( *runs[ static_cast<std::size_t>( run ) ], file.fileName );
            } );

            for ( std::size_t run = 0; run < runs.size(); ++run ) {
                REQUIRE( statuses[ run ] == LoadingStatus::Successful );
                REQUIRE( runs[ run ]->getNbLine().get() == file.lineCount );
            }
        };
    }
}

TEST_CASE( "Reading a contiguous range of Log Lines", "[logdata-benchmark][contiguous-read]" )
{
    for ( const auto shape : Shapes ) {
        generatedLogFiles->indexedLogData( shape );
    }

    for ( const auto shape : Shapes ) {
        const auto& logData = generatedLogFiles->indexedLogData( shape );
        const auto range = contiguousRange( logData );

        BENCHMARK( caseName( shape, "getLines, one call" ) )
        {
            return logData.getLines( range.first, range.count ).size();
        };

        BENCHMARK( caseName( shape, "getLineString, line by line" ) )
        {
            qsizetype characters = 0;
            for ( auto line = range.first; line < range.first + range.count; ++line ) {
                characters += logData.getLineString( line ).size();
            }
            return characters;
        };
    }
}

TEST_CASE( "Reading a sparse set of Log Lines", "[logdata-benchmark][sparse-read]" )
{
    for ( const auto shape : Shapes ) {
        generatedLogFiles->indexedLogData( shape );
    }

    for ( const auto shape : Shapes ) {
        const auto& logData = generatedLogFiles->indexedLogData( shape );
        const auto lines = sparseLogLines( logData );

        // As the Filtered View and saving a Search result read today.
        BENCHMARK( caseName( shape, "getLineString, line by line" ) )
        {
            qsizetype characters = 0;
            for ( const auto line : lines ) {
                characters += logData.getLineString( line ).size();
            }
            return characters;
        };

        // The same Log Lines in one sparse read (#286).
        BENCHMARK( caseName( shape, "getLinesSparse" ) )
        {
            qsizetype characters = 0;
            for ( const auto& text : logData.getLinesSparse( lines ) ) {
                characters += text.size();
            }
            return characters;
        };

        // The same Log Lines as UTF-8, as the command line tool prints them
        // (#288).
        BENCHMARK( caseName( shape, "getUtf8LinesSparse" ) )
        {
            return logData.getUtf8LinesSparse( lines ).size();
        };

        // As Quick Find read before #287.
        BENCHMARK( caseName( shape, "getExpandedLineString, line by line" ) )
        {
            qsizetype characters = 0;
            for ( const auto line : lines ) {
                characters += logData.getExpandedLineString( line ).size();
            }
            return characters;
        };

        // The same Log Lines in one sparse read, tabs expanded (#286).
        BENCHMARK( caseName( shape, "getExpandedLinesSparse" ) )
        {
            qsizetype characters = 0;
            for ( const auto& text : logData.getExpandedLinesSparse( lines ) ) {
                characters += text.size();
            }
            return characters;
        };
    }
}

TEST_CASE( "Walking the Displayed Lines from a position", "[logdata-benchmark][displayed-lines]" )
{
    // Every third Log Line of 30 million displayed, as a Search with many
    // Matches leaves them, walked for ReadLineCount positions from the middle.
    SearchResultArray lines;
    for ( std::uint64_t line = 0; line < 30'000'000; line += 3 ) {
        lines.add( line );
    }
    const auto first = LineNumber( lines.cardinality() / 2 );

    // As the Filtered View, saving and Quick Find look positions up today.
    BENCHMARK( "lineAtPosition, position by position" )
    {
        std::uint64_t sum = 0;
        for ( std::uint64_t position = 0; position < ReadLineCount; ++position ) {
            sum += lineAtPosition( lines, first + LinesCount( position ) ).value_or( 0_lnum ).get();
        }
        return sum;
    };

    // One select(), then stepping from Log Line to Log Line (#286).
    BENCHMARK( "DisplayedLinesCursor, takeForward" )
    {
        std::uint64_t sum = 0;
        DisplayedLinesCursor cursor( lines, first );
        for ( const auto line : cursor.takeForward( LinesCount( ReadLineCount ) ) ) {
            sum += line.get();
        }
        return sum;
    };
}

namespace {

// Appends `count` more generated Log Lines to the Log File.
void appendLogLines( LogFileShape shape, GeneratedLogFile& file, std::uint64_t count )
{
    std::string lines;
    for ( std::uint64_t line = 0; line < count; ++line ) {
        appendGeneratedLogLine( shape, file.lineCount++, lines );
    }
    QFile out{ file.fileName };
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Append ) );
    REQUIRE( out.write( lines.data(), static_cast<qint64>( lines.size() ) )
             == static_cast<qint64>( lines.size() ) );
}

} // namespace

// Following a Log File as it grows (#277): what one change on disk costs once
// the Log File is indexed, run the way the log data runs it on a change
// notification. Appends to the generated Log Files, so it runs last.
TEST_CASE( "Following a growing Log File", "[logdata-benchmark][tailing]" )
{
    // How many Log Lines each append adds: a few KB.
    constexpr std::uint64_t AppendedLines = 20;

    for ( const auto shape : Shapes ) {
        generatedLogFiles->logFile( shape );
    }

    for ( const auto fastModificationDetection : { false, true } ) {
        for ( const auto shape : Shapes ) {
            auto& file = generatedLogFiles->logFile( shape );

            auto policy = testSettingsPolicies().indexing;
            policy.useIndexCache = false;
            policy.fastModificationDetection = fastModificationDetection;

            // Indexed once, not measured.
            auto data = std::make_shared<IndexingData>();
            AtomicFlag interruptRequest;
            {
                FullIndexOperation indexing{ file.fileName, data, interruptRequest, policy };
                REQUIRE( std::get<bool>( indexing.run() ) );
            }

            const auto name = [ & ]( const char* how ) {
                return caseName( shape, how )
                       + ( fastModificationDetection ? ", fast modification detection" : "" );
            };

            // A change notification for an append: the check, then indexing
            // the appended Log Lines. Writing them is measured too, and is
            // the same on both sides of a comparison.
            BENCHMARK( name( "append, check and index the appended Log Lines" ) )
            {
                appendLogLines( shape, file, AppendedLines );
                CheckFileChangesOperation check{ file.fileName, data, interruptRequest, policy };
                const auto status = std::get<MonitoredFileStatus>( check.run() );
                PartialIndexOperation indexing{ file.fileName, data, interruptRequest, policy };
                indexing.run();
                return status == MonitoredFileStatus::DataAdded;
            };

            // A change notification for bytes already indexed, as a writer
            // appending in quick succession causes.
            BENCHMARK( name( "check with nothing appended" ) )
            {
                CheckFileChangesOperation check{ file.fileName, data, interruptRequest, policy };
                return std::get<MonitoredFileStatus>( check.run() )
                       == MonitoredFileStatus::Unchanged;
            };

            IndexingData::ConstAccessor accessor{ data.get() };
            REQUIRE( accessor.getNbLines().get() == file.lineCount );
        }
    }
}

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    GeneratedLogFiles files;
    generatedLogFiles = &files;
    const auto result = Catch::Session().run( argc, argv );
    generatedLogFiles = nullptr;
    return result;
}
