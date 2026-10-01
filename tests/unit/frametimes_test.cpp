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

#include "frametimes.h"

using namespace logsquirl::benchmark;
using namespace std::chrono_literals;

TEST_CASE( "A frame lasts from the start of its paint to its end", "[benchmark]" )
{
    const auto t = Clock::now();
    FrameTimes frames;

    frames.painted( t + 2ms, t + 7ms );
    frames.painted( t + 20ms, t + 21500us );

    CHECK( frames.count() == 2 );
    const auto distribution = frames.distribution();
    CHECK( distribution.min == Catch::Approx( 1.5 ) );
    CHECK( distribution.max == Catch::Approx( 5.0 ) );
}

TEST_CASE( "The frame budget is one frame at 60 Hz", "[benchmark]" )
{
    CHECK( FrameTimes::DefaultBudgetMs == Catch::Approx( 1000.0 / 60.0 ) );
    CHECK( FrameTimes{}.budgetMs() == Catch::Approx( 16.6667 ).epsilon( 1e-4 ) );
}

TEST_CASE( "Only a frame longer than the budget is over it", "[benchmark]" )
{
    const auto t = Clock::now();
    FrameTimes frames{ 10.0 };

    frames.painted( t, t + 9ms );
    // Exactly the budget still fits in it.
    frames.painted( t, t + 10ms );
    frames.painted( t, t + 10100us );
    frames.painted( t, t + 40ms );

    CHECK( frames.count() == 4 );
    CHECK( frames.overBudget() == 2 );
}

TEST_CASE( "No frame is none over the budget", "[benchmark]" )
{
    FrameTimes frames;

    CHECK( frames.count() == 0 );
    CHECK( frames.overBudget() == 0 );

    const auto json = frames.toJson();
    CHECK( json.value( "count" ).toInteger() == 0 );
    CHECK( json.value( "over_budget_count" ).toInteger() == 0 );
    CHECK( json.value( "budget_ms" ).toDouble() == Catch::Approx( FrameTimes::DefaultBudgetMs ) );
    CHECK_FALSE( json.contains( "p50_ms" ) );
}

TEST_CASE( "The frames are reported as a distribution with the frames over budget", "[benchmark]" )
{
    const auto t = Clock::now();
    FrameTimes frames;

    // 1 .. 100 ms: 84 of them over 16.7 ms.
    for ( int frame = 1; frame <= 100; ++frame ) {
        const auto started = t + std::chrono::milliseconds{ 200 * frame };
        frames.painted( started, started + std::chrono::milliseconds{ frame } );
    }

    const auto json = frames.toJson();
    CHECK( json.value( "count" ).toInteger() == 100 );
    CHECK( json.value( "min_ms" ).toDouble() == Catch::Approx( 1.0 ) );
    CHECK( json.value( "p50_ms" ).toDouble() == Catch::Approx( 50.0 ) );
    CHECK( json.value( "p99_ms" ).toDouble() == Catch::Approx( 99.0 ) );
    CHECK( json.value( "max_ms" ).toDouble() == Catch::Approx( 100.0 ) );
    CHECK( json.value( "mean_ms" ).toDouble() == Catch::Approx( 50.5 ) );
    CHECK( json.value( "over_budget_count" ).toInteger() == 84 );
    CHECK( json.value( "budget_ms" ).toDouble() == Catch::Approx( 1000.0 / 60.0 ) );
}
