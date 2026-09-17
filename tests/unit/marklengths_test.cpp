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

#include "marklengths.h"

#include <catch2/catch.hpp>

// The lengths of the marked Log Lines are remembered when they are marked, so
// the longest Mark is known again when one is unmarked without reading any
// Log Line.

SCENARIO( "The longest Mark is known from the lengths remembered for the Marks", "[marks]" )
{
    GIVEN( "no Mark" )
    {
        MarkLengths lengths;

        THEN( "the longest Mark is of length 0" )
        {
            REQUIRE( lengths.longest() == 0_length );
        }

        THEN( "unmarking a Log Line that is not marked changes nothing" )
        {
            lengths.remove( 4_lnum );
            REQUIRE( lengths.longest() == 0_length );
        }
    }

    GIVEN( "Marks of lengths 10, 40, 25 and a second one of 40" )
    {
        MarkLengths lengths;
        lengths.add( 3_lnum, LineLength( 10 ) );
        lengths.add( 7_lnum, LineLength( 40 ) );
        lengths.add( 9_lnum, LineLength( 25 ) );
        lengths.add( 12_lnum, LineLength( 40 ) );

        THEN( "the longest Mark is of length 40" )
        {
            REQUIRE( lengths.longest() == LineLength( 40 ) );
        }

        WHEN( "one of the two longest is unmarked" )
        {
            lengths.remove( 7_lnum );

            THEN( "the other one is still the longest" )
            {
                REQUIRE( lengths.longest() == LineLength( 40 ) );
            }

            AND_WHEN( "the other one is unmarked too" )
            {
                lengths.remove( 12_lnum );

                THEN( "the longest left is the longest Mark" )
                {
                    REQUIRE( lengths.longest() == LineLength( 25 ) );
                }
            }
        }

        WHEN( "a shorter Mark is unmarked" )
        {
            lengths.remove( 3_lnum );
            lengths.remove( 9_lnum );

            THEN( "the longest Mark stays" )
            {
                REQUIRE( lengths.longest() == LineLength( 40 ) );
            }
        }

        WHEN( "a Log Line already marked is marked again with another length" )
        {
            lengths.add( 7_lnum, LineLength( 5 ) );
            lengths.remove( 12_lnum );

            THEN( "only its new length counts" )
            {
                REQUIRE( lengths.longest() == LineLength( 25 ) );
            }
        }

        WHEN( "a Log Line not marked is unmarked" )
        {
            lengths.remove( 8_lnum );

            THEN( "the longest Mark stays" )
            {
                REQUIRE( lengths.longest() == LineLength( 40 ) );
            }
        }

        WHEN( "every Mark is cleared" )
        {
            lengths.clear();

            THEN( "the longest Mark is of length 0" )
            {
                REQUIRE( lengths.longest() == 0_length );
                REQUIRE_FALSE( lengths.contains( 7_lnum ) );
            }
        }

        THEN( "it knows which Log Lines it remembers a length for" )
        {
            REQUIRE( lengths.contains( 9_lnum ) );
            REQUIRE_FALSE( lengths.contains( 8_lnum ) );
        }
    }
}
