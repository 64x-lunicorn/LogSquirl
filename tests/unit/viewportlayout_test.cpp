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
#include <map>
#include <vector>

#include "viewportlayout.h"

namespace {

ViewportLayoutInput fixedWidthInput()
{
    ViewportLayoutInput input;
    input.charWidthPx = 10;
    input.charHeightPx = 20;
    input.viewportWidthPx = 500;
    input.viewportHeightPx = 400;
    input.scrollPosition = ScrollPosition{};
    input.firstColumn = 0_lcol;
    input.lineNumbersVisible = false;
    input.largestDisplayLineNumber = 0;
    input.textWrap = false;
    input.drawingTopOffsetPx = 0;
    return input;
}

// One Visual Line per Log Line, as an unwrapped view produces.
VisualLines unwrappedVisualLines( LineNumber firstLine, size_t count, LineLength lineLength )
{
    VisualLines visualLines;
    for ( size_t i = 0; i < count; ++i ) {
        visualLines.push_back(
            VisualLine{ firstLine + LinesCount( i ), 0, 0_lcol, lineLength, lineLength } );
    }
    return visualLines;
}

// A Log File longer than the viewport, with no elastic pull, not hooked and not
// aligned on its last Visual Line. Each pull-to-follow case changes what it is about.
PullToFollowState restingPullToFollowState()
{
    return PullToFollowState{ .elasticHookLength = 0,
                              .hooked = false,
                              .lastLineAligned = false,
                              .bottomVisualLines = 20_lcount };
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
            REQUIRE( layout.visibleColumns()
                     == LineLength{ ( 500 - layout.leftMarginPx() ) / 10 } );
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
            const ViewportLayout visualLinesLayout{ input,
                                                    unwrappedVisualLines( 0_lnum, 3, 10_length ) };
            REQUIRE( visualLinesLayout.lineAtPoint( 2 ) == LineNumber( 2 ) );
        }
    }
}

SCENARIO( "Viewport layout hit testing without any paint", "[viewportlayout]" )
{
    GIVEN( "Three unwrapped Visual Lines starting at line 100" )
    {
        const ViewportLayout layout{ fixedWidthInput(),
                                     unwrappedVisualLines( 100_lnum, 3, 40_length ) };

        THEN( "each 20 pixel band maps to its own Log Line" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 100 ) );
            REQUIRE( layout.lineAtPoint( 19 ) == LineNumber( 100 ) );
            REQUIRE( layout.lineAtPoint( 20 ) == LineNumber( 101 ) );
            REQUIRE( layout.lineAtPoint( 59 ) == LineNumber( 102 ) );
        }

        THEN( "a point below the last Visual Line resolves to nothing" )
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
            REQUIRE( layout.filePositionAtPoint( 100000, 0 ) == FilePosition{ 100_lnum, 39_lcol } );
        }
    }

    GIVEN( "A view scrolled sideways" )
    {
        auto input = fixedWidthInput();
        input.firstColumn = 30_lcol;
        const ViewportLayout layout{ input, unwrappedVisualLines( 0_lnum, 2, 200_length ) };

        THEN( "the first visible column is added to the hit column" )
        {
            REQUIRE( layout.filePositionAtPoint( layout.leftMarginPx() + 11, 0 )
                     == FilePosition{ 0_lnum, 31_lcol } );
        }
    }

    GIVEN( "A wrapped Log Line spanning three Visual Lines" )
    {
        auto input = fixedWidthInput();
        input.textWrap = true;
        VisualLines visualLines;
        visualLines.push_back( VisualLine{ 7_lnum, 0, 0_lcol, 40_length, 100_length } );
        visualLines.push_back( VisualLine{ 7_lnum, 1, 40_lcol, 40_length, 100_length } );
        visualLines.push_back( VisualLine{ 7_lnum, 2, 80_lcol, 20_length, 100_length } );
        const ViewportLayout layout{ input, visualLines };

        THEN( "every Visual Line belongs to the same Log Line" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 7 ) );
            REQUIRE( layout.lineAtPoint( 45 ) == LineNumber( 7 ) );
        }

        THEN( "the columns of earlier Visual Lines are added in" )
        {
            const auto x = layout.leftMarginPx() + 11;
            REQUIRE( layout.filePositionAtPoint( x, 0 ) == FilePosition{ 7_lnum, 1_lcol } );
            REQUIRE( layout.filePositionAtPoint( x, 20 ) == FilePosition{ 7_lnum, 41_lcol } );
            REQUIRE( layout.filePositionAtPoint( x, 40 ) == FilePosition{ 7_lnum, 81_lcol } );
        }
    }

    GIVEN( "A Scroll Position partway through a wrapped Log Line with 12,000 Visual Lines" )
    {
        // The layout holds only the Visual Lines from the Scroll Position down,
        // so the last Visual Lines of a very tall Log Line are hit-tested like
        // any others.
        auto input = fixedWidthInput();
        input.textWrap = true;
        input.scrollPosition = ScrollPosition{ 7_lnum, 11998 };
        VisualLines visualLines;
        visualLines.push_back( VisualLine{ 7_lnum, 11998, 11998_lcol, 1_length, 12000_length } );
        visualLines.push_back( VisualLine{ 7_lnum, 11999, 11999_lcol, 1_length, 12000_length } );
        visualLines.push_back( VisualLine{ 8_lnum, 0, 0_lcol, 10_length, 10_length } );
        const ViewportLayout layout{ input, visualLines };

        THEN( "the top rows are its last Visual Lines" )
        {
            const auto x = layout.leftMarginPx() + 1;
            REQUIRE( layout.filePositionAtPoint( x, 0 ) == FilePosition{ 7_lnum, 11998_lcol } );
            REQUIRE( layout.filePositionAtPoint( x, 20 ) == FilePosition{ 7_lnum, 11999_lcol } );
            REQUIRE( layout.lineAtPoint( 40 ) == LineNumber( 8 ) );
        }

        THEN( "the Log Line's rectangle covers only its Visual Lines in the Viewport" )
        {
            REQUIRE( layout.rectForLine( 7_lnum ) == ViewportRect{ 0, 0, 500, 40 } );
        }
    }

    GIVEN( "A view drawn with a negative top offset" )
    {
        auto input = fixedWidthInput();
        input.drawingTopOffsetPx = -20;
        const ViewportLayout layout{ input, unwrappedVisualLines( 0_lnum, 3, 10_length ) };

        THEN( "the offset shifts which Visual Line a pixel falls in" )
        {
            REQUIRE( layout.lineAtPoint( 0 ) == LineNumber( 1 ) );
            REQUIRE( layout.lineAtPoint( 20 ) == LineNumber( 2 ) );
        }
    }

    GIVEN( "A layout with no Visual Lines at all" )
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
        const ViewportLayout layout{ fixedWidthInput(),
                                     unwrappedVisualLines( 3_lnum, 1, 0_length ) };

        THEN( "any point in it is the first column" )
        {
            REQUIRE( layout.filePositionAtPoint( 400, 0 ) == FilePosition{ 3_lnum, 0_lcol } );
        }
    }
}

SCENARIO( "Viewport layout rectangles", "[viewportlayout]" )
{
    GIVEN( "A wrapped Log Line spanning two Visual Lines below an unwrapped one" )
    {
        auto input = fixedWidthInput();
        input.textWrap = true;
        VisualLines visualLines;
        visualLines.push_back( VisualLine{ 4_lnum, 0, 0_lcol, 10_length, 10_length } );
        visualLines.push_back( VisualLine{ 5_lnum, 0, 0_lcol, 40_length, 60_length } );
        visualLines.push_back( VisualLine{ 5_lnum, 1, 40_lcol, 20_length, 60_length } );
        const ViewportLayout layout{ input, visualLines };

        THEN( "the line rectangle covers both of its Visual Lines" )
        {
            REQUIRE( layout.rectForLine( 5_lnum ) == ViewportRect{ 0, 20, 500, 40 } );
        }

        THEN( "a column rectangle is one character cell on its own Visual Line" )
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
    GIVEN( "A Log File whose bottom Scroll Position is its top" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "there is nothing to scroll vertically" )
        {
            REQUIRE( layout.verticalScrollRange( ScrollPosition{} ) == 0 );
        }
    }

    GIVEN( "A Log File longer than the viewport" )
    {
        const ViewportLayout layout{ fixedWidthInput() };

        THEN( "the range ends at the Log Line of the bottom Scroll Position, whatever its Visual "
              "Line" )
        {
            REQUIRE( layout.verticalScrollRange( ScrollPosition{ 980_lnum, 0 } ) == 980 );
            REQUIRE( layout.verticalScrollRange( ScrollPosition{ 999_lnum, 37 } ) == 999 );
        }

        THEN( "a line number that overflows the scrollbar saturates" )
        {
            const ScrollPosition huge{ LineNumber( std::numeric_limits<uint64_t>::max() - 1 ), 0 };
            REQUIRE( layout.verticalScrollRange( huge ) == std::numeric_limits<int>::max() );
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

SCENARIO( "Viewport layout Scroll Position range", "[viewportlayout][scrollposition]" )
{
    const ViewportLayout layout{ fixedWidthInput() };

    GIVEN( "A Scroll Position past the end of the Log File" )
    {
        THEN( "it is clamped to the first Visual Line of the last Log Line" )
        {
            REQUIRE( layout.clampScrollPosition( ScrollPosition{ 50_lnum, 4 }, 10_lcount )
                     == ScrollPosition{ 9_lnum, 0 } );
            REQUIRE( layout.clampScrollPosition( ScrollPosition{ 50_lnum, 4 }, 0_lcount )
                     == ScrollPosition{} );
        }
    }

    GIVEN( "A Scroll Position partway through a Log Line inside the Log File" )
    {
        const ScrollPosition partway{ 3_lnum, 4 };

        THEN( "with text wrapping it is kept as it is" )
        {
            auto input = fixedWidthInput();
            input.textWrap = true;
            REQUIRE( ViewportLayout{ input }.clampScrollPosition( partway, 10_lcount ) == partway );
        }

        THEN( "without text wrapping the Log Line is kept and the Visual Line dropped" )
        {
            REQUIRE( layout.clampScrollPosition( partway, 10_lcount )
                     == ScrollPosition{ 3_lnum, 0 } );
        }
    }
}

SCENARIO( "Scroll Positions are ordered by Log Line, then by Visual Line",
          "[viewportlayout][scrollposition]" )
{
    REQUIRE( ScrollPosition{ 3_lnum, 9 } < ScrollPosition{ 4_lnum, 0 } );
    REQUIRE( ScrollPosition{ 4_lnum, 1 } > ScrollPosition{ 4_lnum, 0 } );
    REQUIRE( ScrollPosition{ 4_lnum, 1 } == ScrollPosition{ 4_lnum, 1 } );
    REQUIRE( ScrollPosition{} == ScrollPosition{ 0_lnum, 0 } );
}

SCENARIO( "A page is one Viewport height of Visual Lines", "[viewportlayout][scrollposition]" )
{
    GIVEN( "A 400 px viewport with 20 px Visual Lines" )
    {
        THEN( "a page is the 20 Visual Lines that fit, not the partly hidden one below" )
        {
            REQUIRE( ViewportLayout{ fixedWidthInput() }.visualLinesPerPage() == 20_lcount );
        }
    }

    GIVEN( "A viewport lower than one Visual Line" )
    {
        auto input = fixedWidthInput();
        input.viewportHeightPx = 5;

        THEN( "a page still moves one Visual Line" )
        {
            REQUIRE( ViewportLayout{ input }.visualLinesPerPage() == 1_lcount );
        }
    }
}

namespace {

// A Log File of 100 Log Lines. Log Line 2 wraps into 5 Visual Lines, Log Line 4
// into 30, every other Log Line into one. Remembers which Log Lines it was asked
// about, in order.
struct WrappedLogFile {
    std::vector<LineNumber::UnderlyingType> asked;

    VisualLineCounter counter()
    {
        return [ this ]( LineNumber line ) -> size_t {
            asked.push_back( line.get() );
            switch ( line.get() ) {
            case 2:
                return 5;
            case 4:
                return 30;
            default:
                return 1;
            }
        };
    }
};

const ScrollPosition LastOfWrappedLogFile{ 90_lnum, 0 };

} // namespace

SCENARIO( "Moving a Scroll Position by Visual Lines", "[viewportlayout][scrollposition]" )
{
    WrappedLogFile file;

    GIVEN( "A Scroll Position partway through a tall Log Line" )
    {
        THEN( "moving inside that Log Line only changes its Visual Line" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 4_lnum, 2 }, 3, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 4_lnum, 5 } );
            REQUIRE( moveScrollPosition( ScrollPosition{ 4_lnum, 5 }, -3, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 4_lnum, 2 } );
        }

        THEN( "moving down can reach its last Visual Line" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 4_lnum, 0 }, 29, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 4_lnum, 29 } );
        }
    }

    GIVEN( "A move that crosses into other Log Lines" )
    {
        THEN( "every Visual Line passed counts once, whichever Log Line it belongs to" )
        {
            // Log Line 2's Visual Lines 3 and 4, then Log Line 3, then Log Line 4.
            REQUIRE( moveScrollPosition( ScrollPosition{ 2_lnum, 3 }, 3, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 4_lnum, 0 } );
            REQUIRE( moveScrollPosition( ScrollPosition{ 4_lnum, 28 }, 3, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 6_lnum, 0 } );
        }

        THEN( "moving up into a wrapped Log Line lands on its last Visual Line" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 3_lnum, 0 }, -1, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 2_lnum, 4 } );
        }

        THEN( "moving down and back up by the same amount returns to where it started" )
        {
            const ScrollPosition start{ 2_lnum, 3 };
            const auto down = moveScrollPosition( start, 40, LastOfWrappedLogFile, file.counter() );
            REQUIRE( down == ScrollPosition{ 12_lnum, 0 } );
            REQUIRE( moveScrollPosition( down, -40, LastOfWrappedLogFile, file.counter() )
                     == start );
        }

        THEN( "only the Log Lines passed over are wrapped" )
        {
            moveScrollPosition( ScrollPosition{ 2_lnum, 3 }, 3, LastOfWrappedLogFile,
                                file.counter() );
            REQUIRE( file.asked == std::vector<LineNumber::UnderlyingType>{ 2, 3 } );

            file.asked.clear();
            moveScrollPosition( ScrollPosition{ 4_lnum, 0 }, -2, LastOfWrappedLogFile,
                                file.counter() );
            REQUIRE( file.asked == std::vector<LineNumber::UnderlyingType>{ 3, 2 } );
        }
    }

    GIVEN( "A move past either end of the Log File" )
    {
        THEN( "moving up stops at the top" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 2_lnum, 1 }, -10, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{} );
        }

        THEN( "moving down stops at the last Scroll Position" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 88_lnum, 0 }, 10, LastOfWrappedLogFile,
                                         file.counter() )
                     == LastOfWrappedLogFile );
        }

        THEN( "a last Scroll Position partway through a Log Line stops there too" )
        {
            const ScrollPosition last{ 4_lnum, 10 };
            REQUIRE( moveScrollPosition( ScrollPosition{ 4_lnum, 2 }, 20, last, file.counter() )
                     == last );
            REQUIRE( moveScrollPosition( ScrollPosition{ 2_lnum, 0 }, 100, last, file.counter() )
                     == last );
        }

        THEN( "a Scroll Position already past the last one is brought back" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 95_lnum, 0 }, 1, LastOfWrappedLogFile,
                                         file.counter() )
                     == LastOfWrappedLogFile );
            REQUIRE( moveScrollPosition( ScrollPosition{ 95_lnum, 0 }, -1, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 89_lnum, 0 } );
        }
    }

    GIVEN( "A Visual Line index a re-wrap has left past the end of its Log Line" )
    {
        THEN( "moving down continues with the next Log Line" )
        {
            REQUIRE( moveScrollPosition( ScrollPosition{ 3_lnum, 7 }, 1, LastOfWrappedLogFile,
                                         file.counter() )
                     == ScrollPosition{ 4_lnum, 0 } );
        }
    }
}

SCENARIO( "The Viewport's rows", "[viewportlayout][bottom]" )
{
    GIVEN( "A viewport a whole number of Visual Lines high" )
    {
        THEN( "it has that many rows" )
        {
            REQUIRE( ViewportLayout{ fixedWidthInput() }.viewportRows() == 20_lcount );
        }
    }

    GIVEN( "A viewport with a partly visible last row" )
    {
        auto input = fixedWidthInput();
        input.viewportHeightPx = 410;

        THEN( "the partly visible row counts" )
        {
            REQUIRE( ViewportLayout{ input }.viewportRows() == 21_lcount );
        }
    }

    GIVEN( "A viewport lower than one Visual Line, or of no height at all" )
    {
        auto input = fixedWidthInput();
        input.viewportHeightPx = 5;
        auto empty = fixedWidthInput();
        empty.viewportHeightPx = 0;

        THEN( "it still has one row" )
        {
            REQUIRE( ViewportLayout{ input }.viewportRows() == 1_lcount );
            REQUIRE( ViewportLayout{ empty }.viewportRows() == 1_lcount );
        }
    }
}

namespace {

// A Log File of a given number of Log Lines, each wrapping into one Visual
// Line unless listed. Remembers which Log Lines it was asked about, in order.
struct CountedLogFile {
    std::map<LineNumber::UnderlyingType, size_t> wrapped;
    std::vector<LineNumber::UnderlyingType> asked;

    VisualLineCounter counter()
    {
        return [ this ]( LineNumber line ) -> size_t {
            asked.push_back( line.get() );
            const auto found = wrapped.find( line.get() );
            return found == wrapped.end() ? 1 : found->second;
        };
    }
};

} // namespace

// With a 400 px viewport and 20 px Visual Lines, the Viewport has 20 rows.
SCENARIO( "The bottom Scroll Position of a Log File", "[viewportlayout][scrollposition][bottom]" )
{
    const ViewportLayout layout{ fixedWidthInput() };
    CountedLogFile file;

    GIVEN( "A Log File that ends in Log Lines of one Visual Line each" )
    {
        const auto bottom = layout.logFileBottom( 100_lcount, file.counter() );

        THEN( "the last 20 Log Lines are shown, the last on the last row" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 80_lnum, 0 } );
            REQUIRE( bottom.visualLines == 20_lcount );
        }

        THEN( "only those Log Lines are wrapped, from the last one up" )
        {
            std::vector<LineNumber::UnderlyingType> lastTwenty;
            for ( LineNumber::UnderlyingType line = 99; line >= 80; --line ) {
                lastTwenty.push_back( line );
            }
            REQUIRE( file.asked == lastTwenty );
        }
    }

    GIVEN( "A Log File whose last Log Lines wrap" )
    {
        file.wrapped = { { 98, 5 }, { 99, 3 } };
        const auto bottom = layout.logFileBottom( 100_lcount, file.counter() );

        THEN( "the Visual Lines of the wrapped Log Lines count, not the Log Lines" )
        {
            // 3 + 5 Visual Lines, then the 12 Log Lines 86 to 97.
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 86_lnum, 0 } );
            REQUIRE( bottom.visualLines == 20_lcount );
        }
    }

    GIVEN( "A Log File whose bottom is partway through a wrapped Log Line" )
    {
        file.wrapped = { { 98, 30 }, { 99, 3 } };
        const auto bottom = layout.logFileBottom( 100_lcount, file.counter() );

        THEN( "the bottom Scroll Position starts at the Visual Line that fills the top row" )
        {
            // Visual Lines 13 to 29 of Log Line 98, then the 3 of Log Line 99.
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 98_lnum, 13 } );
            REQUIRE( file.asked == std::vector<LineNumber::UnderlyingType>{ 99, 98 } );
        }
    }

    GIVEN( "A Log File whose last Log Line is taller than the Viewport" )
    {
        file.wrapped = { { 99, 50 } };
        const auto bottom = layout.logFileBottom( 100_lcount, file.counter() );

        THEN( "its last 20 Visual Lines are shown, and no other Log Line is wrapped" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 99_lnum, 30 } );
            REQUIRE( bottom.visualLines == 20_lcount );
            REQUIRE( file.asked == std::vector<LineNumber::UnderlyingType>{ 99 } );
        }
    }

    GIVEN( "A Log File with fewer Visual Lines than the Viewport has rows" )
    {
        file.wrapped = { { 2, 4 } };
        const auto bottom = layout.logFileBottom( 5_lcount, file.counter() );

        THEN( "the bottom Scroll Position is the top of the Log File" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{} );
            REQUIRE( bottom.visualLines == 8_lcount );
        }
    }

    GIVEN( "A Log File with fewer Log Lines than rows, one of them taller than the Viewport" )
    {
        file.wrapped = { { 2, 30 } };
        const auto bottom = layout.logFileBottom( 5_lcount, file.counter() );

        THEN( "the bottom Scroll Position is partway through the tall Log Line" )
        {
            // Visual Lines 12 to 29 of Log Line 2, then Log Lines 3 and 4.
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 2_lnum, 12 } );
            REQUIRE( bottom.visualLines == 20_lcount );
        }
    }

    GIVEN( "A Log File that exactly fills the Viewport" )
    {
        const auto bottom = layout.logFileBottom( 20_lcount, file.counter() );

        THEN( "its bottom is its top" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{} );
            REQUIRE( bottom.visualLines == 20_lcount );
        }
    }

    GIVEN( "An empty Log File" )
    {
        const auto bottom = layout.logFileBottom( 0_lcount, file.counter() );

        THEN( "its bottom is its top, and nothing is wrapped" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{} );
            REQUIRE( bottom.visualLines == 0_lcount );
            REQUIRE( file.asked.empty() );
        }
    }

    GIVEN( "A viewport with a partly visible last row" )
    {
        auto input = fixedWidthInput();
        input.viewportHeightPx = 410;
        const auto bottom = ViewportLayout{ input }.logFileBottom( 100_lcount, file.counter() );

        THEN( "the partly visible row is filled too" )
        {
            REQUIRE( bottom.scrollPosition == ScrollPosition{ 79_lnum, 0 } );
            REQUIRE( bottom.visualLines == 21_lcount );
        }
    }
}

SCENARIO( "The last Visual Line is aligned on the Viewport's last row only at the bottom",
          "[viewportlayout][scrollposition][bottom]" )
{
    const LogFileBottom bottom{ ScrollPosition{ 98_lnum, 13 }, 20_lcount };

    THEN( "at the bottom Scroll Position the last Visual Line is aligned" )
    {
        REQUIRE( bottom.alignsLastVisualLineAt( ScrollPosition{ 98_lnum, 13 } ) );
    }

    THEN( "anywhere above it the first Visual Line is aligned on the top row" )
    {
        REQUIRE_FALSE( bottom.alignsLastVisualLineAt( ScrollPosition{ 98_lnum, 12 } ) );
        REQUIRE_FALSE( bottom.alignsLastVisualLineAt( ScrollPosition{} ) );
    }

    THEN( "a Log File whose bottom is its top is shown from its top" )
    {
        const LogFileBottom shortBottom{ ScrollPosition{}, 20_lcount };
        REQUIRE_FALSE( shortBottom.alignsLastVisualLineAt( ScrollPosition{} ) );
    }
}

// The expected pixels below are the positions painting placed the text and the
// pull-to-follow bar at since their geometry had one definition (#138), with
// the bottom Scroll Position filling exactly the Viewport's rows (#154). A 400 px
// viewport of 20 px Visual Lines has 20 rows and nothing overhangs its bottom.
SCENARIO( "Viewport layout pull-to-follow geometry", "[viewportlayout]" )
{
    const ViewportLayout layout{ fixedWidthInput() };
    REQUIRE( layout.viewportRows() == 20_lcount );

    GIVEN( "No elastic pull and the view not hooked" )
    {
        const auto geometry = layout.pullToFollowGeometry( restingPullToFollowState() );

        THEN( "there is no bar and the text is drawn from the top" )
        {
            REQUIRE( geometry.barHeightPx == 0 );
            REQUIRE( geometry.textTopPx == 0 );
            REQUIRE( geometry.barTopPx == 400 );
        }
    }

    GIVEN( "An elastic pull that has not hooked yet" )
    {
        auto state = restingPullToFollowState();
        state.elasticHookLength = 140;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "the text moves up by the pull and the bar follows below it" )
        {
            REQUIRE( geometry.barHeightPx == 10 );
            REQUIRE( geometry.textTopPx == -10 );
            REQUIRE( geometry.barTopPx == 390 );
        }
    }

    GIVEN( "The elastic hooked on a Log File longer than the viewport" )
    {
        auto state = restingPullToFollowState();
        state.hooked = true;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "the hooked bar shows below the last Visual Line" )
        {
            REQUIRE( geometry.barHeightPx == ViewportLayout::PullToFollowHookedHeight );
            REQUIRE( geometry.textTopPx == -10 );
            REQUIRE( geometry.barTopPx == 390 );
        }
    }

    GIVEN( "The elastic hooked on a Log File shorter than a screenful" )
    {
        auto state = restingPullToFollowState();
        state.elasticHookLength = 70;
        state.hooked = true;
        state.bottomVisualLines = 5_lcount;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "the text stays at the top, moved only by the pull" )
        {
            REQUIRE( geometry.barHeightPx == 15 );
            REQUIRE( geometry.textTopPx == -5 );
        }

        THEN( "the bar sits at the bottom of the viewport" )
        {
            REQUIRE( geometry.barTopPx == -5 + 400 - ViewportLayout::PullToFollowHookedHeight );
        }
    }

    GIVEN( "The elastic hooked on a Log File that exactly fills the Viewport's rows" )
    {
        auto state = restingPullToFollowState();
        state.hooked = true;
        state.bottomVisualLines = 20_lcount;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "it is placed like a longer Log File" )
        {
            REQUIRE( geometry.textTopPx == -10 );
            REQUIRE( geometry.barTopPx == 390 );
        }
    }

    GIVEN( "The last Visual Line aligned to the bottom, not hooked" )
    {
        auto state = restingPullToFollowState();
        state.lastLineAligned = true;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "nothing overhangs, so the text stays where it is" )
        {
            REQUIRE( geometry.barHeightPx == 0 );
            REQUIRE( geometry.textTopPx == 0 );
            REQUIRE( geometry.barTopPx == 400 );
        }
    }

    GIVEN( "The last Visual Line aligned and the elastic hooked" )
    {
        auto state = restingPullToFollowState();
        state.hooked = true;
        state.lastLineAligned = true;
        const auto geometry = layout.pullToFollowGeometry( state );

        THEN( "the hook wins over the alignment" )
        {
            REQUIRE( geometry.textTopPx == -10 );
            REQUIRE( geometry.barTopPx == 390 );
        }
    }

    GIVEN( "A 410 px viewport, whose partly visible last row overhangs by 10 px" )
    {
        auto input = fixedWidthInput();
        input.viewportHeightPx = 410;
        const ViewportLayout partlyLayout{ input };
        auto state = restingPullToFollowState();
        state.bottomVisualLines = 21_lcount;

        THEN( "aligned, the text moves up by the overhang: the last Visual Line ends at the "
              "bottom of the Viewport" )
        {
            state.lastLineAligned = true;
            const auto geometry = partlyLayout.pullToFollowGeometry( state );
            REQUIRE( geometry.textTopPx == -10 );
            REQUIRE( geometry.textTopPx + 21 * 20 == 410 );
            REQUIRE( geometry.barTopPx == 410 );
        }

        THEN( "hooked, the bar starts right below the last Visual Line" )
        {
            state.lastLineAligned = true;
            state.hooked = true;
            const auto geometry = partlyLayout.pullToFollowGeometry( state );
            REQUIRE( geometry.textTopPx == -20 );
            REQUIRE( geometry.barTopPx == geometry.textTopPx + 21 * 20 );
            REQUIRE( geometry.barTopPx == 410 - ViewportLayout::PullToFollowHookedHeight );
        }
    }

    GIVEN( "A layout that already carries a drawing offset" )
    {
        auto input = fixedWidthInput();
        input.drawingTopOffsetPx = -123;
        const ViewportLayout offsetLayout{ input };

        THEN( "the geometry does not depend on it, since it is what produces it" )
        {
            auto state = restingPullToFollowState();
            state.elasticHookLength = 140;
            state.hooked = true;
            REQUIRE( offsetLayout.pullToFollowGeometry( state )
                     == layout.pullToFollowGeometry( state ) );
        }
    }
}
