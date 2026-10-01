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

// Loading, scrolling and searching a Log File of about 1 GB with and without
// ANSI color sequences, showing them as text and hiding them (#573). Show
// colors loads and searches as Hide does -- the same Decoding Policy -- and
// adds only a read of the Log Lines entering the Viewport with their colors,
// which the scroll case measures where log data offers it.
//
// Not Catch2 BENCHMARKs: a sample of loading 1 GB takes seconds, so each case
// runs once per process and prints its milliseconds as a RESULT line; runs of
// two builds are alternated and their medians compared. Uses only what log
// data offered before #573 (the Show colors read is compiled in only where it
// exists), so the same file measures both sides of an A/B comparison.
//
// Set LOGSQUIRL_BENCHMARK_LOG_FILE_MB for smaller Log Files. They are written
// into a temporary directory under TMPDIR, removed at the end.

#include "linetypes.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint64_t Mib = 1024 * 1024;

std::uint64_t logFileBytes()
{
    bool isNumber = false;
    const auto requested = qgetenv( "LOGSQUIRL_BENCHMARK_LOG_FILE_MB" ).toULongLong( &isNumber );
    return ( isNumber && requested > 0 ) ? requested * Mib : 1024 * Mib;
}

// Log Line number index, as a colored logger writes it or without the colors.
void appendLogLine( std::uint64_t index, bool colored, std::string& out )
{
    const auto scrambled = index * 2654435761u;
    const char* level = ( index % 97 == 0 ) ? "ERROR" : ( index % 13 == 0 ) ? "WARN " : "INFO ";
    const char* levelColor = ( index % 97 == 0 )   ? "\x1B[31m"
                             : ( index % 13 == 0 ) ? "\x1B[33m"
                                                   : "\x1B[32m";
    const auto color = [ colored, &out ]( const char* sequence ) {
        if ( colored ) {
            out += sequence;
        }
    };

    out += "2026-09-17 12:34:56.";
    out += std::to_string( 100 + index % 900 );
    out += ' ';
    color( levelColor );
    out += level;
    color( "\x1B[0m" );
    out += " [";
    color( "\x1B[36m" );
    out += "worker-";
    out += std::to_string( index % 8 );
    color( "\x1B[0m" );
    out += "] request ";
    out += std::to_string( scrambled % 10'000'000u );
    out += " handled in ";
    out += std::to_string( scrambled % 997 );
    out += " ms by the ";
    color( "\x1B[1m" );
    out += "frontend";
    color( "\x1B[0m" );
    out += '\n';
}

bool writeLogFile( const QString& fileName, bool colored )
{
    QFile file{ fileName };
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    const auto bytes = logFileBytes();
    std::string chunk;
    std::uint64_t index = 0;
    std::uint64_t total = 0;
    while ( total < bytes ) {
        chunk.clear();
        while ( chunk.size() < 4 * Mib ) {
            appendLogLine( index++, colored, chunk );
        }
        if ( file.write( chunk.data(), static_cast<qint64>( chunk.size() ) )
             != static_cast<qint64>( chunk.size() ) ) {
            return false;
        }
        total += chunk.size();
    }
    return true;
}

using Clock = std::chrono::steady_clock;

double millisecondsSince( Clock::time_point start )
{
    return std::chrono::duration<double, std::milli>( Clock::now() - start ).count();
}

void report( const char* file, const char* mode, const char* operation, double milliseconds )
{
    std::cout << "RESULT\t" << file << '\t' << mode << '\t' << operation << '\t' << milliseconds
              << std::endl;
}

std::unique_ptr<LogData> newLogData( bool hideAnsiColorSequences )
{
    auto policies = testSettingsPolicies();
    policies.indexing.useIndexCache = false;
    policies.search.useResultsCache = false;
    policies.decoding.hideAnsiColorSequences = hideAnsiColorSequences;
    return std::make_unique<LogData>( policies.indexing, policies.search, policies.fileAccess,
                                      policies.decoding );
}

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

// A screen of Log Lines at 2,000 places spread over the Log File, as dragging
// the scrollbar over it reads them.
constexpr std::uint64_t ScrollJumps = 2'000;
constexpr std::uint64_t ScreenLines = 60;

template <typename Read>
std::uint64_t scroll( const LogData& logData, Read read )
{
    const auto lines = logData.getNbLine().get();
    std::uint64_t characters = 0;
    for ( std::uint64_t jump = 0; jump < ScrollJumps; ++jump ) {
        const auto first = ( lines - ScreenLines ) * jump / ScrollJumps;
        characters += read( LineNumber( first ), LinesCount( ScreenLines ) );
    }
    return characters;
}

// What Show colors reads for a screen, where log data offers it (#573).
template <typename Data>
bool scrollWithAnsiColors( const Data& logData, double& milliseconds )
{
    if constexpr ( requires { logData.getAnsiColoredLines( LineNumber( 0 ), LinesCount( 1 ) ); } ) {
        const auto start = Clock::now();
        const auto characters
            = scroll( logData, [ &logData ]( LineNumber first, LinesCount count ) {
                  std::uint64_t size = 0;
                  for ( const auto& line : logData.getAnsiColoredLines( first, count ) ) {
                      size += static_cast<std::uint64_t>( line.text.size() ) + line.spans.size();
                  }
                  return size;
              } );
        milliseconds = millisecondsSince( start );
        return characters > 0;
    }
    else {
        return false;
    }
}

// Runs a Search for pattern over the whole Log File and waits for it.
LinesCount search( const LogData& logData, const QString& pattern )
{
    auto filtered = logData.getNewFilteredData();
    QEventLoop loop;
    QObject::connect( filtered.get(), &LogFilteredData::searchStateChanged, &loop,
                      [ &loop ]( const SearchSession::State& state ) {
                          if ( state.phase == SearchSession::Phase::Complete ) {
                              loop.quit();
                          }
                      } );
    filtered->request( RegularExpressionPattern( pattern ) );
    loop.exec();
    return filtered->getNbMatches();
}

QTemporaryDir* directory = nullptr;

} // namespace

TEST_CASE( "loading, scrolling and searching a Log File with ANSI color sequences",
           "[ansi-log-file-benchmark][wall-clock]" )
{
    REQUIRE( directory != nullptr );
    REQUIRE( directory->isValid() );

    for ( const bool colored : { false, true } ) {
        const auto* fileKind = colored ? "with-ansi" : "without-ansi";
        const auto fileName = directory->filePath( QString::fromLatin1( fileKind ) + ".log" );
        REQUIRE( writeLogFile( fileName, colored ) );

        for ( const bool hide : { false, true } ) {
            const auto* mode = hide ? "hide" : "show-as-text";
            auto logData = newLogData( hide );

            auto start = Clock::now();
            REQUIRE( indexLogFile( *logData, fileName ) == LoadingStatus::Successful );
            report( fileKind, mode, "load", millisecondsSince( start ) );

            start = Clock::now();
            const auto characters
                = scroll( *logData, [ &logData ]( LineNumber first, LinesCount count ) {
                      std::uint64_t size = 0;
                      for ( const auto& line : logData->getLines( first, count ) ) {
                          size += static_cast<std::uint64_t>( line.size() );
                      }
                      return size;
                  } );
            report( fileKind, mode, "scroll", millisecondsSince( start ) );
            REQUIRE( characters > 0 );

            // Hide and Show colors decode alike; only Show colors reads the
            // Log Lines of a screen with their colors.
            double showColorsScroll = 0;
            if ( hide && scrollWithAnsiColors( *logData, showColorsScroll ) ) {
                report( fileKind, "show-colors", "scroll", showColorsScroll );
            }

            // A regular expression that, with the sequences shown as text,
            // also matches the colored Log Lines: it runs over every one.
            start = Clock::now();
            const auto matches
                = search( *logData, QStringLiteral( "ERROR.*handled in 1[0-9] ms" ) );
            report( fileKind, mode, "search", millisecondsSince( start ) );
            REQUIRE( matches.get() > 0 );
        }

        QFile::remove( fileName );
    }
}

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    QTemporaryDir temporary{ QDir::tempPath() + "/logsquirl_ansi_log_file_benchmark_XXXXXX" };
    directory = &temporary;
    const auto result = Catch::Session().run( argc, argv );
    directory = nullptr;
    return result;
}
