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

#ifndef VIEWPORTLAYOUT_H
#define VIEWPORTLAYOUT_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "containers.h"
#include "linetypes.h"

// Where a Log Line sits on screen, and what sits at a given pixel.
//
// This is a value: it is built from plain integers, it reads its inputs and it
// returns answers. It writes nothing -- in particular it never moves the view.
// It knows nothing about widgets, fonts or paint devices, which is what lets it
// answer before anything has ever been painted.
//
// All arithmetic is in characters: a column is charWidthPx wide and a row is
// charHeightPx tall. That holds exactly for the fixed-width fonts LogSquirl
// supports.
//
// Every computation here is bounded by the size of the viewport. Nothing in it
// is proportional to the number of Log Lines in the Log File.

// One displayed row of the viewport: a whole Log Line when text wrapping is
// off, one wrapped fragment of a Log Line when it is on.
struct ViewportRow {
    // The Log Line this row shows part of.
    LineNumber lineNumber{ 0 };
    // Index of this fragment within that Log Line (0 without wrapping).
    uint32_t wrappedLineIndex = 0;
    // Display column of the Log Line at which this row starts.
    LineColumn firstColumn{ 0 };
    // Number of display columns this row holds.
    LineLength length{ 0 };
    // Number of display columns of the whole Log Line.
    LineLength lineLength{ 0 };

    bool operator==( const ViewportRow& other ) const
    {
        return lineNumber == other.lineNumber && wrappedLineIndex == other.wrappedLineIndex
               && firstColumn == other.firstColumn && length == other.length
               && lineLength == other.lineLength;
    }
};

using ViewportRows = logsquirl::vector<ViewportRow>;

// A rectangle in viewport pixels. Deliberately not a QRect: the layout has no
// Qt in its interface.
struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==( const ViewportRect& other ) const
    {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

// Everything the layout needs, as plain integers.
struct ViewportLayoutInput {
    int charWidthPx = 1;
    int charHeightPx = 1;
    int viewportWidthPx = 0;
    int viewportHeightPx = 0;
    // First Log Line shown at the top of the viewport.
    LineNumber firstLine{ 0 };
    // First display column shown at the left edge (text wrapping off only).
    LineColumn firstColumn{ 0 };
    bool lineNumbersVisible = false;
    // The largest line number that can be displayed; it fixes the width of the
    // line number area.
    LineNumber::UnderlyingType largestDisplayLineNumber = 0;
    bool textWrap = false;
    // Vertical offset (pixels) at which the first row is drawn. Negative when
    // the view is pulled up (last line aligned, or pull to follow).
    int drawingTopOffsetPx = 0;
};

class ViewportLayout {
public:
    // Margin geometry. One definition, used by hit testing, the scrollbars and
    // painting alike.
    static constexpr int SeparatorWidth = 1;
    static constexpr int BulletAreaWidth = 11;
    static constexpr int ContentMarginWidth = 1;
    static constexpr int LineNumberPadding = 3;

    explicit ViewportLayout( ViewportLayoutInput input, ViewportRows rows = {} );

    const ViewportLayoutInput& input() const
    {
        return input_;
    }

    const ViewportRows& rows() const
    {
        return rows_;
    }

    // --- margins -------------------------------------------------------

    // Number of digits reserved for a line number.
    int lineNumberDigits() const;
    // Width of the line number area, 0 when line numbers are hidden.
    int lineNumberAreaWidthPx() const;
    // Left edge of the line number area, 0 when line numbers are hidden.
    int lineNumberAreaStartX() const;
    // Width of the bullet zone, including its separator. A click left of this
    // marks a Log Line.
    int bulletZoneWidthPx() const;
    // Pixel column at which line content starts.
    int contentStartPosX() const;
    // Total width of all margins and decorations.
    int leftMarginPx() const;
    // Pixel column of the first character of a row.
    int textOriginX() const;

    // --- visible counts ------------------------------------------------

    LinesCount visibleLines() const;
    LineLength visibleColumns() const;

    // --- hit testing ---------------------------------------------------

    // Index into rows() of the row at yPos, or nothing if outside.
    std::optional<size_t> rowAtPoint( int yPos ) const;
    // The Log Line at yPos, or nothing if no row lives there.
    OptionalLineNumber lineAtPoint( int yPos ) const;
    // The position in the Log File at a viewport point. Always inside the
    // known rows; returns the first position when there are none.
    FilePosition filePositionAtPoint( int xPos, int yPos ) const;

    // --- rectangles ----------------------------------------------------

    // The full-width band a Log Line occupies (all its wrapped rows).
    ViewportRect rectForLine( LineNumber line ) const;
    // The single character cell of a display column of a Log Line.
    ViewportRect rectForColumn( LineNumber line, LineColumn column ) const;

    // --- scroll ranges -------------------------------------------------

    // Maximum value of the vertical scrollbar.
    // bottomWrappedVisibleLines is how many rows the last screenful of Log
    // Lines needs once wrapped; it equals visibleLines() without wrapping.
    int verticalScrollRange( LinesCount totalLines, LinesCount bottomWrappedVisibleLines ) const;
    // Maximum value of the horizontal scrollbar.
    int horizontalScrollRange( LineLength maxLineLength ) const;
    // The largest first visible line that still fills the viewport.
    LineNumber lastValidFirstLine( LinesCount totalLines ) const;
    // line, brought back into the range the Log File actually has.
    LineNumber clampFirstLine( LineNumber line, LinesCount totalLines ) const;

private:
    // Never zero: a degenerate font metric must not divide by zero, and a
    // one-pixel cell keeps every answer finite and in range.
    int charWidth() const;
    int charHeight() const;

    ViewportLayoutInput input_;
    ViewportRows rows_;
};

// Number of decimal digits of x (x == 0 counts as one digit).
int countLineNumberDigits( uint64_t x );

#endif
