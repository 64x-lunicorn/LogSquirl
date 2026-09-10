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

#include <catch2/catch.hpp>

#include "logdata.h"
#include "searchsession.h"
#include "test_policies.h"

using Phase = SearchSession::Phase;

// These scenarios drive the Session through its states directly (no
// event loop, no attached file): the pattern-validity check and the
// continuation-vs-fresh decision are both made synchronously, before
// anything touches the worker thread, so they are observable right after
// the call returns.

SCENARIO( "A Search Session starts idle", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    SearchSession session( logData, policies.search );

    THEN( "its phase is Idle with no matches" )
    {
        const auto state = session.state();
        REQUIRE( state.phase == Phase::Idle );
        REQUIRE( state.matchCount == 0_lcount );
        REQUIRE( session.matches().cardinality() == 0 );
    }
}

SCENARIO( "Requesting an invalid pattern goes to InvalidPattern without running anything",
         "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    SearchSession session( logData, policies.search );

    GIVEN( "a pattern that fails to compile as a regex" )
    {
        const RegularExpressionPattern badPattern( "[unterminated" );

        WHEN( "it is requested" )
        {
            session.request( badPattern, 0_lnum, 100_lnum );

            THEN( "the phase is InvalidPattern and carries an error string" )
            {
                const auto state = session.state();
                REQUIRE( state.phase == Phase::InvalidPattern );
                REQUIRE_FALSE( state.errorString.isEmpty() );
                REQUIRE( state.matchCount == 0_lcount );
                REQUIRE( session.matches().cardinality() == 0 );
            }
        }
    }
}

SCENARIO( "Requesting with no pattern goes idle", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    SearchSession session( logData, policies.search );

    GIVEN( "a Session that was left in InvalidPattern" )
    {
        session.request( RegularExpressionPattern( "[unterminated" ), 0_lnum, 100_lnum );
        REQUIRE( session.state().phase == Phase::InvalidPattern );

        WHEN( "request() is called with no pattern" )
        {
            session.request();

            THEN( "the phase goes back to Idle" )
            {
                const auto state = session.state();
                REQUIRE( state.phase == Phase::Idle );
                REQUIRE( state.matchCount == 0_lcount );
                REQUIRE( state.errorString.isEmpty() );
            }
        }
    }
}

SCENARIO( "stop() is a no-op when nothing is running", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    SearchSession session( logData, policies.search );

    WHEN( "stop() is called on an idle Session" )
    {
        session.stop();

        THEN( "it stays Idle rather than reporting Interrupted" )
        {
            REQUIRE( session.state().phase == Phase::Idle );
        }
    }
}

// Cache-hit adoption (matches/maxLength/fromCache, and that it rebuilds
// Context Lines the same way a real completion does) and "an invalid
// pattern discards a previous run's results" both need a real completed
// search to set up their "previous result" -- which needs a real attached,
// indexed LogData and the worker thread. Both are exercised end-to-end in
// tests/ui/logfiltereddata_test.cpp instead, alongside the
// continuation-vs-fresh decision for a *valid* pattern, for the same
// reason.
