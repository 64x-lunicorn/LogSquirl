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

#include <QString>
#include <QtGlobal>

#include "hyperscanruntime.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"

SCENARIO( "The Hyperscan runtime uses AVX2 only where the CPU has it", "[hyperscanruntime]" )
{
    GIVEN( "a CPU with AVX2" )
    {
        THEN( "the AVX2 runtime is chosen" )
        {
            REQUIRE( chooseHyperscanRuntime( true, false ) == HyperscanRuntime::Avx2 );
        }

        THEN( "the SSE runtime is chosen when AVX2 is disabled" )
        {
            REQUIRE( chooseHyperscanRuntime( true, true ) == HyperscanRuntime::Sse42 );
        }
    }

    GIVEN( "a CPU without AVX2" )
    {
        THEN( "the SSE runtime is chosen" )
        {
            REQUIRE( chooseHyperscanRuntime( false, false ) == HyperscanRuntime::Sse42 );
            REQUIRE( chooseHyperscanRuntime( false, true ) == HyperscanRuntime::Sse42 );
        }
    }
}

SCENARIO( "A Search runs on the Hyperscan runtime chosen for this CPU",
          "[hyperscanruntime][regex]" )
{
    GIVEN( "a pattern compiled for Vectorscan" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "err(or)?", true, false, false, false ),
            RegexpEngine::Vectorscan );
        REQUIRE( expression.isValid() );

        WHEN( "it matches Log Lines" )
        {
            auto matcher = expression.createMatcher();
            REQUIRE( matcher->hasMatch( "2026-01-01 error: something broke" ) );
            REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 info: all good" ) );

            THEN( "the runtime that matched is the chosen one" )
            {
                if ( !isHyperscanRuntimeChosenAtRunTime() ) {
                    REQUIRE( loadedHyperscanRuntime() == HyperscanRuntime::Linked );
                    return;
                }

                // The CTest variant that disables AVX2 names the runtime it
                // expects, so the check does not depend on the runner's CPU
                // alone.
                auto expected = chooseHyperscanRuntime( cpuSupportsHyperscanAvx2(),
                                                        isHyperscanAvx2Disabled() );
                if ( qEnvironmentVariableIsSet( "LOGSQUIRL_EXPECT_HYPERSCAN_RUNTIME" ) ) {
                    expected
                        = qEnvironmentVariable( "LOGSQUIRL_EXPECT_HYPERSCAN_RUNTIME" ) == "avx2"
                              ? HyperscanRuntime::Avx2
                              : HyperscanRuntime::Sse42;
                }

                INFO( "chosen runtime " << hyperscanRuntimeName( expected ) );
                REQUIRE( loadedHyperscanRuntime() == expected );
            }
        }
    }
}
