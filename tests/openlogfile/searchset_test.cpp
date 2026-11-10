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

#include "logfiltereddata.h"
#include "searchset.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// The Search Set (#941): every Search of an Open Log File, the current one and
// the kept ones, each waiting for the first load, held by the Session, or
// running. Its Searches here are fakes that only remember what they were
// asked: no Log File, thread or event loop is involved.

namespace {

// A Search that remembers what it was asked, and is in the phase a Search
// Session would be in after it.
struct FakeSearch {
    struct Request {
        QString pattern;
        LineNumber start;
        LineNumber end;

        bool operator==( const Request& ) const = default;
    };

    void request( const RegularExpressionPattern& pattern, LineNumber start, LineNumber end )
    {
        requests.push_back( Request{ pattern.pattern, start, end } );
        state.pattern = pattern;
        state.startLine = start;
        state.endLine = end;
        state.phase = SearchSessionPhase::Running;
    }

    void dropResults()
    {
        ++resultsDropped;
        state = {};
    }

    void stop()
    {
        ++stops;
        if ( state.phase == SearchSessionPhase::Running ) {
            state.phase = SearchSessionPhase::Interrupted;
        }
    }

    void setSearchPolicy( const SearchPolicy& searchPolicy )
    {
        policy = searchPolicy;
    }

    SearchSessionState searchState() const
    {
        return state;
    }

    std::vector<Request> requests;
    int resultsDropped = 0;
    int stops = 0;
    std::optional<SearchPolicy> policy;
    SearchSessionState state;
};

// The two adapters of the Search Port.
static_assert( SearchPort<FakeSearch> );
static_assert( SearchPort<LogFilteredData> );

using Set = SearchSet<FakeSearch>;
using State = Set::State;
using Request = FakeSearch::Request;

// The Search Limits the Open Log File hands on, as a load leaves them.
const auto Limits = Set::SearchLimits{ 0_lnum, 30_lnum };

RegularExpressionPattern pattern( const char* text )
{
    return RegularExpressionPattern( QString::fromLatin1( text ) );
}

Request over( const char* text, Set::SearchLimits limits = Limits )
{
    return Request{ QString::fromLatin1( text ), limits.start, limits.end };
}

// A Search Set as an Open Log File holds it, with what its hooks were told.
struct Searches {
    Searches()
        : set( Set::Hooks{
              .newSearch = [ this ] { return made.emplace_back( std::make_shared<FakeSearch>() ); },
              .preparePattern =
                  [ this ]( RegularExpressionPattern requested ) {
                      ++prepared;
                      return requested;
                  },
              .currentRequested = [ this ] { currentRequests.push_back( set.current() ); },
              .heldNoLonger = [ this ] { ++heldNoLonger; },
          } )
    {
    }

    Searches( const Searches& ) = delete;
    Searches& operator=( const Searches& ) = delete;

    // Every Search the set made, in the order made.
    std::vector<std::shared_ptr<FakeSearch>> made;
    int prepared = 0;
    // The current Search each time it was requested as the user requests it.
    std::vector<std::shared_ptr<FakeSearch>> currentRequests;
    int heldNoLonger = 0;
    Set set;
};

} // namespace

SCENARIO( "A Search requested before the first load waits for it", "[openlogfile][searchset]" )
{
    Searches searches;
    const auto current = searches.set.current();

    WHEN( "the current Search is requested" )
    {
        const auto waits = searches.set.requestCurrent( pattern( "fizz" ), Limits );

        THEN( "it waits, and nothing runs over the Log Lines not loaded yet" )
        {
            REQUIRE( waits );
            REQUIRE( searches.set.state( current ) == State::Waiting );
            REQUIRE( searches.set.currentPattern().pattern == "fizz" );
            REQUIRE( current->requests.empty() );
            REQUIRE( searches.currentRequests.empty() );
            REQUIRE_FALSE( searches.set.hasFirstLoadFinished() );
        }

        THEN( "it is not held, as nothing held the waiting Searches" )
        {
            REQUIRE_FALSE( searches.set.isHeld( current ) );
            REQUIRE_FALSE( searches.set.holdsAny() );
        }

        AND_WHEN( "the first load has finished" )
        {
            searches.set.loadFinished( true, Limits );

            THEN( "it runs over the Search Limits that load leaves, as the user requests it" )
            {
                REQUIRE( searches.set.hasFirstLoadFinished() );
                REQUIRE( searches.set.state( current ) == State::Running );
                REQUIRE( current->requests == std::vector{ over( "fizz" ) } );
                REQUIRE( searches.currentRequests == std::vector{ current } );
                REQUIRE( searches.prepared == 1 );
            }

            AND_WHEN( "another load finishes" )
            {
                searches.set.loadFinished( true, Limits );

                THEN( "it does not run again" )
                {
                    REQUIRE( current->requests.size() == 1 );
                }
            }
        }

        AND_WHEN( "the first load did not succeed" )
        {
            searches.set.loadFinished( false, Limits );

            THEN( "it is dropped: idle, it never ran" )
            {
                REQUIRE( searches.set.hasFirstLoadFinished() );
                REQUIRE( searches.set.state( current ) == State::Idle );
                REQUIRE( current->requests.empty() );
            }

            AND_WHEN( "the next load succeeds" )
            {
                searches.set.loadFinished( true, Limits );

                THEN( "it does not run with it" )
                {
                    REQUIRE( current->requests.empty() );
                }
            }
        }

        AND_WHEN( "it is stopped before the first load" )
        {
            searches.set.stopCurrent();
            searches.set.loadFinished( true, Limits );

            THEN( "it does not run" )
            {
                REQUIRE( current->stops == 1 );
                REQUIRE( current->requests.empty() );
                REQUIRE( searches.set.state( current ) == State::Running );
            }
        }

        AND_WHEN( "it is cleared, or the Log File reloaded, before the first load" )
        {
            searches.set.clearCurrent();
            searches.set.loadFinished( true, Limits );

            THEN( "it drops its results, goes idle and does not run" )
            {
                REQUIRE( current->resultsDropped == 1 );
                REQUIRE( current->requests.empty() );
                REQUIRE( searches.set.state( current ) == State::Idle );
            }
        }
    }

    WHEN( "a kept Search and the current one are requested" )
    {
        const auto kept = current;
        const auto another = searches.set.startAnother();
        searches.set.requestKept( kept, pattern( "fizz" ), Limits );
        searches.set.requestKept( another, pattern( "buzz" ), Limits );

        THEN( "both wait" )
        {
            REQUIRE( searches.set.current() == another );
            REQUIRE( searches.set.state( kept ) == State::Waiting );
            REQUIRE( searches.set.state( another ) == State::Waiting );
            REQUIRE( kept->requests.empty() );
            REQUIRE( another->requests.empty() );
        }

        AND_WHEN( "the first load has finished" )
        {
            searches.set.loadFinished( true, Limits );

            THEN( "each one runs with its pattern, the current one as the user requests it" )
            {
                REQUIRE( kept->requests == std::vector{ over( "fizz" ) } );
                REQUIRE( another->requests == std::vector{ over( "buzz" ) } );
                REQUIRE( searches.currentRequests == std::vector{ another } );
                REQUIRE( searches.set.current() == another );
            }
        }

        AND_WHEN( "the first load did not succeed" )
        {
            searches.set.loadFinished( false, Limits );

            THEN( "neither runs" )
            {
                REQUIRE( kept->requests.empty() );
                REQUIRE( another->requests.empty() );
                REQUIRE( searches.set.state( kept ) == State::Idle );
                REQUIRE( searches.set.state( another ) == State::Idle );
            }
        }
    }
}

SCENARIO( "Whether a Search waits depends on the first load alone", "[openlogfile][searchset]" )
{
    struct Row {
        const char* sequence;
        std::vector<std::optional<bool>> loads; // a load that succeeded or not; none: a reload
        bool waits;
    };
    // clang-format off
    const std::vector<Row> rows = {
        { "before any load", {}, true },
        { "while the first load is reloaded", { std::nullopt }, true },
        { "after the first load", { true }, false },
        { "after a first load that failed", { false }, false },
        { "while a reload after the first load is running", { true, std::nullopt }, false },
    };
    // clang-format on

    for ( const auto& row : rows ) {
        INFO( row.sequence );
        Searches searches;
        for ( const auto& load : row.loads ) {
            if ( load ) {
                searches.set.loadFinished( *load, Limits );
            }
            else {
                // A reload drops the current Search.
                searches.set.clearCurrent();
            }
        }
        CHECK( searches.set.requestCurrent( pattern( "fizz" ), Limits ) == row.waits );
        CHECK( searches.set.state( searches.set.current() )
               == ( row.waits ? State::Waiting : State::Running ) );
        CHECK( searches.set.current()->requests.empty() == row.waits );
    }
}

SCENARIO( "The Searches waiting for the first load are held beyond it until released",
          "[openlogfile][searchset][session]" )
{
    Searches searches;
    const auto fizz = searches.set.current();
    const auto buzz = searches.set.startAnother();
    const auto line = searches.set.startAnother();
    const auto idle = searches.set.startAnother();
    searches.set.makeCurrent( buzz );
    searches.set.requestKept( fizz, pattern( "fizz" ), Limits );
    searches.set.requestKept( line, pattern( "line" ), Limits );
    searches.set.requestCurrent( pattern( "buzz" ), Limits );

    searches.set.holdWaiting();

    THEN( "every waiting one is held, the one never requested is not" )
    {
        REQUIRE( searches.set.holdsAny() );
        REQUIRE( searches.set.isHeld( fizz ) );
        REQUIRE( searches.set.isHeld( buzz ) );
        REQUIRE( searches.set.isHeld( line ) );
        REQUIRE_FALSE( searches.set.isHeld( idle ) );
        REQUIRE( searches.set.state( buzz ) == State::HeldBeforeLoad );
    }

    WHEN( "the first load has finished" )
    {
        searches.set.loadFinished( true, Limits );

        THEN( "none runs, and each one is still held" )
        {
            REQUIRE( fizz->requests.empty() );
            REQUIRE( buzz->requests.empty() );
            REQUIRE( line->requests.empty() );
            REQUIRE( searches.set.state( fizz ) == State::HeldAfterLoad );
            REQUIRE( searches.set.state( buzz ) == State::HeldAfterLoad );
            REQUIRE( searches.set.holdsAny() );
            REQUIRE( searches.heldNoLonger == 0 );
        }

        AND_WHEN( "they are released" )
        {
            const auto releasedOver = Set::SearchLimits{ 5_lnum, 20_lnum };
            searches.set.releaseHeld( releasedOver );

            THEN( "each one runs with its pattern over the Search Limits, which is told once" )
            {
                REQUIRE( fizz->requests == std::vector{ over( "fizz", releasedOver ) } );
                REQUIRE( buzz->requests == std::vector{ over( "buzz", releasedOver ) } );
                REQUIRE( line->requests == std::vector{ over( "line", releasedOver ) } );
                REQUIRE( idle->requests.empty() );
                REQUIRE( searches.currentRequests == std::vector{ buzz } );
                REQUIRE( searches.heldNoLonger == 1 );
                REQUIRE_FALSE( searches.set.holdsAny() );
                REQUIRE( searches.set.state( fizz ) == State::Running );
            }
        }

        AND_WHEN( "the user requests the current Search and a kept one again" )
        {
            searches.set.requestCurrent( pattern( "buzz 2" ), Limits );
            searches.set.requestKept( line, pattern( "line 2" ), Limits );

            THEN( "both run at once, each held no longer, which is told for each" )
            {
                REQUIRE( buzz->requests == std::vector{ over( "buzz 2" ) } );
                REQUIRE( line->requests == std::vector{ over( "line 2" ) } );
                REQUIRE( searches.heldNoLonger == 2 );
                REQUIRE_FALSE( searches.set.isHeld( buzz ) );
                REQUIRE_FALSE( searches.set.isHeld( line ) );
                REQUIRE( searches.set.isHeld( fizz ) );
                REQUIRE( fizz->requests.empty() );
            }
        }

        AND_WHEN( "a held kept Search is made current, and they are released" )
        {
            searches.set.makeCurrent( fizz );
            searches.set.releaseHeld( Limits );

            THEN( "it runs as the current Search, the one current before runs kept" )
            {
                REQUIRE( searches.set.current() == fizz );
                REQUIRE( searches.currentRequests == std::vector{ fizz } );
                REQUIRE( fizz->requests == std::vector{ over( "fizz" ) } );
                REQUIRE( buzz->requests == std::vector{ over( "buzz" ) } );
            }
        }

        AND_WHEN( "the user clears the current Search, and they are released" )
        {
            searches.set.clearCurrent();
            searches.set.releaseHeld( Limits );

            THEN( "it is held no longer and does not run when the others are released" )
            {
                REQUIRE( searches.heldNoLonger == 2 );
                REQUIRE( buzz->requests.empty() );
                REQUIRE( fizz->requests == std::vector{ over( "fizz" ) } );
            }
        }

        AND_WHEN( "the user stops the current Search, and they are released" )
        {
            searches.set.stopCurrent();
            searches.set.releaseHeld( Limits );

            THEN( "it is held no longer and does not run when the others are released" )
            {
                REQUIRE( searches.heldNoLonger == 2 );
                REQUIRE( buzz->requests.empty() );
                REQUIRE( line->requests == std::vector{ over( "line" ) } );
            }
        }

        AND_WHEN( "the Log Format changes under the held current Search" )
        {
            searches.set.requestCurrentAgain( Limits );

            THEN( "it is not requested again" )
            {
                REQUIRE( buzz->requests.empty() );
            }
        }
    }

    WHEN( "the first load did not succeed" )
    {
        searches.set.loadFinished( false, Limits );

        THEN( "what is held is dropped with what waits, which is told" )
        {
            REQUIRE( searches.heldNoLonger == 1 );
            REQUIRE_FALSE( searches.set.holdsAny() );
            REQUIRE( searches.set.state( fizz ) == State::Idle );
            REQUIRE( searches.set.state( buzz ) == State::Idle );
            REQUIRE( fizz->requests.empty() );
            REQUIRE( buzz->requests.empty() );
        }
    }

    WHEN( "they are released before the first load has finished" )
    {
        searches.set.releaseHeld( Limits );

        THEN( "they wait for it as though never held, which is told" )
        {
            REQUIRE( searches.heldNoLonger == 1 );
            REQUIRE_FALSE( searches.set.holdsAny() );
            REQUIRE( searches.set.state( fizz ) == State::Waiting );
            REQUIRE( fizz->requests.empty() );
        }

        AND_WHEN( "the first load has finished" )
        {
            searches.set.loadFinished( true, Limits );

            THEN( "they run" )
            {
                REQUIRE( fizz->requests == std::vector{ over( "fizz" ) } );
                REQUIRE( buzz->requests == std::vector{ over( "buzz" ) } );
            }
        }
    }
}

SCENARIO( "A request by the user is never held", "[openlogfile][searchset][session]" )
{
    Searches searches;
    const auto kept = searches.set.current();
    const auto current = searches.set.startAnother();
    searches.set.requestKept( kept, pattern( "fizz" ), Limits );

    WHEN( "the waiting Searches are held and the user then requests the current one" )
    {
        searches.set.holdWaiting();
        searches.set.requestCurrent( pattern( "buzz" ), Limits );

        THEN( "the user's Search waits, not held; the kept one is held" )
        {
            REQUIRE( searches.set.state( current ) == State::Waiting );
            REQUIRE_FALSE( searches.set.isHeld( current ) );
            REQUIRE( searches.set.isHeld( kept ) );
        }

        AND_WHEN( "the first load has finished" )
        {
            searches.set.loadFinished( true, Limits );

            THEN( "the user's Search runs, the held one waits" )
            {
                REQUIRE( current->requests == std::vector{ over( "buzz" ) } );
                REQUIRE( kept->requests.empty() );
                REQUIRE( searches.set.isHeld( kept ) );
            }
        }
    }

    WHEN( "a Search is started after the waiting one was held" )
    {
        searches.set.makeCurrent( kept );
        searches.set.holdWaiting();
        const auto another = searches.set.startAnother();
        searches.set.loadFinished( true, Limits );

        THEN( "the held one waits on, kept, and runs once released" )
        {
            REQUIRE( searches.set.isHeld( kept ) );
            REQUIRE( kept->requests.empty() );
            searches.set.releaseHeld( Limits );
            REQUIRE( kept->requests == std::vector{ over( "fizz" ) } );
            REQUIRE( searches.set.current() == another );
            REQUIRE( another->requests.empty() );
            REQUIRE( searches.currentRequests.empty() );
        }
    }

    WHEN( "the waiting Searches are held after the first load" )
    {
        searches.set.loadFinished( true, Limits );
        searches.set.holdWaiting();
        searches.set.requestCurrent( pattern( "buzz" ), Limits );

        THEN( "nothing is held, and the Search runs at once" )
        {
            REQUIRE_FALSE( searches.set.holdsAny() );
            REQUIRE( current->requests == std::vector{ over( "buzz" ) } );
            REQUIRE( kept->requests == std::vector{ over( "fizz" ) } );
        }
    }
}

SCENARIO( "A Search waiting for the first load can be made current", "[openlogfile][searchset]" )
{
    Searches searches;
    const auto first = searches.set.current();
    searches.set.requestCurrent( pattern( "fizz" ), Limits );
    const auto another = searches.set.startAnother();

    THEN( "the one current before waits on, kept, stopped" )
    {
        REQUIRE( searches.set.state( first ) == State::Waiting );
        REQUIRE( first->stops == 1 );
        REQUIRE( searches.set.state( another ) == State::Idle );
    }

    WHEN( "the first load has finished" )
    {
        searches.set.loadFinished( true, Limits );

        THEN( "it ran all the same, kept, and the new one stays idle" )
        {
            REQUIRE( first->requests == std::vector{ over( "fizz" ) } );
            REQUIRE( searches.currentRequests.empty() );
            REQUIRE( another->requests.empty() );
        }
    }

    WHEN( "it is made current again before the first load" )
    {
        searches.set.makeCurrent( first );
        searches.set.loadFinished( true, Limits );

        THEN( "it runs as the current Search" )
        {
            REQUIRE( searches.set.current() == first );
            REQUIRE( first->requests == std::vector{ over( "fizz" ) } );
            REQUIRE( searches.currentRequests == std::vector{ first } );
        }
    }

    WHEN( "the current Search is made current once more before the first load" )
    {
        searches.set.makeCurrent( first );
        searches.set.makeCurrent( first );
        searches.set.loadFinished( true, Limits );

        THEN( "it is stopped and waits no longer" )
        {
            REQUIRE( first->requests.empty() );
        }
    }
}

SCENARIO( "A kept Search dropped before the first load is not held on to",
          "[openlogfile][searchset]" )
{
    Searches searches;
    std::weak_ptr<FakeSearch> dropped;
    {
        const auto kept = searches.set.current();
        searches.set.startAnother();
        searches.set.requestKept( kept, pattern( "fizz" ), Limits );
        searches.set.holdWaiting();
        dropped = kept;
    }
    searches.made.clear();

    THEN( "it is gone, and nothing is held" )
    {
        REQUIRE( dropped.expired() );
        REQUIRE_FALSE( searches.set.holdsAny() );
    }

    WHEN( "the first load has finished and what was held is released" )
    {
        searches.set.loadFinished( true, Limits );
        searches.set.releaseHeld( Limits );

        THEN( "the current Search did not run" )
        {
            REQUIRE( searches.set.current()->requests.empty() );
        }
    }
}

SCENARIO( "A kept Search dropped while it waits for the first load is not waited on",
          "[openlogfile][searchset]" )
{
    Searches searches;
    std::weak_ptr<FakeSearch> dropped;
    {
        const auto kept = searches.set.current();
        searches.set.startAnother();
        searches.set.requestKept( kept, pattern( "fizz" ), Limits );
        dropped = kept;
    }
    searches.made.clear();

    THEN( "it is gone" )
    {
        REQUIRE( dropped.expired() );
    }

    WHEN( "the first load has finished" )
    {
        searches.set.loadFinished( true, Limits );

        THEN( "nothing holds it, and the current Search did not run" )
        {
            REQUIRE( dropped.expired() );
            REQUIRE( searches.set.current()->requests.empty() );
            REQUIRE( searches.currentRequests.empty() );
        }
    }
}

SCENARIO( "The current Search's results are dropped on a truncation when it was requested",
          "[openlogfile][searchset]" )
{
    struct Row {
        const char* sequence;
        std::vector<const char*> events;
        bool dropped;
    };
    // clang-format off
    const std::vector<Row> rows = {
        { "no Search requested", { "loaded" }, false },
        { "under a Search", { "loaded", "requested" }, true },
        { "under a Search waiting for the first load", { "requested" }, true },
        { "after the Search was cleared", { "loaded", "requested", "cleared" }, false },
        { "after the Search was stopped", { "loaded", "requested", "stopped" }, true },
        { "after a failed first load dropped the waiting Search",
          { "requested", "failed" }, false },
    };
    // clang-format on

    for ( const auto& row : rows ) {
        INFO( row.sequence );
        Searches searches;
        for ( const std::string_view event : row.events ) {
            if ( event == "loaded" ) {
                searches.set.loadFinished( true, Limits );
            }
            else if ( event == "failed" ) {
                searches.set.loadFinished( false, Limits );
            }
            else if ( event == "requested" ) {
                searches.set.requestCurrent( pattern( "fizz" ), Limits );
            }
            else if ( event == "cleared" ) {
                searches.set.clearCurrent();
            }
            else if ( event == "stopped" ) {
                searches.set.stopCurrent();
            }
        }
        const auto droppedBefore = searches.set.current()->resultsDropped;
        CHECK( searches.set.dropTruncatedResults() == row.dropped );
        CHECK( searches.set.current()->resultsDropped - droppedBefore == ( row.dropped ? 1 : 0 ) );
    }
}

SCENARIO( "The current Search starts again with its own pattern", "[openlogfile][searchset]" )
{
    Searches searches;
    searches.set.loadFinished( true, Limits );
    const auto first = searches.set.current();
    searches.set.requestCurrent( pattern( "fizz" ), Limits );
    const auto second = searches.set.startAnother();
    searches.set.requestCurrent( pattern( "buzz" ), Limits );
    searches.set.makeCurrent( first );

    WHEN( "it starts again over a truncated Log File" )
    {
        const auto truncatedTo = Set::SearchLimits{ 0_lnum, 10_lnum };
        searches.set.restartCurrent( truncatedTo );

        THEN( "its results are dropped and it runs with its pattern" )
        {
            REQUIRE( first->resultsDropped == 1 );
            REQUIRE( first->requests.back() == over( "fizz", truncatedTo ) );
            REQUIRE( second->requests == std::vector{ over( "buzz" ) } );
        }
    }

    WHEN( "a Log Format is recognized anew" )
    {
        searches.set.requestCurrentAgain( Limits );

        THEN( "it is requested again with its pattern" )
        {
            REQUIRE( first->requests == std::vector{ over( "fizz" ), over( "fizz" ) } );
        }
    }

    WHEN( "a Log Format is recognized anew under an idle Search" )
    {
        searches.set.clearCurrent();
        searches.set.requestCurrentAgain( Limits );

        THEN( "it is not requested" )
        {
            REQUIRE( first->requests.size() == 1 );
        }
    }
}

SCENARIO( "The Search Policy reaches every Search of the Search Set",
          "[openlogfile][searchset][policies]" )
{
    Searches searches;
    const auto first = searches.set.current();
    const auto second = searches.set.startAnother();
    const auto third = searches.set.startAnother();
    searches.set.makeCurrent( second );

    std::weak_ptr<FakeSearch> dropped;
    {
        const auto kept = searches.set.startAnother();
        dropped = kept;
        searches.set.makeCurrent( second );
        searches.made.pop_back();
    }
    REQUIRE( dropped.expired() );

    SearchPolicy policy;
    policy.contextLinesCount = 7;
    searches.set.setSearchPolicy( policy );

    THEN( "the current one and the kept ones still held run under it" )
    {
        REQUIRE( first->policy == policy );
        REQUIRE( second->policy == policy );
        REQUIRE( third->policy == policy );
    }
}
