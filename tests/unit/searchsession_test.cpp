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

#include <chrono>
#include <thread>
#include <vector>

#include <QStringList>
#include <QTest>

#include "in_memory_block_source.h"
#include "searchsession.h"
#include "test_policies.h"

using Phase = SearchSession::Phase;

// The first scenarios drive the Session through its states directly (no
// event loop): the pattern-validity check and the continuation-vs-fresh
// decision are both made synchronously, before anything touches the
// worker thread, so they are observable right after the call returns.
// The later ones run real Searches over Log Lines held in memory.

namespace {

QStringList numberedLines( int count, int firstNumber = 0 )
{
    QStringList lines;
    for ( auto number = firstNumber; number < firstNumber + count; ++number ) {
        lines.append( QString( "line %1 %2" ).arg( number ).arg( number % 3 == 0 ? "fizz" : "" ) );
    }
    return lines;
}

// The Log Lines numberedLines() marks "fizz", in [0, count).
SearchResultArray fizzLines( int count )
{
    SearchResultArray lines;
    for ( auto number = 0; number < count; number += 3 ) {
        lines.add( static_cast<uint64_t>( number ) );
    }
    return lines;
}

bool waitUntilSettled( const SearchSession& session )
{
    return QTest::qWaitFor( [ &session ] { return session.state().phase != Phase::Running; },
                            10000 );
}

} // namespace

SCENARIO( "A Search Session starts idle", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource;
    SearchSession session( blockSource, policies.search );

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
    InMemoryBlockSource blockSource;
    SearchSession session( blockSource, policies.search );

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
    InMemoryBlockSource blockSource;
    SearchSession session( blockSource, policies.search );

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
    InMemoryBlockSource blockSource;
    SearchSession session( blockSource, policies.search );

    WHEN( "stop() is called on an idle Session" )
    {
        session.stop();

        THEN( "it stays Idle rather than reporting Interrupted" )
        {
            REQUIRE( session.state().phase == Phase::Idle );
        }
    }
}

SCENARIO( "A Search Session runs a Search over its block source", "[searchsession]" )
{
    auto policies = testSettingsPolicies();
    // Several blocks, so that results are gathered across them.
    policies.search.readBufferSizeLines = 7;
    InMemoryBlockSource blockSource( numberedLines( 50 ) );
    SearchSession session( blockSource, policies.search );

    WHEN( "a pattern is requested over every Log Line" )
    {
        session.request( RegularExpressionPattern( "fizz" ) );
        REQUIRE( waitUntilSettled( session ) );

        THEN( "it completes with the Log Lines that match" )
        {
            const auto state = session.state();
            REQUIRE( state.phase == Phase::Complete );
            REQUIRE( state.matchCount == 17_lcount );
            REQUIRE( session.matches() == fizzLines( 50 ) );
            REQUIRE( session.processedLines() == 50_lcount );
        }

        THEN( "it read the Log Lines in blocks and let go of the reader" )
        {
            REQUIRE( blockSource.readBlocks().size() == 8 );
            REQUIRE( blockSource.attachedReaders() == 0 );
        }
    }
}

SCENARIO( "A Search Session changes its Matches only when it reports a state change",
          "[searchsession]" )
{
    auto policies = testSettingsPolicies();
    // Many small blocks, so that progress is reported before completion.
    policies.search.readBufferSizeLines = 3;
    InMemoryBlockSource blockSource( numberedLines( 300 ) );
    SearchSession session( blockSource, policies.search );

    // What the Matches were at each reported state change, and whether they
    // were ever seen changing in between.
    std::vector<uint64_t> reportedMatchCounts;
    int completions = 0;
    QObject::connect( &session, &SearchSession::stateChanged, &session,
                      [ & ]( const SearchSession::State& state ) {
                          reportedMatchCounts.push_back( session.matches().cardinality() );
                          if ( state.phase == Phase::Complete ) {
                              ++completions;
                          }
                      } );

    WHEN( "a pattern is requested and runs to completion" )
    {
        session.request( RegularExpressionPattern( "fizz" ) );
        bool changedUnreported = false;
        REQUIRE( QTest::qWaitFor(
            [ & ] {
                if ( !reportedMatchCounts.empty()
                     && session.matches().cardinality() != reportedMatchCounts.back() ) {
                    changedUnreported = true;
                }
                return session.state().phase != Phase::Running;
            },
            10000 ) );
        // Let any progress still held in the throttler go.
        QTest::qWait( 250 );

        THEN( "the Matches were complete when completion was reported, and it was reported once" )
        {
            REQUIRE_FALSE( changedUnreported );
            REQUIRE( completions == 1 );
            REQUIRE( reportedMatchCounts.back() == fizzLines( 300 ).cardinality() );
            REQUIRE( session.matches() == fizzLines( 300 ) );
        }
    }
}

SCENARIO( "A Search continues after Log Lines were added", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource( numberedLines( 30 ) );
    SearchSession session( blockSource, policies.search );

    const RegularExpressionPattern pattern( "fizz" );
    session.request( pattern, 0_lnum, 30_lnum );
    REQUIRE( waitUntilSettled( session ) );
    REQUIRE( session.state().phase == Phase::Complete );

    GIVEN( "Log Lines appended to the source" )
    {
        blockSource.appendLines( numberedLines( 20, 30 ) );
        const auto blocksBefore = blockSource.readBlocks().size();

        WHEN( "the same pattern is requested over the grown range" )
        {
            session.request( pattern, 0_lnum, 50_lnum );
            const auto isContinuation = session.state().isContinuation;
            REQUIRE( waitUntilSettled( session ) );

            THEN( "it continues from where it left off and finds the new matches too" )
            {
                REQUIRE( isContinuation );
                const auto state = session.state();
                REQUIRE( state.phase == Phase::Complete );
                REQUIRE( session.matches() == fizzLines( 50 ) );

                // Only the last Log Line searched before is read again: it
                // may have been incomplete.
                const auto blocks = blockSource.readBlocks();
                REQUIRE( blocks.size() == blocksBefore + 1 );
                REQUIRE( blocks.back().first == 29_lnum );
            }
        }
    }
}

SCENARIO( "A new request supersedes the Search in flight", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource( QStringList{ "alpha", "beta", "alpha", "beta", "beta" } );
    SearchSession session( blockSource, policies.search );

    GIVEN( "a Search held in its first block read" )
    {
        blockSource.holdReading();
        session.request( RegularExpressionPattern( "alpha" ) );
        blockSource.waitUntilReadingHeld();
        REQUIRE( session.state().phase == Phase::Running );

        WHEN( "another pattern is requested" )
        {
            // The new run only starts once the held one has stopped, so the
            // held one is let go shortly after the request was made.
            std::thread releaser( [ &blockSource ] {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
                blockSource.releaseReading();
            } );
            session.request( RegularExpressionPattern( "beta" ) );
            releaser.join();
            REQUIRE( waitUntilSettled( session ) );

            THEN( "only the newer Search's results are kept" )
            {
                const auto state = session.state();
                REQUIRE( state.phase == Phase::Complete );
                REQUIRE( state.pattern == RegularExpressionPattern( "beta" ) );
                SearchResultArray betaLines;
                betaLines.add( uint64_t{ 1 } );
                betaLines.add( uint64_t{ 3 } );
                betaLines.add( uint64_t{ 4 } );
                REQUIRE( session.matches() == betaLines );
                REQUIRE( state.matchCount == 3_lcount );
            }

            THEN( "both runs let go of the reader" )
            {
                REQUIRE( QTest::qWaitFor(
                    [ &blockSource ] { return blockSource.attachedReaders() == 0; }, 10000 ) );
            }
        }
    }
}

SCENARIO( "A Search repeated over the same range is served from the cache", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource( numberedLines( 40 ) );
    SearchSession session( blockSource, policies.search );

    const RegularExpressionPattern fizz( "fizz" );
    session.request( fizz );
    REQUIRE( waitUntilSettled( session ) );
    session.request( RegularExpressionPattern( "line 1" ) );
    REQUIRE( waitUntilSettled( session ) );
    REQUIRE( session.state().phase == Phase::Complete );

    WHEN( "the first pattern is requested again" )
    {
        const auto blocksBefore = blockSource.readBlocks().size();
        session.request( fizz );

        THEN( "it completes at once from the cache without reading a block" )
        {
            const auto state = session.state();
            REQUIRE( state.phase == Phase::Complete );
            REQUIRE( state.fromCache );
            REQUIRE( state.matchCount == 14_lcount );
            REQUIRE( session.matches() == fizzLines( 40 ) );
            REQUIRE( blockSource.readBlocks().size() == blocksBefore );
        }
    }
}

SCENARIO( "A Search whose block cannot be read fails", "[searchsession]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource( numberedLines( 40 ) );
    SearchSession session( blockSource, policies.search );

    GIVEN( "a block source that fails to read" )
    {
        blockSource.failReading( "the Log File could not be read" );

        WHEN( "a pattern is requested" )
        {
            session.request( RegularExpressionPattern( "fizz" ) );
            REQUIRE( waitUntilSettled( session ) );

            THEN( "the Session is Failed, describes why, and keeps no results" )
            {
                const auto state = session.state();
                REQUIRE( state.phase == Phase::Failed );
                REQUIRE( state.errorString.contains( "the Log File could not be read" ) );
                REQUIRE( state.matchCount == 0_lcount );
                REQUIRE( session.matches().cardinality() == 0 );
                REQUIRE( blockSource.attachedReaders() == 0 );
            }
        }
    }
}
