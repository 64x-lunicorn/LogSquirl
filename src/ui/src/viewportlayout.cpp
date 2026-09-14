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

#include "viewportlayout.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

int countLineNumberDigits( uint64_t x )
{
    int digits = 1;
    while ( x >= 10 ) {
        x /= 10;
        ++digits;
    }
    return digits;
}

namespace {

// How many units of elastic hook pull make one pixel of the pull-to-follow bar.
constexpr int ElasticHookLengthPerPx = 14;

// Round up a / b for a possibly negative numerator and a positive divisor.
int ceilDiv( int a, int b )
{
    const int quotient = a / b;
    const int remainder = a % b;
    return ( remainder > 0 ) ? quotient + 1 : quotient;
}

int clampToInt( int64_t value )
{
    return static_cast<int>( std::clamp<int64_t>( value, 0, std::numeric_limits<int>::max() ) );
}

} // namespace

ViewportLayout::ViewportLayout( ViewportLayoutInput input, VisualLines visualLines )
    : input_{ input }
    , visualLines_{ std::move( visualLines ) }
{
}

int ViewportLayout::charWidth() const
{
    return std::max( input_.charWidthPx, 1 );
}

int ViewportLayout::charHeight() const
{
    return std::max( input_.charHeightPx, 1 );
}

int ViewportLayout::lineNumberDigits() const
{
    return countLineNumberDigits( input_.largestDisplayLineNumber );
}

int ViewportLayout::lineNumberAreaWidthPx() const
{
    if ( !input_.lineNumbersVisible ) {
        return 0;
    }
    return 2 * LineNumberPadding + charWidth() * lineNumberDigits();
}

int ViewportLayout::lineNumberAreaStartX() const
{
    return input_.lineNumbersVisible ? bulletZoneWidthPx() : 0;
}

int ViewportLayout::bulletZoneWidthPx() const
{
    return BulletAreaWidth + SeparatorWidth;
}

int ViewportLayout::contentStartPosX() const
{
    return bulletZoneWidthPx() + lineNumberAreaWidthPx();
}

int ViewportLayout::leftMarginPx() const
{
    return contentStartPosX() + SeparatorWidth;
}

int ViewportLayout::textOriginX() const
{
    return contentStartPosX() + ContentMarginWidth;
}

LinesCount ViewportLayout::visibleLines() const
{
    const int lines = input_.viewportHeightPx / charHeight() + 1;
    return LinesCount( static_cast<LinesCount::UnderlyingType>( std::max( lines, 1 ) ) );
}

LineLength ViewportLayout::visibleColumns() const
{
    // viewportWidthPx already excludes the vertical scrollbar (Qt's
    // QAbstractScrollArea handles that), so the margin is subtracted exactly
    // once. Floor division: only columns that fully fit are counted.
    // At least one column, so a viewport narrower than the left margin still
    // yields a usable value.
    const int columns = ( input_.viewportWidthPx - leftMarginPx() ) / charWidth();
    return LineLength{ std::max( columns, 1 ) };
}

std::optional<size_t> ViewportLayout::visualLineAtPoint( int yPos ) const
{
    const auto offset = std::abs( ( yPos - input_.drawingTopOffsetPx ) / charHeight() );
    const auto index = static_cast<size_t>( offset );
    if ( index < visualLines_.size() ) {
        return index;
    }
    return std::nullopt;
}

OptionalLineNumber ViewportLayout::lineAtPoint( int yPos ) const
{
    const auto visualLine = visualLineAtPoint( yPos );
    if ( !visualLine.has_value() ) {
        return OptionalLineNumber{};
    }
    return visualLines_[ *visualLine ].lineNumber;
}

FilePosition ViewportLayout::filePositionAtPoint( int xPos, int yPos ) const
{
    if ( visualLines_.empty() ) {
        return FilePosition{ 0_lnum, 0_lcol };
    }

    const auto offset = std::abs( ( yPos - input_.drawingTopOffsetPx ) / charHeight() );
    const auto visualLineIndex
        = visualLines_.size() > 1
              ? std::clamp( static_cast<size_t>( offset ), size_t{ 0 }, visualLines_.size() - 1 )
              : size_t{ 0 };

    const auto& visualLine = visualLines_[ visualLineIndex ];

    if ( visualLine.lineLength.get() <= 1 ) {
        return FilePosition{ visualLine.lineNumber, 0_lcol };
    }

    // Number of columns of this Visual Line that are actually in the Viewport.
    const auto visibleTextLength
        = input_.textWrap ? visualLine.length.get()
                          : std::clamp( visualLine.lineLength.get() - input_.firstColumn.get(),
                                        LineLength::UnderlyingType{ 0 }, visibleColumns().get() );

    // The first column whose right edge is at or past xPos, then step back one
    // to land on the column the pixel is actually inside.
    const auto firstColumnPastX = std::clamp<int64_t>(
        ceilDiv( xPos - leftMarginPx(), charWidth() ), 0, visibleTextLength );

    auto column
        = LineColumn{ static_cast<LineColumn::UnderlyingType>( firstColumnPastX ) } - 1_length;

    // Move from the Visual Line's own columns to the Log Line's columns.
    column += input_.textWrap ? LineLength{ visualLine.firstColumn.get() }
                              : LineLength{ input_.firstColumn.get() };

    const auto maxColumn = LineColumn{ visualLine.lineLength.get() } - 1_length;
    column = std::clamp( column, 0_lcol, maxColumn );

    return FilePosition{ visualLine.lineNumber, column };
}

ViewportRect ViewportLayout::rectForLine( LineNumber line ) const
{
    const auto first = std::find_if(
        visualLines_.begin(), visualLines_.end(),
        [ line ]( const VisualLine& visualLine ) { return visualLine.lineNumber == line; } );
    if ( first == visualLines_.end() ) {
        return ViewportRect{};
    }

    const auto visualLineCount = static_cast<int>(
        std::count_if( first, visualLines_.end(), [ line ]( const VisualLine& visualLine ) {
            return visualLine.lineNumber == line;
        } ) );

    const auto firstIndex = static_cast<int>( std::distance( visualLines_.begin(), first ) );
    return ViewportRect{ 0, input_.drawingTopOffsetPx + firstIndex * charHeight(),
                         input_.viewportWidthPx, visualLineCount * charHeight() };
}

ViewportRect ViewportLayout::rectForColumn( LineNumber line, LineColumn column ) const
{
    for ( size_t index = 0; index < visualLines_.size(); ++index ) {
        const auto& visualLine = visualLines_[ index ];
        if ( visualLine.lineNumber != line ) {
            continue;
        }

        const auto visualLineFirst = visualLine.firstColumn;
        const auto visualLineLast = visualLineFirst + visualLine.length;
        if ( input_.textWrap && !( column >= visualLineFirst && column < visualLineLast ) ) {
            continue;
        }

        const auto columnInVisualLine
            = input_.textWrap ? ( column - LineLength{ visualLineFirst.get() } ).get()
                              : ( column - LineLength{ input_.firstColumn.get() } ).get();

        return ViewportRect{ textOriginX() + static_cast<int>( columnInVisualLine ) * charWidth(),
                             input_.drawingTopOffsetPx + static_cast<int>( index ) * charHeight(),
                             charWidth(), charHeight() };
    }

    return ViewportRect{};
}

int ViewportLayout::verticalScrollRange( LinesCount totalLines,
                                         LinesCount bottomWrappedVisibleLines ) const
{
    const auto visible = visibleLines();
    if ( totalLines < visible ) {
        return 0;
    }

    // Wrapping makes the last screenful taller than a screenful of unwrapped
    // Log Lines; the extra Visual Lines have to be reachable.
    const auto wrappedAdjust = bottomWrappedVisibleLines.get() > visible.get()
                                   ? bottomWrappedVisibleLines.get() - visible.get()
                                   : LinesCount::UnderlyingType{ 0 };

    const auto range = totalLines.get() - visible.get() + 1 + wrappedAdjust;

    // A Log File can hold more lines than a scrollbar can address; saturate
    // rather than wrap around into a negative range.
    return static_cast<int>(
        std::min<uint64_t>( range, static_cast<uint64_t>( std::numeric_limits<int>::max() ) ) );
}

int ViewportLayout::horizontalScrollRange( LineLength maxLineLength ) const
{
    if ( input_.textWrap ) {
        return 0;
    }

    const auto visible = visibleColumns();
    if ( maxLineLength.get() < visible.get() ) {
        return 0;
    }

    return clampToInt( static_cast<int64_t>( maxLineLength.get() )
                       - static_cast<int64_t>( visible.get() ) + 1 );
}

ScrollPosition ViewportLayout::lastValidScrollPosition( LinesCount totalLines ) const
{
    const auto visible = visibleLines();
    if ( totalLines.get() <= visible.get() ) {
        return ScrollPosition{};
    }
    return ScrollPosition{ LineNumber( totalLines.get() - visible.get() ), 0 };
}

ScrollPosition ViewportLayout::clampScrollPosition( ScrollPosition position,
                                                    LinesCount totalLines ) const
{
    if ( totalLines.get() == 0 ) {
        return ScrollPosition{};
    }
    const auto lastLine = LineNumber( totalLines.get() - 1 );
    if ( position.lineNumber > lastLine ) {
        return ScrollPosition{ lastLine, 0 };
    }
    if ( !input_.textWrap ) {
        position.visualLineIndex = 0;
    }
    return position;
}

LinesCount ViewportLayout::visualLinesPerPage() const
{
    const int lines = input_.viewportHeightPx / charHeight();
    return LinesCount( static_cast<LinesCount::UnderlyingType>( std::max( lines, 1 ) ) );
}

ScrollPosition moveScrollPosition( ScrollPosition from, int64_t visualLines, ScrollPosition last,
                                   const VisualLineCounter& visualLineCount )
{
    const auto countOf = [ &visualLineCount ]( LineNumber line ) {
        return std::max( visualLineCount( line ), size_t{ 1 } );
    };

    auto position = std::min( from, last );

    if ( visualLines > 0 ) {
        auto remaining = static_cast<uint64_t>( visualLines );
        while ( remaining > 0 && position < last ) {
            const auto count = countOf( position.lineNumber );
            // Visual Lines of this Log Line still below the top row. None when
            // a re-wrap left the index past the end of the Log Line.
            const auto below = position.visualLineIndex + 1 < count
                                   ? count - 1 - position.visualLineIndex
                                   : size_t{ 0 };
            if ( remaining <= below ) {
                position.visualLineIndex += static_cast<size_t>( remaining );
                break;
            }
            remaining -= below + 1;
            position = ScrollPosition{ position.lineNumber + 1_lcount, 0 };
        }
    }
    else {
        auto remaining = static_cast<uint64_t>( -( visualLines + 1 ) ) + 1;
        while ( remaining > 0 && position > ScrollPosition{} ) {
            if ( remaining <= position.visualLineIndex ) {
                position.visualLineIndex -= static_cast<size_t>( remaining );
                break;
            }
            remaining -= position.visualLineIndex + 1;
            if ( position.lineNumber == 0_lnum ) {
                position.visualLineIndex = 0;
                break;
            }
            const auto previousLine = position.lineNumber - 1_lcount;
            position = ScrollPosition{ previousLine, countOf( previousLine ) - 1 };
        }
    }

    return std::min( position, last );
}

PullToFollowGeometry ViewportLayout::pullToFollowGeometry( const PullToFollowState& state ) const
{
    // Height including the partly hidden last Visual Line.
    const int wholeHeight = static_cast<int>( visibleLines().get() ) * charHeight();
    // How far the partly hidden last Visual Line reaches below the viewport.
    const int overhang = wholeHeight - input_.viewportHeightPx;
    const int elasticHeight = state.elasticHookLength / ElasticHookLengthPerPx;

    PullToFollowGeometry geometry;
    geometry.barHeightPx
        = elasticHeight + ( state.hooked ? overhang + PullToFollowHookedHeight : 0 );
    geometry.textTopPx = -geometry.barHeightPx;
    geometry.barTopPx = geometry.textTopPx + wholeHeight;

    if ( state.hooked && state.totalLines + LinesCount( 1 ) < visibleLines() ) {
        // Less than a screenful of Log Lines: show the Log File from the top
        // rather than pushing its first lines above the viewport, with the bar
        // at the bottom of the viewport.
        geometry.textTopPx += overhang + PullToFollowHookedHeight;
        geometry.barTopPx = geometry.textTopPx + input_.viewportHeightPx - PullToFollowHookedHeight;
    }
    else if ( state.lastLineAligned && !state.hooked ) {
        // On the extra slot at the end, aligned on the last Log Line.
        geometry.textTopPx -= overhang;
        geometry.barTopPx = geometry.textTopPx + wholeHeight;
    }

    return geometry;
}
