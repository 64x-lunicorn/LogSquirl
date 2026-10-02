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

#include "appendlatency.h"

using namespace logsquirl::benchmark;
using namespace std::chrono_literals;

TEST_CASE( "An appended Log Line is shown by the first paint that shows it", "[benchmark]" )
{
    const auto t = Clock::now();
    AppendLatency latency;

    latency.appended( 100, t );
    CHECK( latency.waitingCount() == 1 );

    // The Log File through Log Line 99: not yet the one appended.
    CHECK( latency.shown( 99, t + 3ms ).empty() );
    CHECK( latency.waitingCount() == 1 );

    const auto shown = latency.shown( 100, t + 12ms );
    REQUIRE( shown.size() == 1 );
    CHECK( shown.front() == Catch::Approx( 12.0 ) );
    CHECK( latency.waitingCount() == 0 );
    REQUIRE( latency.latencies().size() == 1 );
    CHECK( latency.latencies().front() == Catch::Approx( 12.0 ) );
}

TEST_CASE( "One paint shows every Log Line appended up to the last it shows", "[benchmark]" )
{
    const auto t = Clock::now();
    AppendLatency latency;

    latency.appended( 10, t );
    latency.appended( 11, t + 1ms );
    latency.appended( 12, t + 2ms );

    // A following view scrolled past 10 and shows 11 and 12: 10 went by.
    const auto shown = latency.shown( 11, t + 5ms );
    REQUIRE( shown.size() == 2 );
    CHECK( shown[ 0 ] == Catch::Approx( 5.0 ) );
    CHECK( shown[ 1 ] == Catch::Approx( 4.0 ) );
    CHECK( latency.waitingCount() == 1 );

    const auto last = latency.shown( 12, t + 9ms );
    REQUIRE( last.size() == 1 );
    CHECK( last.front() == Catch::Approx( 7.0 ) );
}

TEST_CASE( "A Log Line is shown once", "[benchmark]" )
{
    const auto t = Clock::now();
    AppendLatency latency;

    latency.appended( 5, t );
    CHECK( latency.shown( 5, t + 2ms ).size() == 1 );
    // Painted again: nothing new appended was shown.
    CHECK( latency.shown( 5, t + 4ms ).empty() );
    CHECK( latency.shown( 4, t + 6ms ).empty() );
    CHECK( latency.latencies().size() == 1 );
}

TEST_CASE( "The latencies of the shown Log Lines are summarized", "[benchmark]" )
{
    const auto t = Clock::now();
    AppendLatency latency;

    for ( auto line = 0; line < 4; ++line ) {
        latency.appended( static_cast<std::uint64_t>( line ), t + line * 1ms );
    }
    latency.shown( 3, t + 10ms );

    const auto summary = latency.distribution();
    CHECK( summary.count == 4 );
    CHECK( summary.min == Catch::Approx( 7.0 ) );
    CHECK( summary.max == Catch::Approx( 10.0 ) );
    CHECK( latency.lastShown() == std::optional<std::uint64_t>{ 3 } );
}

TEST_CASE( "Nothing appended, nothing shown", "[benchmark]" )
{
    AppendLatency latency;

    CHECK( latency.shown( 1000, Clock::now() ).empty() );
    CHECK( latency.distribution().count == 0 );
    CHECK_FALSE( latency.lastShown().has_value() );
}
