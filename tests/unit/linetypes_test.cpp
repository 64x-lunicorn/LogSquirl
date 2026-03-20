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

#include "linetypes.h"

SCENARIO( "OffsetInFile basic operations", "[linetypes]" )
{
    GIVEN( "An offset created via literal" )
    {
        auto offset = 42_offset;

        THEN( "The underlying value is correct" )
        {
            REQUIRE( offset.get() == 42 );
        }
    }

    GIVEN( "Two offsets" )
    {
        auto a = 100_offset;
        auto b = 30_offset;

        THEN( "Addition works" )
        {
            REQUIRE( ( a + b ).get() == 130 );
        }

        THEN( "Subtraction works" )
        {
            REQUIRE( ( a - b ).get() == 70 );
        }

        THEN( "Relational comparison works" )
        {
            REQUIRE( b < a );
            REQUIRE( a > b );
            REQUIRE( a >= a );
            REQUIRE( a != b );
        }

        THEN( "Equality comparison works" )
        {
            REQUIRE( a == OffsetInFile( 100 ) );
        }
    }

    GIVEN( "An offset for increment" )
    {
        auto offset = 10_offset;

        WHEN( "Pre-incremented" )
        {
            ++offset;
            THEN( "Value increases by 1" )
            {
                REQUIRE( offset.get() == 11 );
            }
        }
    }
}

SCENARIO( "LinesCount basic operations", "[linetypes]" )
{
    GIVEN( "A LinesCount created via literal" )
    {
        auto count = 100_lcount;

        THEN( "The value is correct" )
        {
            REQUIRE( count.get() == 100 );
        }
    }

    GIVEN( "Two line counts" )
    {
        auto a = 50_lcount;
        auto b = 30_lcount;

        THEN( "Addition works" )
        {
            REQUIRE( ( a + b ).get() == 80 );
        }

        THEN( "Subtraction works" )
        {
            REQUIRE( ( a - b ).get() == 20 );
        }

        THEN( "Comparison works" )
        {
            REQUIRE( b < a );
            REQUIRE( a > b );
        }
    }

    GIVEN( "A LinesCount for decrement" )
    {
        auto count = LinesCount( 5 );

        WHEN( "Decremented" )
        {
            --count;
            THEN( "Value decreases by 1" )
            {
                REQUIRE( count.get() == 4 );
            }
        }
    }
}

SCENARIO( "LineNumber operations", "[linetypes]" )
{
    GIVEN( "A line number from literal" )
    {
        auto ln = 0_lnum;

        THEN( "The value is 0" )
        {
            REQUIRE( ln.get() == 0 );
        }
    }

    GIVEN( "LineNumber + LinesCount addition" )
    {
        auto number = 10_lnum;
        auto count = 5_lcount;

        THEN( "Adding count to number gives a LineNumber" )
        {
            auto result = number + count;
            REQUIRE( result.get() == 15 );
        }
    }

    GIVEN( "LineNumber - LinesCount subtraction" )
    {
        auto number = LineNumber( 10 );
        auto count = LinesCount( 3 );

        THEN( "Subtracting yields correct LineNumber" )
        {
            auto result = number - count;
            REQUIRE( result.get() == 7 );
        }
    }

    GIVEN( "LineNumber - LineNumber subtraction" )
    {
        auto a = LineNumber( 20 );
        auto b = LineNumber( 8 );

        THEN( "Difference is a LinesCount" )
        {
            LinesCount diff = a - b;
            REQUIRE( diff.get() == 12 );
        }
    }

    GIVEN( "LineNumber += LinesCount" )
    {
        auto number = LineNumber( 5 );
        auto count = LinesCount( 10 );

        WHEN( "+= is applied" )
        {
            number += count;
            THEN( "Number is updated" )
            {
                REQUIRE( number.get() == 15 );
            }
        }
    }
}

SCENARIO( "LineLength operations", "[linetypes]" )
{
    GIVEN( "A LineLength from literal" )
    {
        auto len = 80_length;

        THEN( "The value is correct" )
        {
            REQUIRE( len.get() == 80 );
        }
    }

    GIVEN( "Two LineLengths" )
    {
        auto a = 50_length;
        auto b = 20_length;

        THEN( "Addition works" )
        {
            REQUIRE( ( a + b ).get() == 70 );
        }

        THEN( "Subtraction works" )
        {
            REQUIRE( ( a - b ).get() == 30 );
        }
    }
}

SCENARIO( "LineColumn operations", "[linetypes]" )
{
    GIVEN( "A LineColumn from literal" )
    {
        auto col = 10_lcol;

        THEN( "The value is correct" )
        {
            REQUIRE( col.get() == 10 );
        }
    }

    GIVEN( "LineColumn + LineLength" )
    {
        auto col = 5_lcol;
        auto len = 10_length;

        THEN( "Result is a LineColumn" )
        {
            auto result = col + len;
            REQUIRE( result.get() == 15 );
        }
    }

    GIVEN( "LineColumn - LineLength" )
    {
        auto col = LineColumn( 20 );
        auto len = LineLength( 5 );

        THEN( "Result is a LineColumn" )
        {
            auto result = col - len;
            REQUIRE( result.get() == 15 );
        }
    }

    GIVEN( "LineColumn - LineColumn" )
    {
        auto a = LineColumn( 30 );
        auto b = LineColumn( 10 );

        THEN( "Difference is a LineLength" )
        {
            LineLength diff = a - b;
            REQUIRE( diff.get() == 20 );
        }
    }
}

SCENARIO( "Bounded arithmetic prevents overflow/underflow", "[linetypes]" )
{
    GIVEN( "LineNumber near maximum" )
    {
        auto almostMax = maxValue<LineNumber>();

        WHEN( "Adding a count" )
        {
            auto result = almostMax + 1_lcount;
            THEN( "It saturates at max value" )
            {
                REQUIRE( result == maxValue<LineNumber>() );
            }
        }
    }

    GIVEN( "LineNumber at zero" )
    {
        auto zero = 0_lnum;

        WHEN( "Subtracting more than the value" )
        {
            auto result = zero - 5_lcount;
            THEN( "It clamps to zero" )
            {
                REQUIRE( result.get() == 0 );
            }
        }
    }

    GIVEN( "LineColumn at zero" )
    {
        auto zero = 0_lcol;

        WHEN( "Subtracting a length" )
        {
            auto result = zero - 5_length;
            THEN( "It clamps to zero" )
            {
                REQUIRE( result.get() == 0 );
            }
        }
    }
}

SCENARIO( "LineNumber compared with LinesCount", "[linetypes]" )
{
    GIVEN( "A line number and a count" )
    {
        auto number = 5_lnum;
        auto count = 10_lcount;

        THEN( "LineNumber < LinesCount when value is smaller" )
        {
            REQUIRE( number < count );
        }

        THEN( "LineNumber >= LinesCount when value is equal or greater" )
        {
            auto bigNumber = LineNumber( 10 );
            REQUIRE( bigNumber >= count );
        }
    }
}
