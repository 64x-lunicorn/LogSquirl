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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonObject>

#include "benchmarkreport.h"
#include "processclock.h"

using namespace logsquirl::benchmark;
using namespace std::chrono_literals;

namespace {

// A clock whose process started 50 ms before main() was entered.
ProcessClock clockStartedBefore( Clock::time_point mainEntered )
{
    return ProcessClock{ mainEntered, std::chrono::nanoseconds{ 50ms } };
}

} // namespace

TEST_CASE( "A benchmark report says what it is and which version of the format", "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    const BenchmarkReport report{ "open-and-index", clockStartedBefore( mainEntered ) };

    const auto json = report.toJson( std::nullopt );

    CHECK( json[ "format" ].toString() == "logsquirl-benchmark" );
    CHECK( json[ "format_version" ].toInt() == BenchmarkReport::FormatVersion );
    CHECK( BenchmarkReport::FormatVersion == 1 );
    CHECK( json[ "scenario" ].toString() == "open-and-index" );
    CHECK( json[ "outcome" ].toString() == "passed" );
    CHECK_FALSE( json.contains( "failure" ) );
}

TEST_CASE( "An event is timed since the process started and since the scenario started",
           "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    BenchmarkReport report{ "open-and-index", clockStartedBefore( mainEntered ) };

    report.markScenarioStart( mainEntered + 100ms );
    report.eventAt( "index_finished", mainEntered + 130ms, QJsonObject{ { "lines", 42 } } );

    const auto json = report.toJson( std::nullopt );
    CHECK( json[ "scenario_started_ms" ].toDouble() == Catch::Approx( 150.0 ) );

    const auto events = json[ "events" ].toArray();
    REQUIRE( events.size() == 1 );
    const auto event = events.at( 0 ).toObject();
    CHECK( event[ "name" ].toString() == "index_finished" );
    CHECK( event[ "since_process_start_ms" ].toDouble() == Catch::Approx( 180.0 ) );
    CHECK( event[ "since_scenario_start_ms" ].toDouble() == Catch::Approx( 30.0 ) );
    CHECK( event[ "data" ].toObject()[ "lines" ].toInt() == 42 );

    CHECK( report.hasEvent( "index_finished" ) );
    CHECK_FALSE( report.hasEvent( "first_log_line_displayed" ) );
    REQUIRE( report.millisecondsSinceScenarioStart( "index_finished" ).has_value() );
    CHECK( *report.millisecondsSinceScenarioStart( "index_finished" ) == Catch::Approx( 30.0 ) );
}

TEST_CASE( "Events are reported in the order they happened", "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    BenchmarkReport report{ "open-and-index", clockStartedBefore( mainEntered ) };
    report.markScenarioStart( mainEntered );

    report.eventAt( "second", mainEntered + 20ms );
    report.eventAt( "first", mainEntered + 10ms );

    const auto events = report.toJson( std::nullopt )[ "events" ].toArray();
    REQUIRE( events.size() == 2 );
    CHECK( events.at( 0 ).toObject()[ "name" ].toString() == "first" );
    CHECK( events.at( 1 ).toObject()[ "name" ].toString() == "second" );
}

TEST_CASE( "An event without data has no data field", "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    BenchmarkReport report{ "open-and-index", clockStartedBefore( mainEntered ) };
    report.markScenarioStart( mainEntered );
    report.eventAt( "index_finished", mainEntered + 1ms );

    const auto event = report.toJson( std::nullopt )[ "events" ].toArray().at( 0 ).toObject();
    CHECK_FALSE( event.contains( "data" ) );
}

TEST_CASE( "The report carries the process, the options, the Log Files and the results",
           "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    BenchmarkReport report{ "open-and-index", clockStartedBefore( mainEntered ) };

    report.setOption( "rate", "100" );
    report.addLogFile( "/tmp/a.log", 1234 );
    report.setResult( "log_line_count", 10 );
    report.setApplication( QJsonObject{ { "version", "1.2.3" } } );
    report.setPlatform( QJsonObject{ { "qpa_platform", "offscreen" } } );

    const auto json = report.toJson( std::int64_t{ 1'000'000 } );

    const auto process = json[ "process" ].toObject();
    CHECK( process[ "start_source" ].toString() == "os" );
    CHECK( process[ "main_entered_ms" ].toDouble() == Catch::Approx( 50.0 ) );
    CHECK( process[ "peak_rss_bytes" ].toDouble() == Catch::Approx( 1'000'000.0 ) );

    CHECK( json[ "options" ].toObject()[ "rate" ].toString() == "100" );
    const auto logFiles = json[ "log_files" ].toArray();
    REQUIRE( logFiles.size() == 1 );
    CHECK( logFiles.at( 0 ).toObject()[ "path" ].toString() == "/tmp/a.log" );
    CHECK( logFiles.at( 0 ).toObject()[ "size_bytes" ].toInteger() == 1234 );
    CHECK( json[ "results" ].toObject()[ "log_line_count" ].toInt() == 10 );
    CHECK( json[ "application" ].toObject()[ "version" ].toString() == "1.2.3" );
    CHECK( json[ "platform" ].toObject()[ "qpa_platform" ].toString() == "offscreen" );
}

TEST_CASE( "Without a known process start the times count from main()", "[benchmark]" )
{
    const auto mainEntered = Clock::now();
    BenchmarkReport report{ "open-and-index", ProcessClock{ mainEntered, std::nullopt } };
    report.markScenarioStart( mainEntered + 5ms );

    const auto json = report.toJson( std::nullopt );
    CHECK( json[ "process" ].toObject()[ "start_source" ].toString() == "main" );
    CHECK( json[ "process" ].toObject()[ "main_entered_ms" ].toDouble() == Catch::Approx( 0.0 ) );
    CHECK( json[ "scenario_started_ms" ].toDouble() == Catch::Approx( 5.0 ) );
    // A peak RSS the platform does not tell is left out, never reported as 0.
    CHECK_FALSE( json[ "process" ].toObject().contains( "peak_rss_bytes" ) );
}

TEST_CASE( "A failed report keeps the first reason", "[benchmark]" )
{
    BenchmarkReport report{ "open-and-index", ProcessClock{ Clock::now(), std::nullopt } };

    CHECK_FALSE( report.failed() );
    report.fail( "the Log File could not be opened" );
    report.fail( "timed out" );

    CHECK( report.failed() );
    const auto json = report.toJson( std::nullopt );
    CHECK( json[ "outcome" ].toString() == "failed" );
    CHECK( json[ "failure" ].toString() == "the Log File could not be opened" );
}

TEST_CASE( "A distribution of durations reports nearest-rank percentiles", "[benchmark]" )
{
    std::vector<double> samples;
    for ( int sample = 100; sample >= 1; --sample ) {
        samples.push_back( static_cast<double>( sample ) );
    }

    const auto distribution = Distribution::of( samples );

    CHECK( distribution.count == 100 );
    CHECK( distribution.min == Catch::Approx( 1.0 ) );
    CHECK( distribution.p50 == Catch::Approx( 50.0 ) );
    CHECK( distribution.p99 == Catch::Approx( 99.0 ) );
    CHECK( distribution.max == Catch::Approx( 100.0 ) );
    CHECK( distribution.mean == Catch::Approx( 50.5 ) );

    const auto json = distribution.toJson();
    CHECK( json[ "count" ].toInt() == 100 );
    CHECK( json[ "p50_ms" ].toDouble() == Catch::Approx( 50.0 ) );
    CHECK( json[ "p99_ms" ].toDouble() == Catch::Approx( 99.0 ) );
    CHECK( json[ "max_ms" ].toDouble() == Catch::Approx( 100.0 ) );
}

TEST_CASE( "A distribution of one sample is that sample everywhere", "[benchmark]" )
{
    const auto distribution = Distribution::of( { 7.5 } );

    CHECK( distribution.count == 1 );
    CHECK( distribution.p50 == Catch::Approx( 7.5 ) );
    CHECK( distribution.p99 == Catch::Approx( 7.5 ) );
    CHECK( distribution.max == Catch::Approx( 7.5 ) );
}

TEST_CASE( "An empty distribution reports only its count", "[benchmark]" )
{
    const auto json = Distribution::of( {} ).toJson();

    CHECK( json[ "count" ].toInt() == 0 );
    CHECK_FALSE( json.contains( "p50_ms" ) );
}

TEST_CASE( "This process knows when it started and how much memory it used at most", "[benchmark]" )
{
    const auto sinceStart = timeSinceProcessStart();
    REQUIRE( sinceStart.has_value() );
    CHECK( sinceStart->count() >= 0 );
    // A test process started less than a day ago.
    CHECK( *sinceStart < std::chrono::hours{ 24 } );

    const auto peak = peakResidentBytes();
    REQUIRE( peak.has_value() );
    CHECK( *peak > 1024 * 1024 );
}

TEST_CASE( "A process clock measured now puts main() after the process start", "[benchmark]" )
{
    const auto clock = ProcessClock::measure( Clock::now() );

    CHECK( clock.knowsProcessStart() );
    CHECK( clock.mainEnteredMilliseconds() >= 0.0 );
    CHECK( clock.millisecondsSinceProcessStart( Clock::now() ) >= clock.mainEnteredMilliseconds() );
}
