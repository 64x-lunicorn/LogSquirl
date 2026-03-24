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

// ---------------------------------------------------------------------------
// untabify() tests
// ---------------------------------------------------------------------------

SCENARIO( "untabify expands tabs to spaces correctly", "[linetypes][untabify]" )
{
    GIVEN( "A string with no tabs" )
    {
        QString input = "hello world";

        THEN( "It is returned unchanged" )
        {
            auto result = untabify( QString( input ) );
            REQUIRE( result == "hello world" );
        }
    }

    GIVEN( "An empty string" )
    {
        THEN( "It returns empty" )
        {
            auto result = untabify( QString( "" ) );
            REQUIRE( result.isEmpty() );
        }
    }

    GIVEN( "A single tab at position 0" )
    {
        THEN( "It expands to 8 spaces (TabStop)" )
        {
            auto result = untabify( QString( "\t" ) );
            REQUIRE( result == QString( 8, QChar::Space ) );
            REQUIRE( result.size() == TabStop );
        }
    }

    GIVEN( "A tab after 3 characters" )
    {
        THEN( "It expands to 5 spaces (next tab stop at 8)" )
        {
            auto result = untabify( QString( "abc\t" ) );
            REQUIRE( result == "abc     " );
            REQUIRE( result.size() == 8 );
        }
    }

    GIVEN( "A tab at an exact tab stop boundary" )
    {
        // 8 chars + tab → tab at column 8 → next stop is 16, so 8 spaces
        THEN( "It expands to a full TabStop width" )
        {
            auto result = untabify( QString( "12345678\t" ) );
            REQUIRE( result == "12345678        " );
            REQUIRE( result.size() == 16 );
        }
    }

    GIVEN( "Multiple consecutive tabs" )
    {
        THEN( "Each tab respects its own column position" )
        {
            auto result = untabify( QString( "\t\t" ) );
            REQUIRE( result.size() == 16 );
            REQUIRE( result == QString( 16, QChar::Space ) );
        }
    }

    GIVEN( "Mixed text and tabs" )
    {
        THEN( "Tab stops align correctly" )
        {
            // "ab\tcd\tefgh"
            // 'a' at 0, 'b' at 1, tab at 2 → 6 spaces → "ab      "
            // 'c' at 8, 'd' at 9, tab at 10 → 6 spaces → "cd      "
            // 'e' at 16, 'f' at 17, 'g' at 18, 'h' at 19
            auto result = untabify( QString( "ab\tcd\tefgh" ) );
            REQUIRE( result == "ab      cd      efgh" );
            REQUIRE( result.size() == 20 );
        }
    }

    GIVEN( "A string with null characters" )
    {
        THEN( "Nulls are replaced with spaces" )
        {
            QString input;
            input.append( 'a' );
            input.append( QChar::Null );
            input.append( 'b' );
            auto result = untabify( std::move( input ) );
            REQUIRE( result == "a b" );
        }
    }

    GIVEN( "An initialPosition offset" )
    {
        THEN( "Tab stops align relative to the initial column" )
        {
            // initialPosition=3, tab at pos 0 → column 3 → spaces = 8 - (3 % 8) = 5
            auto result = untabify( QString( "\t" ), LineColumn( 3 ) );
            REQUIRE( result.size() == 5 );
        }
    }
}

SCENARIO( "untabify handles very long lines with many tabs efficiently", "[linetypes][untabify]" )
{
    GIVEN( "A line with 10000 tabs" )
    {
        QString input( 10'000, QChar::Tabulation );

        THEN( "It completes and produces the correct length" )
        {
            auto result = untabify( std::move( input ) );
            REQUIRE( result.size() == 10'000 * TabStop );
        }
    }

    GIVEN( "A line with tabs that would exceed MaxExpandedLineLength" )
    {
        // Each tab at position 0 of its group expands to 8 spaces.
        // Need > MaxExpandedLineLength / 8 tabs to trigger truncation.
        const int numTabs = ( MaxExpandedLineLength / TabStop ) + 1000;
        QString input( numTabs, QChar::Tabulation );

        THEN( "It is capped at MaxExpandedLineLength" )
        {
            auto result = untabify( std::move( input ) );
            REQUIRE( result.size() == MaxExpandedLineLength );
        }
    }
}

// ---------------------------------------------------------------------------
// getUntabifiedLength() tests
// ---------------------------------------------------------------------------

SCENARIO( "getUntabifiedLength calculates correct expanded length", "[linetypes][untabify]" )
{
    GIVEN( "A string with no tabs" )
    {
        std::string input = "hello";

        THEN( "Length equals the string size" )
        {
            REQUIRE( getUntabifiedLength( input ).get() == 5 );
        }
    }

    GIVEN( "An empty string" )
    {
        THEN( "Length is zero" )
        {
            REQUIRE( getUntabifiedLength( std::string( "" ) ).get() == 0 );
        }
    }

    GIVEN( "A single tab at position 0" )
    {
        THEN( "Length equals TabStop" )
        {
            REQUIRE( getUntabifiedLength( std::string( "\t" ) ).get() == TabStop );
        }
    }

    GIVEN( "Tab after 3 characters" )
    {
        THEN( "Length is 8 (3 chars + 5 spaces)" )
        {
            REQUIRE( getUntabifiedLength( std::string( "abc\t" ) ).get() == 8 );
        }
    }

    GIVEN( "Two consecutive tabs" )
    {
        THEN( "Length is 16" )
        {
            REQUIRE( getUntabifiedLength( std::string( "\t\t" ) ).get() == 16 );
        }
    }

    GIVEN( "Mixed text and tabs" )
    {
        THEN( "Length matches untabify result" )
        {
            std::string input = "ab\tcd\tefgh";
            auto length = getUntabifiedLength( input ).get();
            REQUIRE( length == 20 );
        }
    }
}

SCENARIO( "getUntabifiedLength for QString matches untabify output", "[linetypes][untabify]" )
{
    GIVEN( "Various strings with tabs" )
    {
        logsquirl::vector<QString> inputs = {
            QString( "hello" ),
            QString( "\t" ),
            QString( "abc\t" ),
            QString( "\t\t" ),
            QString( "ab\tcd\tefgh" ),
            QString( "12345678\t" ),
        };

        THEN( "getUntabifiedLength equals untabify().size() for each input" )
        {
            for ( const auto& input : inputs ) {
                auto expanded = untabify( QString( input ) );
                auto length = getUntabifiedLength( input );
                REQUIRE( length.get() == expanded.size() );
            }
        }
    }
}
