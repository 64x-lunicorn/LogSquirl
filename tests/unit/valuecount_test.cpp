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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStringList>
#include <QThreadPool>

#include "growinglogdata.h"
#include "logformatdefinition.h"
#include "valuecount.h"

using namespace std::chrono_literals;
using testing_support::GrowingLogData;

Q_DECLARE_METATYPE( ValueCountResult )

namespace {

LogFormatDefinition levelFormat()
{
    LogFormatDefinition def;
    def.setName( "value_count_test" );
    QHash<QString, QString> regex;
    regex[ "std" ] = R"(^(?<timestamp>\d\d:\d\d) (?<level>[A-Z]+) (?<body>.*)$)";
    def.setRegexPatterns( regex );
    def.setTimestampField( "timestamp" );
    def.setLevelField( "level" );
    def.setBodyField( "body" );
    return def;
}

std::optional<ValueCountResult> count( const QStringList& lines, const ValueOfLine& valueOf,
                                       size_t cap = MaxDistinctValues )
{
    const GrowingLogData logData( lines );
    const std::atomic<bool> cancel{ false };
    std::atomic<uint64_t> done{ 0 };
    return countValues( logData, 0_lnum, LinesCount( static_cast<uint64_t>( lines.size() ) ),
                        valueOf, cap, cancel, done );
}

QStringList sampleLines()
{
    return { "10:00 INFO started", "10:01 ERROR failed", "10:02 INFO going",
             "not a log line",     "10:03 WARN slow",    "10:04 INFO done" };
}

} // namespace

SCENARIO( "The values of a Log Format field are counted", "[valuecount]" )
{
    const auto format = levelFormat();

    GIVEN( "A Log File with a line the format does not match" )
    {
        const auto result = count( sampleLines(), fieldValueOf( format, "level" ) );
        REQUIRE( result.has_value() );

        THEN( "The values are counted, the most frequent first" )
        {
            REQUIRE( result->entries.size() == 3 );
            CHECK( result->entries[ 0 ].value == "INFO" );
            CHECK( result->entries[ 0 ].count == 3 );
            // Equal counts by value.
            CHECK( result->entries[ 1 ].value == "ERROR" );
            CHECK( result->entries[ 2 ].value == "WARN" );
        }
        THEN( "The line the format does not match is left out of the share" )
        {
            CHECK( result->linesCounted == 5 );
            CHECK( result->sharePercent( 3 ) == 60.0 );
            CHECK_FALSE( result->tooManyDistinctValues );
        }
    }

    GIVEN( "A Log File without a line" )
    {
        const auto result = count( {}, fieldValueOf( format, "level" ) );
        THEN( "Nothing is counted" )
        {
            REQUIRE( result.has_value() );
            CHECK( result->entries.isEmpty() );
            CHECK( result->sharePercent( 0 ) == 0.0 );
        }
    }
}

SCENARIO( "The values of a capture group of a regexp are counted", "[valuecount]" )
{
    const QRegularExpression regexp( R"(^\d\d:\d\d (INFO|ERROR)( \w+)?)" );

    THEN( "The regexp counts its groups" )
    {
        CHECK( captureGroupCount( regexp ) == 2 );
        CHECK( captureGroupCount( QRegularExpression( "(" ) ) == 0 );
    }

    GIVEN( "A group that does not always match" )
    {
        const QStringList lines{ "10:00 INFO a", "10:01 INFO", "10:02 ERROR b", "10:03 WARN c" };
        const auto result = count( lines, captureGroupValueOf( regexp, 2 ) );
        REQUIRE( result.has_value() );

        THEN( "Only the lines where the group has a value are counted" )
        {
            CHECK( result->linesCounted == 2 );
            REQUIRE( result->entries.size() == 2 );
            CHECK( result->entries[ 0 ].value == " a" );
        }
    }
}

SCENARIO( "A Value Count stops beyond the cap of distinct values", "[valuecount]" )
{
    QStringList lines;
    for ( int i = 0; i < 20; ++i ) {
        lines << QString( "10:00 %1 x" ).arg( QString( QChar( 'A' + i ) ) );
    }
    const auto format = levelFormat();

    WHEN( "There are more distinct values than the cap" )
    {
        const auto result = count( lines, fieldValueOf( format, "level" ), 19 );
        THEN( "There is no list, partial or otherwise" )
        {
            REQUIRE( result.has_value() );
            CHECK( result->tooManyDistinctValues );
            CHECK( result->entries.isEmpty() );
        }
    }
    WHEN( "There are exactly as many as the cap" )
    {
        const auto result = count( lines, fieldValueOf( format, "level" ), 20 );
        THEN( "They are all counted" )
        {
            REQUIRE( result.has_value() );
            CHECK_FALSE( result->tooManyDistinctValues );
            CHECK( result->entries.size() == 20 );
        }
    }
}

SCENARIO( "A cancelled Value Count returns nothing", "[valuecount]" )
{
    const GrowingLogData logData( sampleLines() );
    const std::atomic<bool> cancel{ true };
    std::atomic<uint64_t> done{ 0 };

    const auto result
        = countValues( logData, 0_lnum, 6_lcount, fieldValueOf( levelFormat(), "level" ),
                       MaxDistinctValues, cancel, done );
    REQUIRE_FALSE( result.has_value() );
}

SCENARIO( "A Value Count runs on a worker thread", "[valuecount]" )
{
    qRegisterMetaType<ValueCountResult>();

    GIVEN( "A Value Count of a Log File" )
    {
        auto logData = std::make_shared<GrowingLogData>( sampleLines() );
        ValueCounter counter;
        QSignalSpy finished( &counter, &ValueCounter::finished );

        WHEN( "It runs to its end" )
        {
            counter.start( logData, fieldValueOf( levelFormat(), "level" ) );
            REQUIRE( counter.isRunning() );

            THEN( "The result is reported" )
            {
                REQUIRE( finished.wait( 5000 ) );
                const auto result = finished.first().first().value<ValueCountResult>();
                CHECK( result.linesCounted == 5 );
                CHECK_FALSE( counter.isRunning() );
            }
        }
    }

    GIVEN( "A count whose worker is held in its read of the Log File" )
    {
        auto logData = std::make_shared<GrowingLogData>( sampleLines() );
        auto* const held = logData.get();
        held->holdNextRead();
        auto counter = std::make_unique<ValueCounter>();
        QSignalSpy finished( counter.get(), &ValueCounter::finished );
        counter->start( logData, fieldValueOf( levelFormat(), "level" ) );
        REQUIRE( held->waitUntilHeld() );

        WHEN( "It is cancelled" )
        {
            QElapsedTimer timer;
            timer.start();
            counter->cancel();

            THEN( "Cancelling returns at once and nothing is reported" )
            {
                CHECK( timer.elapsed() < 1000 );
                CHECK_FALSE( counter->isRunning() );
                held->release();
                QThreadPool::globalInstance()->waitForDone( 10'000 );
                QCoreApplication::processEvents();
                QCoreApplication::processEvents();
                CHECK( finished.isEmpty() );
            }
            held->release();
        }

        WHEN( "The counter is destroyed, as when its tab or Log File is closed" )
        {
            std::weak_ptr<GrowingLogData> weak = logData;
            logData.reset();

            auto destroyed = std::async( std::launch::async, [ &counter ] { counter.reset(); } );
            std::this_thread::sleep_for( 100ms );

            THEN( "It waits for the worker, which does not outlive the log data" )
            {
                CHECK( destroyed.wait_for( 0ms ) == std::future_status::timeout );
                CHECK_FALSE( weak.expired() );
                held->release();
                REQUIRE( destroyed.wait_for( 10s ) == std::future_status::ready );
                CHECK( weak.expired() );
            }
        }
    }
}
