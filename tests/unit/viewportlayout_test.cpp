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

// The viewport layout is a value built from plain integers, so every test here
// uses literal inputs and needs no QApplication, no widget and no font.

#include <catch2/catch.hpp>

#include <limits>

#include "viewportlayout.h"

namespace {

ViewportLayoutInput fixedWidthInput()
{
    ViewportLayoutInput input;
    input.charWidthPx = 10;
    input.charHeightPx = 20;
    input.viewportWidthPx = 500;
    input.viewportHeightPx = 400;
    input.firstLine = 0_lnum;
    input.firstColumn = 0_lcol;
    input.lineNumbersVisible = false;
    input.largestDisplayLineNumber = 0;
    input.textWrap = false;
    input.drawingTopOffsetPx = 0;
    return input;
}

// One row per Log Line, as an unwrapped view produces.
ViewportRows unwrappedRows( LineNumber firstLine, size_t count, LineLength lineLength )
{
    ViewportRows rows;
    for ( size_t i = 0; i < count; ++i ) {
        rows.push_back( ViewportRow{ firstLine + LinesCount( i ), 0, 0_lcol, lineLength,
                                     lineLength } );
    }
    return rows;
}

} // namespace

SCENARIO( "Viewport layout margin arithmetic", "[viewportlayout]" )
{
    GIVEN( "A layout without line numbers" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "the bullet zone is the bullet area plus its separator" )
        {
            REQUIRE( layout.bulletZoneWidthPx()
                     == ViewportLayout::BulletAreaWidth + ViewportLayout::SeparatorWidth );
        }

        THEN( "no line number area is reserved" )
        {
            REQUIRE( layout.lineNumberAreaWidthPx() == 0 );
            REQUIRE( layout.lineNumberAreaStartX() == 0 );
            REQUIRE( layout.contentStartPosX() == layout.bulletZoneWidthPx() );
        }

        THEN( "the left margin is the content start plus a separator" )
        {
            REQUIRE( layout.leftMarginPx()
                     == layout.contentStartPosX() + ViewportLayout::SeparatorWidth );
        }
    }

    GIVEN( "A layout showing line numbers up to 12345" )
    {
        auto input = fixedWidthInput();
        input.lineNumbersVisible = true;
        input.largestDisplayLineNumber = 12345;
        const ViewportLayout layout{ input };

        THEN( "the line number area fits five digits plus padding" )
        {
            REQUIRE( layout.lineNumberDigits() == 5 );
            REQUIRE( layout.lineNumberAreaWidthPx()
                     == 2 * ViewportLayout::LineNumberPadding + 5 * 10 );
        }

        THEN( "the line number area starts where the bullet zone ends" )
        {
            REQUIRE( layout.lineNumberAreaStartX() == layout.bulletZoneWidthPx() );
        }

        THEN( "content starts after the line number area" )
        {
            REQUIRE( layout.contentStartPosX()
                     == layout.bulletZoneWidthPx() + layout.lineNumberAreaWidthPx() );
        }
    }

    GIVEN( "A layout with no lines yet" )
    {
        auto input = fixedWidthInput();
        input.lineNumbersVisible = true;
        input.largestDisplayLineNumber = 0;
        const ViewportLayout layout{ input };

        THEN( "one digit is still reserved" )
        {
            REQUIRE( layout.lineNumberDigits() == 1 );
        }
    }
}

SCENARIO( "Viewport layout visible counts", "[viewportlayout]" )
{
    GIVEN( "A 500x400 viewport with 10x20 characters and no line numbers" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "the visible lines include the partly visible last one" )
        {
            REQUIRE( layout.visibleLines() == LinesCount( 400 / 20 + 1 ) );
        }

        THEN( "the left margin is subtracted exactly once" )
        {
            // The viewport width already excludes the vertical scrollbar, so
            // subtracting anything but the left margin once is the double
            // subtraction bug this pins down.
            REQUIRE( layout.visibleColumns() == LineLength{ ( 500 - layout.leftMarginPx() ) / 10 } );
        }
    }

    GIVEN( "A viewport narrower than its own left margin" )
    {
        auto input = fixedWidthInput();
        input.viewportWidthPx = 4;
        const ViewportLayout layout{ input };

        THEN( "at least one column is reported" )
        {
            REQUIRE( layout.visibleColumns() == 1_length );
        }
    }

    GIVEN( "A degenerate font reporting zero character width and height" )
    {
        auto input = fixedWidthInput();
        input.charWidthPx = 0;
        input.charHeightPx = 0;
        const ViewportLayout layout{ input };

        THEN( "the counts stay finite instead of dividing by zero" )
        {
            REQUIRE( layout.visibleColumns().get() > 0 );
            REQUIRE( layout.visibleLines().get() == 401 );
        }

        THEN( "hit testing still answers" )
        {
            const ViewportLayout rowsLayout{ input, unwrappedRows( 0_lnum, 3, 10_length ) };
            REQUIRE( rowsLayout.lineAtPoint( 2 ) == LineNumber( 2 ) );
        }
    }
}

SCENARIO( "Viewport layout hit testing without any paint", "[viewportlayout]" )
{
    GIVEN( "Three unwrapped rows starting at line 100" )
    {
        const ViewportLayout layout{ fixedWidthInput(), unwrappedRows( 100_lnum, 3, 40_length ) };

        THEN( "each 20 pixel band maps to its own Log Line" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 100 ) );
            REQUIRE( layout.lineAtPoint( 19 ) == LineNumber( 100 ) );
            REQUIRE( layout.lineAtPoint( 20 ) == LineNumber( 101 ) );
            REQUIRE( layout.lineAtPoint( 59 ) == LineNumber( 102 ) );
        }

        THEN( "a point below the last row resolves to nothing" )
        {
            REQUIRE_FALSE( layout.lineAtPoint( 60 ).has_value() );
        }

        THEN( "the column is the character the pixel sits in" )
        {
            const auto x = layout.leftMarginPx();
            REQUIRE( layout.filePositionAtPoint( x, 0 ) == FilePosition{ 100_lnum, 0_lcol } );
            REQUIRE( layout.filePositionAtPoint( x + 5, 0 ) == FilePosition{ 100_lnum, 0_lcol } );
            REQUIRE( layout.filePositionAtPoint( x + 10, 0 ) == FilePosition{ 100_lnum, 0_lcol } );
            REQUIRE( layout.filePositionAtPoint( x + 11, 0 ) == FilePosition{ 100_lnum, 1_lcol } );
        }

        THEN( "a click in the left margin lands on the first column" )
        {
            REQUIRE( layout.filePositionAtPoint( 0, 20 ) == FilePosition{ 101_lnum, 0_lcol } );
        }

        THEN( "a click past the end of the line is clamped to the line" )
        {
            REQUIRE( layout.filePositionAtPoint( 100000, 0 )
                     == FilePosition{ 100_lnum, 39_lcol } );
        }
    }

    GIVEN( "A view scrolled sideways" )
    {
        auto input = fixedWidthInput();
        input.firstColumn = 30_lcol;
        const ViewportLayout layout{ input, unwrappedRows( 0_lnum, 2, 200_length ) };

        THEN( "the first visible column is added to the hit column" )
        {
            REQUIRE( layout.filePositionAtPoint( layout.leftMarginPx() + 11, 0 )
                     == FilePosition{ 0_lnum, 31_lcol } );
        }
    }

    GIVEN( "A wrapped Log Line spanning three rows" )
    {
        auto input = fixedWidthInput();
        input.textWrap = true;
        ViewportRows rows;
        rows.push_back( ViewportRow{ 7_lnum, 0, 0_lcol, 40_length, 100_length } );
        rows.push_back( ViewportRow{ 7_lnum, 1, 40_lcol, 40_length, 100_length } );
        rows.push_back( ViewportRow{ 7_lnum, 2, 80_lcol, 20_length, 100_length } );
        const ViewportLayout layout{ input, rows };

        THEN( "every row belongs to the same Log Line" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 7 ) );
            REQUIRE( layout.lineAtPoint( 45 ) == LineNumber( 7 ) );
        }

        THEN( "the columns of earlier rows are added in" )
        {
            const auto x = layout.leftMarginPx() + 11;
            REQUIRE( layout.filePositionAtPoint( x, 0 ) == FilePosition{ 7_lnum, 1_lcol } );
            REQUIRE( layout.filePositionAtPoint( x, 20 ) == FilePosition{ 7_lnum, 41_lcol } );
            REQUIRE( layout.filePositionAtPoint( x, 40 ) == FilePosition{ 7_lnum, 81_lcol } );
        }
    }

    GIVEN( "A view drawn with a negative top offset" )
    {
        auto input = fixedWidthInput();
        input.drawingTopOffsetPx = -20;
        const ViewportLayout layout{ input, unwrappedRows( 0_lnum, 3, 10_length ) };

        THEN( "the offset shifts which row a pixel falls in" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 1 ) );
            REQUIRE( layout.lineAtPoint( 20 ) == LineNumber( 2 ) );
        }
    }

    GIVEN( "A layout with no rows at all" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "hit testing answers instead of reading past the end" )
        {
            REQUIRE_FALSE( layout.lineAtPoint( 10 ).has_value() );
            REQUIRE( layout.filePositionAtPoint( 10, 10 ) == FilePosition{ 0_lnum, 0_lcol } );
        }
    }

    GIVEN( "An empty Log Line" )
    {
        const ViewportLayout layout{ fixedWidthInput(), unwrappedRows( 3_lnum, 1, 0_length ) };

        THEN( "any point in it is the first column" )
        {
            REQUIRE( layout.filePositionAtPoint( 400, 0 ) == FilePosition{ 3_lnum, 0_lcol } );
        }
    }
}

SCENARIO( "Viewport layout rectangles", "[viewportlayout]" )
{
    GIVEN( "A wrapped Log Line spanning two rows below an unwrapped one" )
    {
        auto input = fixedWidthInput();
        input.textWrap = true;
        ViewportRows rows;
        rows.push_back( ViewportRow{ 4_lnum, 0, 0_lcol, 10_length, 10_length } );
        rows.push_back( ViewportRow{ 5_lnum, 0, 0_lcol, 40_length, 60_length } );
        rows.push_back( ViewportRow{ 5_lnum, 1, 40_lcol, 20_length, 60_length } );
        const ViewportLayout layout{ input, rows };

        THEN( "the line rectangle covers both of its rows" )
        {
            REQUIRE( layout.rectForLine( 5_lnum ) == ViewportRect{ 0, 20, 500, 40 } );
        }

        THEN( "a column rectangle is one character cell on its own row" )
        {
            REQUIRE( layout.rectForColumn( 5_lnum, 42_lcol )
                     == ViewportRect{ layout.textOriginX() + 20, 40, 10, 20 } );
        }

        THEN( "a line that is not displayed has an empty rectangle" )
        {
            REQUIRE( layout.rectForLine( 99_lnum ) == ViewportRect{} );
        }
    }
}

SCENARIO( "Viewport layout scroll ranges", "[viewportlayout]" )
{
    GIVEN( "A Log File shorter than the viewport" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "there is nothing to scroll vertically" )
        {
            REQUIRE( layout.verticalScrollRange( 5_lcount, 5_lcount ) == 0 );
        }
    }

    GIVEN( "A Log File longer than the viewport" )
    {
        const ViewportLayout layout{ fixedWidthInput() };
        const auto visible = layout.visibleLines();

        THEN( "the range reaches the last screenful" )
        {
            REQUIRE( layout.verticalScrollRange( 1000_lcount, visible )
                     == static_cast<int>( 1000 - visible.get() + 1 ) );
        }

        THEN( "wrapping at the bottom adds the extra rows" )
        {
            REQUIRE( layout.verticalScrollRange( 1000_lcount, visible + 7_lcount )
                     == static_cast<int>( 1000 - visible.get() + 1 + 7 ) );
        }

        THEN( "a line count that overflows the scrollbar saturates" )
        {
            const auto huge = LinesCount( std::numeric_limits<uint64_t>::max() - 1 );
            const auto range = layout.verticalScrollRange( huge, visible );
            REQUIRE( range == std::numeric_limits<int>::max() );
        }
    }

    GIVEN( "An unwrapped view and a long Log Line" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "the range covers the columns that do not fit" )
        {
            REQUIRE( layout.horizontalScrollRange( 500_length )
                     == 500 - layout.visibleColumns().get() + 1 );
        }

        THEN( "a Log File narrower than the viewport does not scroll" )
        {
            REQUIRE( layout.horizontalScrollRange( 1_length ) == 0 );
        }
    }

    GIVEN( "A wrapped view" )
    {
        auto input = fixedWidthInput();
        input.textWrap = true;
        const ViewportLayout layout{ input };

        THEN( "there is never a horizontal range" )
        {
            REQUIRE( layout.horizontalScrollRange( 100000_length ) == 0 );
        }
    }
}

SCENARIO( "Viewport layout first visible line range", "[viewportlayout]" )
{
    const ViewportLayout layout{ fixedWidthInput() };
    const auto visible = layout.visibleLines();

    GIVEN( "A Log File longer than the viewport" )
    {
        THEN( "the last valid first line leaves a full screen below it" )
        {
            REQUIRE( layout.lastValidFirstLine( 1000_lcount )
                     == LineNumber( 1000 - visible.get() ) );
        }
    }

    GIVEN( "A Log File shorter than the viewport" )
    {
        THEN( "the view starts at the top" )
        {
            REQUIRE( layout.lastValidFirstLine( 3_lcount ) == 0_lnum );
        }
    }

    GIVEN( "A first line past the end of the Log File" )
    {
        THEN( "it is clamped to the last line" )
        {
            REQUIRE( layout.clampFirstLine( 50_lnum, 10_lcount ) == 9_lnum );
            REQUIRE( layout.clampFirstLine( 50_lnum, 0_lcount ) == 0_lnum );
            REQUIRE( layout.clampFirstLine( 3_lnum, 10_lcount ) == 3_lnum );
        }
    }
}
