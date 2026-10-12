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

// The Matches' Row Mapping (Matches Only, #798): Row n shows the n-th Match
// of the Search, in Log Line order, and a Sort Order is built over its Rows:
// Matches Only narrows, the Sort Order orders.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

#include "rowmapping.h"
#include "tablesortkeys.h"

SCENARIO( "The Matches' Rows show the Matches in Log Line order", "[rowmapping][matchesonly]" )
{
    GIVEN( "the Matches 1, 4 and 7 of a Search" )
    {
        const MatchRows rows( std::vector<uint32_t>{ 1, 4, 7 } );

        THEN( "there are three Rows while the Log File has every Match" )
        {
            REQUIRE( rows.rowCount( LinesCount( 8 ) ) == 3 );
            REQUIRE( rows.rowCount( LinesCount( 100 ) ) == 3 );
        }

        THEN( "a Log File cut short of a Match has fewer Rows" )
        {
            REQUIRE( rows.rowCount( LinesCount( 7 ) ) == 2 );
            REQUIRE( rows.rowCount( LinesCount( 1 ) ) == 0 );
        }

        THEN( "each Row shows its Match, and a Row beyond the last shows the last's" )
        {
            REQUIRE( rows.logLineAt( 0 ) == 1_lnum );
            REQUIRE( rows.logLineAt( 1 ) == 4_lnum );
            REQUIRE( rows.logLineAt( 2 ) == 7_lnum );
            REQUIRE( rows.logLineAt( 3 ) == 7_lnum );
            REQUIRE( rows.logLineAt( -1 ) == 1_lnum );
        }

        THEN( "a Match finds its Row and any other Log Line has none" )
        {
            REQUIRE( rows.rowOf( 1_lnum ) == 0 );
            REQUIRE( rows.rowOf( 4_lnum ) == 1 );
            REQUIRE( rows.rowOf( 7_lnum ) == 2 );
            REQUIRE_FALSE( rows.rowOf( 0_lnum ).has_value() );
            REQUIRE_FALSE( rows.rowOf( 5_lnum ).has_value() );
            REQUIRE_FALSE( rows.rowOf( 8_lnum ).has_value() );
        }
    }

    GIVEN( "no Matches" )
    {
        const MatchRows rows( std::vector<uint32_t>{} );

        THEN( "there is no Row" )
        {
            REQUIRE( rows.rowCount( LinesCount( 5 ) ) == 0 );
            REQUIRE_FALSE( rows.rowOf( 0_lnum ).has_value() );
            REQUIRE( rows.logLineAt( 0 ) == 0_lnum );
        }
    }
}

SCENARIO( "A Sort Order composes with the Matches' Rows", "[rowmapping][matchesonly][sortorder]" )
{
    GIVEN( "six Log Lines keyed by a number, and the Matches 0, 2, 3 and 5" )
    {
        // Log Line: 0 -> 30, 1 -> 10, 2 -> 20, 3 -> 5, 4 -> 1, 5 -> 15
        TableSortKeys keys( TableRowCells::SortKeyKind::Number );
        for ( const auto number : { 30.0, 10.0, 20.0, 5.0, 1.0, 15.0 } ) {
            TableRowCells::SortKey key;
            key.hasValue = true;
            key.number = number;
            keys.append( key );
        }
        const MatchRows matches( std::vector<uint32_t>{ 0, 2, 3, 5 } );
        const std::atomic<bool> cancel{ false };

        WHEN( "the Matches' Rows are sorted ascending" )
        {
            const SortedRows sorted(
                sortedLogLines( keys, Qt::AscendingOrder, matches.logLines(), cancel ) );

            THEN( "only the Matches are Rows, in the order of their keys: Matches Only narrows, "
                  "the Sort Order orders" )
            {
                REQUIRE( sorted.logLines() == std::vector<uint32_t>{ 3, 5, 2, 0 } );
                REQUIRE( sorted.rowCount( LinesCount( 6 ) ) == 4 );
                REQUIRE( sorted.rowOf( 3_lnum ) == 0 );
                REQUIRE( sorted.rowOf( 0_lnum ) == 3 );
                REQUIRE_FALSE( sorted.rowOf( 1_lnum ).has_value() );
                REQUIRE_FALSE( sorted.rowOf( 4_lnum ).has_value() );
            }
        }

        WHEN( "Matches found since are merged into the sorted Rows" )
        {
            const auto sorted
                = sortedLogLines( keys, Qt::AscendingOrder, matches.logLines(), cancel );
            TableRowCells::SortKey key;
            key.hasValue = true;
            key.number = 12.0;
            keys.append( key );
            const auto merged
                = mergeSortedLogLines( keys, Qt::AscendingOrder, sorted, { 6 }, cancel );

            THEN( "the new Match lands at its place" )
            {
                REQUIRE( merged.logLines == std::vector<uint32_t>{ 3, 6, 5, 2, 0 } );
                REQUIRE( merged.inserted == std::vector<RowRun>{ RowRun{ 1, 1 } } );
            }
        }
    }
}
