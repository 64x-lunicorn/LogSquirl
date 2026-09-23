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

#include "logfiltereddataworker.h"

#include <cstdint>
#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

namespace {

SearchResultArray linesOf( std::initializer_list<uint64_t> lines )
{
    SearchResultArray result;
    for ( const auto line : lines ) {
        result.add( line );
    }
    return result;
}

} // namespace

// The blocks of a parallel Search are combined in whatever order their
// matching finishes, not in the order of their Log Lines.
SCENARIO( "The Search Data count as searched only Log Lines with no gap before them", "[search]" )
{
    SearchData searchData;

    GIVEN( "a Search from line 10" )
    {
        searchData.searchFrom( 10_lnum );

        WHEN( "a block after the first is combined before the first" )
        {
            searchData.addAll( 5_length, linesOf( { 25 } ), 20_lnum, 10_lcount );

            THEN( "the Log Lines before the first block are all that count as searched" )
            {
                REQUIRE( searchData.getLastProcessedLine() == 10_lnum );
                REQUIRE( searchData.takeCurrentResults().processedLines == 10_lcount );
            }

            AND_WHEN( "the first block is combined too" )
            {
                searchData.addAll( 5_length, linesOf( { 12 } ), 10_lnum, 10_lcount );

                THEN( "both blocks count as searched, with the Matches of both" )
                {
                    const auto results = searchData.takeCurrentResults();
                    REQUIRE( results.processedLines == 30_lcount );
                    REQUIRE( results.newMatches == linesOf( { 12, 25 } ) );
                }
            }
        }
    }

    GIVEN( "a Search interrupted after combining a block beyond a gap" )
    {
        searchData.searchFrom( 0_lnum );
        searchData.addAll( 5_length, {}, 0_lnum, 10_lcount );
        searchData.addAll( 5_length, {}, 20_lnum, 10_lcount );

        WHEN( "it is searched again from the Log Lines counted as searched" )
        {
            searchData.searchFrom( 9_lnum );
            searchData.addAll( 5_length, {}, 9_lnum, 11_lcount );

            THEN( "the block beyond the gap no longer counts: it is searched again too" )
            {
                REQUIRE( searchData.getLastProcessedLine() == 20_lnum );
            }
        }
    }
}
