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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processwork.h"

using namespace logsquirl::benchmark;
using namespace std::chrono_literals;

TEST_CASE( "A stretch of work keeps as many cores busy as its CPU time over its wall time",
           "[benchmark]" )
{
    const auto t = Clock::now();

    const auto work = ProcessWork::between( t, t + 2s, 10s, 15s );

    CHECK( work.wallMs == Catch::Approx( 2000.0 ) );
    REQUIRE( work.cpuMs.has_value() );
    CHECK( *work.cpuMs == Catch::Approx( 5000.0 ) );
    REQUIRE( work.parallelism().has_value() );
    CHECK( *work.parallelism() == Catch::Approx( 2.5 ) );

    const auto json = work.toJson();
    CHECK( json.value( "wall_ms" ).toDouble() == Catch::Approx( 2000.0 ) );
    CHECK( json.value( "cpu_ms" ).toDouble() == Catch::Approx( 5000.0 ) );
    CHECK( json.value( "parallelism" ).toDouble() == Catch::Approx( 2.5 ) );
}

TEST_CASE( "Work done on one core is a parallelism of one", "[benchmark]" )
{
    const auto t = Clock::now();

    const auto work = ProcessWork::between( t, t + 800ms, 1s, 1800ms );

    REQUIRE( work.parallelism().has_value() );
    CHECK( *work.parallelism() == Catch::Approx( 1.0 ) );
}

TEST_CASE( "Work of an unknown CPU time reports its wall time only", "[benchmark]" )
{
    const auto t = Clock::now();

    const auto work = ProcessWork::between( t, t + 1s, std::nullopt, 3s );

    CHECK( work.wallMs == Catch::Approx( 1000.0 ) );
    CHECK_FALSE( work.cpuMs.has_value() );
    CHECK_FALSE( work.parallelism().has_value() );

    const auto json = work.toJson();
    CHECK( json.value( "wall_ms" ).toDouble() == Catch::Approx( 1000.0 ) );
    CHECK_FALSE( json.contains( "cpu_ms" ) );
    CHECK_FALSE( json.contains( "parallelism" ) );
}

TEST_CASE( "Work that took no wall time has no parallelism", "[benchmark]" )
{
    const auto t = Clock::now();

    const auto work = ProcessWork::between( t, t, 1s, 1s );

    CHECK( work.cpuMs.has_value() );
    CHECK_FALSE( work.parallelism().has_value() );
    CHECK_FALSE( work.toJson().contains( "parallelism" ) );
}

TEST_CASE( "This process's CPU time grows while it computes", "[benchmark]" )
{
    const auto before = processCpuTime();
    REQUIRE( before.has_value() );

    // Busy for 100 ms of wall time on this thread.
    const auto wallStart = Clock::now();
    volatile std::uint64_t sum = 0;
    while ( Clock::now() - wallStart < 100ms ) {
        sum = sum + 1;
    }

    const auto after = processCpuTime();
    REQUIRE( after.has_value() );
    CHECK( *after > *before );
    // Every thread of the process together; none of them for longer than
    // the wall time, and this one was busy for most of it.
    const auto spent = *after - *before;
    const auto wall = Clock::now() - wallStart;
    const auto cores = std::max( 1u, std::thread::hardware_concurrency() );
    CHECK( spent <= wall * cores + 20ms );
}

TEST_CASE( "A stretch of work from one moment to another reads both clocks at each", "[benchmark]" )
{
    const auto started = ProcessWork::Moment::now();

    std::this_thread::sleep_for( 20ms );
    const auto work = started.until( ProcessWork::Moment::now() );

    CHECK( work.wallMs >= 19.0 );
    REQUIRE( work.cpuMs.has_value() );
    CHECK( *work.cpuMs >= 0.0 );
}
