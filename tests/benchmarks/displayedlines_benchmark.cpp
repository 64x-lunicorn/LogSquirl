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

// Micro-benchmarks for the Displayed Lines (#292): a Search reporting a million
// Matches over ten million Log Lines in progress ticks, its completion, a
// continuation over appended Log Lines, and toggling a Mark -- with 3 Context
// Lines and everything shown.
//
// Builds on origin/master too: where the Displayed Lines cannot take the new
// Matches (before #292), each step tells them only that the Matches changed,
// which is what the Filtered View did then. See tests/benchmarks/README.md.

#include "displayedlines.h"

#include <cstdint>
#include <memory>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

namespace {

constexpr uint64_t LogLineCount = 10'000'000;
// One Log Line in ten is a Match.
constexpr uint64_t MatchEvery = 10;
constexpr uint64_t ProgressTicks = 100;
constexpr int ContextLinesCount = 3;
constexpr uint64_t MarkCount = 100;
// Log Lines appended before a continuation, and its progress ticks.
constexpr uint64_t AppendedLineCount = 100'000;
constexpr uint64_t ContinuationTicks = 10;

template <typename Displayed>
concept TakesNewMatches = requires( Displayed& displayed, const SearchResultArray& newMatches ) {
    displayed.matchesArrived( newMatches );
    displayed.searchCompleted( newMatches );
};

template <typename Displayed>
void matchesArrived( Displayed& displayed, const SearchResultArray& newMatches )
{
    if constexpr ( TakesNewMatches<Displayed> ) {
        displayed.matchesArrived( newMatches );
    }
    else {
        displayed.matchesArrived();
    }
}

template <typename Displayed>
void searchCompleted( Displayed& displayed, const SearchResultArray& newMatches )
{
    if constexpr ( TakesNewMatches<Displayed> ) {
        displayed.searchCompleted( newMatches );
    }
    else {
        displayed.searchCompleted();
    }
}

// The Matches among [first, end), split in consecutive batches the way a
// Search reports them.
std::vector<SearchResultArray> matchBatches( uint64_t first, uint64_t end, uint64_t nbBatches )
{
    std::vector<SearchResultArray> batches( nbBatches );
    const auto linesPerBatch = ( end - first + nbBatches - 1 ) / nbBatches;
    for ( auto line = first; line < end; ++line ) {
        if ( line % MatchEvery == 3 ) {
            batches[ ( line - first ) / linesPerBatch ].add( line );
        }
    }
    return batches;
}

// A Log File, the Matches of a Search over it and its Displayed Lines, with
// Marks spread over the Log File: some on Matches, the others close enough to
// one that their Context Lines overlap.
struct Search {
    Search()
        : displayed( matches, [ this ] { return LinesCount( nbLogLines ); }, ContextLinesCount )
    {
        displayed.setShown( DisplayedLines::LineType{ DisplayedLines::LineTypeFlags::Match }
                            | DisplayedLines::LineTypeFlags::Mark
                            | DisplayedLines::LineTypeFlags::Context );
        const auto markSpacing = LogLineCount / MarkCount;
        for ( uint64_t mark = 0; mark < MarkCount; ++mark ) {
            displayed.addMark( LineNumber( mark * markSpacing + ( mark % 2 == 0 ? 3 : 5 ) ),
                               0_length );
        }
    }

    void arrive( const SearchResultArray& newMatches )
    {
        matches |= newMatches;
        matchesArrived( displayed, newMatches );
    }

    void complete( const SearchResultArray& newMatches )
    {
        matches |= newMatches;
        searchCompleted( displayed, newMatches );
    }

    uint64_t nbLogLines = LogLineCount;
    SearchResultArray matches;
    DisplayedLines displayed;
};

const std::vector<SearchResultArray>& searchBatches()
{
    static const auto batches = matchBatches( 0, LogLineCount, ProgressTicks );
    return batches;
}

const std::vector<SearchResultArray>& appendedBatches()
{
    static const auto batches
        = matchBatches( LogLineCount, LogLineCount + AppendedLineCount, ContinuationTicks );
    return batches;
}

// One fresh Search per run Catch2 measures, each prepared by prepare.
template <typename Prepare>
std::vector<std::unique_ptr<Search>> searchesFor( int runs, Prepare prepare )
{
    std::vector<std::unique_ptr<Search>> searches;
    for ( int run = 0; run < runs; ++run ) {
        searches.push_back( std::make_unique<Search>() );
        prepare( *searches.back() );
    }
    return searches;
}

void runSearch( Search& search )
{
    const auto& batches = searchBatches();
    for ( size_t tick = 0; tick + 1 < batches.size(); ++tick ) {
        search.arrive( batches[ tick ] );
    }
    search.complete( batches.back() );
}

} // namespace

TEST_CASE( "Displayed Lines of a Search with a million Matches", "[displayedlines-benchmark]" )
{
    const auto& batches = searchBatches();
    const auto& appended = appendedBatches();

    BENCHMARK_ADVANCED( "progress ticks: 100 batches of 10,000 Matches" )(
        Catch::Benchmark::Chronometer meter )
    {
        auto searches = searchesFor( meter.runs(), []( Search& ) {} );
        meter.measure( [ & ]( int run ) {
            auto& search = *searches[ static_cast<size_t>( run ) ];
            for ( size_t tick = 0; tick + 1 < batches.size(); ++tick ) {
                search.arrive( batches[ tick ] );
            }
            return search.displayed.count();
        } );
    };

    BENCHMARK_ADVANCED( "completion after the progress ticks" )(
        Catch::Benchmark::Chronometer meter )
    {
        auto searches = searchesFor( meter.runs(), [ & ]( Search& search ) {
            for ( size_t tick = 0; tick + 1 < batches.size(); ++tick ) {
                search.arrive( batches[ tick ] );
            }
        } );
        meter.measure( [ & ]( int run ) {
            auto& search = *searches[ static_cast<size_t>( run ) ];
            search.complete( batches.back() );
            return search.displayed.count();
        } );
    };

    BENCHMARK_ADVANCED( "continuation over 100,000 appended Log Lines" )(
        Catch::Benchmark::Chronometer meter )
    {
        auto searches = searchesFor( meter.runs(), []( Search& search ) {
            runSearch( search );
            search.nbLogLines += AppendedLineCount;
        } );
        meter.measure( [ & ]( int run ) {
            auto& search = *searches[ static_cast<size_t>( run ) ];
            for ( size_t tick = 0; tick + 1 < appended.size(); ++tick ) {
                search.arrive( appended[ tick ] );
            }
            search.complete( appended.back() );
            return search.displayed.count();
        } );
    };

    BENCHMARK_ADVANCED( "toggling a Mark on and off next to a Match" )(
        Catch::Benchmark::Chronometer meter )
    {
        Search search;
        runSearch( search );
        // Two Log Lines after a Match: their Context Lines overlap.
        const auto line = LineNumber( LogLineCount / 2 + 5 );
        meter.measure( [ & ] {
            search.displayed.addMark( line, 0_length );
            search.displayed.removeMark( line );
            return search.displayed.count();
        } );
    };
}
