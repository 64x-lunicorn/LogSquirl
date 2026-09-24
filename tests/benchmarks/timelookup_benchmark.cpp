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

// "Go to timestamp" on a Log File of 10 million Log Lines (#435): the lookup
// of the first Log Line at or after a time, by binary search over the Log
// Lines read through the Index. Must return in under 50 ms in an optimized
// build; a Debug build is several times slower and says nothing.
//
// Writes the Log File at run time into a temporary file; it is never checked
// in. LOGSQUIRL_BENCHMARK_LOG_LINES writes fewer Log Lines, for a quick run.

#include "logdata.h"
#include "logformatparser.h"
#include "test_policies.h"
#include "timelookup.h"
#include "timestampreader.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryFile>
#include <QTimeZone>
#include <QTimer>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr qint64 MillisecondsPerLine = 25;

const char* const FormatJson = R"({
    "bench_log": {
        "title": "Benchmark",
        "regex": {
            "std": { "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}) (?<level>\\w+) (?<body>.*)$" }
        },
        "timestamp-field": "timestamp",
        "timestamp-format": [ "%Y-%m-%d %H:%M:%S.%L" ],
        "sample": [ { "line": "2026-09-01 00:00:00.000 INFO started" } ]
    }
})";

qint64 lineCount()
{
    bool isNumber = false;
    const auto requested = qgetenv( "LOGSQUIRL_BENCHMARK_LOG_LINES" ).toLongLong( &isNumber );
    return ( isNumber && requested > 0 ) ? requested : 10'000'000;
}

// Log Line number `index`: 2026-09-01 00:00:00.000 plus 25 ms per line, except
// that every hundredth Log Line is a stack trace line without a Timestamp.
std::string logLine( qint64 index )
{
    if ( index % 100 == 99 ) {
        return "    at com.example.Handler.run(Handler.java:42)\n";
    }
    const auto ms = index * MillisecondsPerLine;
    const auto day = 1 + ms / 86'400'000;
    const auto inDay = ms % 86'400'000;
    char buffer[ 128 ];
    std::snprintf( buffer, sizeof( buffer ),
                   "2026-09-%02d %02d:%02d:%02d.%03d %s request %lld handled in time\n",
                   static_cast<int>( day ), static_cast<int>( inDay / 3'600'000 ),
                   static_cast<int>( inDay / 60'000 % 60 ), static_cast<int>( inDay / 1000 % 60 ),
                   static_cast<int>( inDay % 1000 ), index % 13 == 0 ? "WARN" : "INFO",
                   static_cast<long long>( index ) );
    return buffer;
}

QDateTime timeOfLine( qint64 index )
{
    return QDateTime( QDate( 2026, 9, 1 ), QTime( 0, 0 ), QTimeZone::UTC )
        .addMSecs( index * MillisecondsPerLine );
}

} // namespace

TEST_CASE( "Go to timestamp on a Log File of 10 million Log Lines", "[timelookup-benchmark]" )
{
    const auto lines = lineCount();

    QTemporaryFile file( "timelookup_benchmark_XXXXXX" );
    REQUIRE( file.open() );
    {
        std::string block;
        for ( qint64 line = 0; line < lines; ++line ) {
            block += logLine( line );
            if ( block.size() > ( 1 << 20 ) ) {
                file.write( block.data(), static_cast<qint64>( block.size() ) );
                block.clear();
            }
        }
        file.write( block.data(), static_cast<qint64>( block.size() ) );
        file.flush();
    }

    auto policies = testSettingsPolicies();
    LogData logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding );
    {
        QEventLoop loop;
        QObject::connect( &logData, &LogData::loadingFinished, &loop, &QEventLoop::quit );
        QTimer::singleShot( 600'000, &loop, &QEventLoop::quit );
        logData.attachFile( file.fileName() );
        loop.exec();
    }
    REQUIRE( logData.getNbLine() == LinesCount( static_cast<uint64_t>( lines ) ) );

    auto formats = LogFormatParser::parseJsonString( FormatJson );
    REQUIRE( formats.size() == 1 );

    // The reader is built once, as the application builds it on first use.
    QElapsedTimer timer;
    timer.start();
    const TimestampReader reader( formats[ 0 ] );
    std::cout << "building the Timestamp reader: " << timer.nsecsElapsed() / 1000 << " us\n";

    std::vector<double> milliseconds;
    for ( int i = 1; i <= 20; ++i ) {
        const qint64 target = lines * i / 21 + i * 7;
        // A Log Line that has a Timestamp, so the answer is that line itself.
        const auto expected = target % 100 == 99 ? target + 1 : target;

        timer.restart();
        const auto result
            = timelookup::firstLineAtOrAfter( timeOfLine( expected ), logData, reader );
        milliseconds.push_back( static_cast<double>( timer.nsecsElapsed() ) / 1e6 );

        REQUIRE( result );
        CHECK( result->position == timelookup::Position::AtOrAfter );
        CHECK( result->line == LineNumber( static_cast<uint64_t>( expected ) ) );
    }

    std::sort( milliseconds.begin(), milliseconds.end() );
    std::cout << "Go to timestamp over " << lines << " Log Lines: median "
              << milliseconds[ milliseconds.size() / 2 ] << " ms, slowest " << milliseconds.back()
              << " ms\n";

    CHECK( milliseconds.back() < 50.0 );
}

int main( int argc, char* argv[] )
{
    QCoreApplication application( argc, argv );
    return Catch::Session().run( argc, argv );
}
