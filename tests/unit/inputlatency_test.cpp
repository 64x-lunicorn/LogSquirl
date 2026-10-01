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

#include "inputlatency.h"

using namespace logsquirl::benchmark;
using namespace std::chrono_literals;

TEST_CASE( "An input is answered by the first paint that starts after it", "[benchmark]" )
{
    const auto t = Clock::now();
    InputLatency latency;

    latency.inputHandled( t );
    CHECK( latency.waiting() );

    const auto answered = latency.painted( t + 2ms, t + 7ms );

    REQUIRE( answered.size() == 1 );
    CHECK( answered.front() == Catch::Approx( 7.0 ) );
    CHECK_FALSE( latency.waiting() );
    REQUIRE( latency.latencies().size() == 1 );
    CHECK( latency.latencies().front() == Catch::Approx( 7.0 ) );
}

TEST_CASE( "A paint that started before the input does not answer it", "[benchmark]" )
{
    const auto t = Clock::now();
    InputLatency latency;

    latency.inputHandled( t + 5ms );

    // Under way when the input came: it painted what was there before.
    CHECK( latency.painted( t, t + 8ms ).empty() );
    CHECK( latency.waiting() );

    const auto answered = latency.painted( t + 9ms, t + 12ms );
    REQUIRE( answered.size() == 1 );
    CHECK( answered.front() == Catch::Approx( 7.0 ) );
}

TEST_CASE( "A paint without an input waiting answers nothing", "[benchmark]" )
{
    const auto t = Clock::now();
    InputLatency latency;

    CHECK_FALSE( latency.waiting() );
    CHECK( latency.painted( t, t + 1ms ).empty() );

    latency.inputHandled( t + 2ms );
    CHECK( latency.painted( t + 3ms, t + 4ms ).size() == 1 );
    // The input was answered; the paint after it answers nothing.
    CHECK( latency.painted( t + 5ms, t + 6ms ).empty() );
    CHECK( latency.latencies().size() == 1 );
}

TEST_CASE( "One paint answers every input that came before it started", "[benchmark]" )
{
    const auto t = Clock::now();
    InputLatency latency;

    latency.inputHandled( t );
    latency.inputHandled( t + 3ms );
    latency.inputHandled( t + 10ms );

    const auto answered = latency.painted( t + 5ms, t + 6ms );

    // Oldest first; the input after the paint started still waits.
    REQUIRE( answered.size() == 2 );
    CHECK( answered[ 0 ] == Catch::Approx( 6.0 ) );
    CHECK( answered[ 1 ] == Catch::Approx( 3.0 ) );
    CHECK( latency.waiting() );

    CHECK( latency.painted( t + 11ms, t + 15ms ).size() == 1 );
    CHECK_FALSE( latency.waiting() );
}

TEST_CASE( "The latencies of the inputs are summarized as a distribution", "[benchmark]" )
{
    const auto t = Clock::now();
    InputLatency latency;

    for ( int input = 1; input <= 4; ++input ) {
        const auto handled = t + std::chrono::milliseconds{ 100 * input };
        latency.inputHandled( handled );
        latency.painted( handled + 1ms, handled + std::chrono::milliseconds{ input } );
    }

    const auto distribution = latency.distribution();
    CHECK( distribution.count == 4 );
    CHECK( distribution.p50 == Catch::Approx( 2.0 ) );
    CHECK( distribution.p99 == Catch::Approx( 4.0 ) );
    CHECK( distribution.min == Catch::Approx( 1.0 ) );
}
