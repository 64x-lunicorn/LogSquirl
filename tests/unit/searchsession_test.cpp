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

using Phase = SearchSession::Phase;

// These scenarios drive the Session through its states directly (no
// event loop, no attached file): the pattern-validity check and the
// continuation-vs-fresh decision are both made synchronously, before
// anything touches the worker thread, so they are observable right after
// the call returns.

SCENARIO( "A Search Session starts idle", "[searchsession]" )
{
    LogData logData;
    SearchSession session( logData );

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
    LogData logData;
    SearchSession session( logData );

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
    LogData logData;
    SearchSession session( logData );

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
    LogData logData;
    SearchSession session( logData );

    WHEN( "stop() is called on an idle Session" )
    {
        session.stop();

        THEN( "it stays Idle rather than reporting Interrupted" )
        {
            REQUIRE( session.state().phase == Phase::Idle );
        }
    }
}

SCENARIO( "A cache hit completes without touching the worker", "[searchsession]" )
{
    LogData logData;
    SearchSession session( logData );

    GIVEN( "a previously-cached result for a pattern" )
    {
        const RegularExpressionPattern pattern( "error" );
        SearchResultArray cachedMatches;
        cachedMatches.add( uint64_t{ 3 } );
        cachedMatches.add( uint64_t{ 7 } );

        WHEN( "the cache hit is adopted" )
        {
            session.completeFromCache( pattern, 0_lnum, 10_lnum, cachedMatches, 42_length );

            THEN( "the Session reports Complete, fromCache, with the cached matches" )
            {
                const auto state = session.state();
                REQUIRE( state.phase == Phase::Complete );
                REQUIRE( state.fromCache );
                REQUIRE_FALSE( state.isContinuation );
                REQUIRE( state.matchCount == 2_lcount );
                REQUIRE( state.progress == 100 );
                REQUIRE( session.matches().cardinality() == 2 );
                REQUIRE( session.maxLength() == 42_length );
            }
        }
    }
}

SCENARIO( "Requesting an invalid pattern discards a previous run's results",
         "[searchsession]" )
{
    LogData logData;
    SearchSession session( logData );

    GIVEN( "a Session holding results from a completed (cached) run" )
    {
        SearchResultArray previousMatches;
        previousMatches.add( uint64_t{ 1 } );
        previousMatches.add( uint64_t{ 2 } );
        previousMatches.add( uint64_t{ 3 } );
        session.completeFromCache( RegularExpressionPattern( "error" ), 0_lnum, 10_lnum,
                                   previousMatches, 10_length );
        REQUIRE( session.matches().cardinality() == 3 );

        WHEN( "an invalid pattern is requested" )
        {
            session.request( RegularExpressionPattern( "[unterminated" ), 0_lnum, 100_lnum );

            THEN( "the previous run's results are gone, not just uncounted" )
            {
                REQUIRE( session.state().phase == Phase::InvalidPattern );
                REQUIRE( session.matches().cardinality() == 0 );
                REQUIRE( session.maxLength() == 0_length );
            }
        }
    }
}

// The continuation-vs-fresh decision for a *valid* pattern is exercised
// end-to-end (through a real attached LogData and the worker thread) in
// tests/ui/logfiltereddata_test.cpp instead: it needs a real, indexed
// LogData to search against, which belongs with the other itests that
// already set that up rather than being reproduced here.
