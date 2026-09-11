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

ViewportLayout::ViewportLayout( ViewportLayoutInput input, ViewportRows rows )
    : input_{ input }
    , rows_{ std::move( rows ) }
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

std::optional<size_t> ViewportLayout::rowAtPoint( int yPos ) const
{
    const auto offset = std::abs( ( yPos - input_.drawingTopOffsetPx ) / charHeight() );
    const auto index = static_cast<size_t>( offset );
    if ( index < rows_.size() ) {
        return index;
    }
    return std::nullopt;
}

OptionalLineNumber ViewportLayout::lineAtPoint( int yPos ) const
{
    const auto row = rowAtPoint( yPos );
    if ( !row.has_value() ) {
        return OptionalLineNumber{};
    }
    return rows_[ *row ].lineNumber;
}

FilePosition ViewportLayout::filePositionAtPoint( int xPos, int yPos ) const
{
    if ( rows_.empty() ) {
        return FilePosition{ 0_lnum, 0_lcol };
    }

    const auto offset = std::abs( ( yPos - input_.drawingTopOffsetPx ) / charHeight() );
    const auto rowIndex = rows_.size() > 1 ? std::clamp( static_cast<size_t>( offset ), size_t{ 0 },
                                                         rows_.size() - 1 )
                                           : size_t{ 0 };

    const auto& row = rows_[ rowIndex ];

    if ( row.lineLength.get() <= 1 ) {
        return FilePosition{ row.lineNumber, 0_lcol };
    }

    // Number of columns of this row that are actually on screen.
    const auto visibleTextLength
        = input_.textWrap ? row.length.get()
                          : std::clamp( row.lineLength.get() - input_.firstColumn.get(),
                                        LineLength::UnderlyingType{ 0 }, visibleColumns().get() );

    // The first column whose right edge is at or past xPos, then step back one
    // to land on the column the pixel is actually inside.
    const auto firstColumnPastX = std::clamp<int64_t>(
        ceilDiv( xPos - leftMarginPx(), charWidth() ), 0, visibleTextLength );

    auto column
        = LineColumn{ static_cast<LineColumn::UnderlyingType>( firstColumnPastX ) } - 1_length;

    // Move from the row's own columns to the Log Line's columns.
    column += input_.textWrap ? LineLength{ row.firstColumn.get() }
                              : LineLength{ input_.firstColumn.get() };

    const auto maxColumn = LineColumn{ row.lineLength.get() } - 1_length;
    column = std::clamp( column, 0_lcol, maxColumn );

    return FilePosition{ row.lineNumber, column };
}

ViewportRect ViewportLayout::rectForLine( LineNumber line ) const
{
    const auto first = std::find_if( rows_.begin(), rows_.end(), [ line ]( const ViewportRow& r ) {
        return r.lineNumber == line;
    } );
    if ( first == rows_.end() ) {
        return ViewportRect{};
    }

    const auto rowCount = static_cast<int>( std::count_if(
        first, rows_.end(), [ line ]( const ViewportRow& r ) { return r.lineNumber == line; } ) );

    const auto firstIndex = static_cast<int>( std::distance( rows_.begin(), first ) );
    return ViewportRect{ 0, input_.drawingTopOffsetPx + firstIndex * charHeight(),
                         input_.viewportWidthPx, rowCount * charHeight() };
}

ViewportRect ViewportLayout::rectForColumn( LineNumber line, LineColumn column ) const
{
    for ( size_t index = 0; index < rows_.size(); ++index ) {
        const auto& row = rows_[ index ];
        if ( row.lineNumber != line ) {
            continue;
        }

        const auto rowFirst = row.firstColumn;
        const auto rowLast = rowFirst + row.length;
        if ( input_.textWrap && !( column >= rowFirst && column < rowLast ) ) {
            continue;
        }

        const auto columnInRow = input_.textWrap
                                     ? ( column - LineLength{ rowFirst.get() } ).get()
                                     : ( column - LineLength{ input_.firstColumn.get() } ).get();

        return ViewportRect{ textOriginX() + static_cast<int>( columnInRow ) * charWidth(),
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
    // Log Lines; the extra rows have to be reachable.
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

LineNumber ViewportLayout::lastValidFirstLine( LinesCount totalLines ) const
{
    const auto visible = visibleLines();
    if ( totalLines.get() <= visible.get() ) {
        return 0_lnum;
    }
    return LineNumber( totalLines.get() - visible.get() );
}

LineNumber ViewportLayout::clampFirstLine( LineNumber line, LinesCount totalLines ) const
{
    if ( totalLines.get() == 0 ) {
        return 0_lnum;
    }
    const auto lastLine = LineNumber( totalLines.get() - 1 );
    return line > lastLine ? lastLine : line;
}
