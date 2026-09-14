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

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "containers.h"
#include "linetypes.h"

// Where a Log Line sits in the Viewport, and what sits at a given pixel.
//
// This is a value: it is built from plain integers, it reads its inputs and it
// returns answers. It writes nothing -- in particular it never moves the view.
// It knows nothing about widgets, fonts or paint devices, which is what lets it
// answer before anything has ever been painted.
//
// All arithmetic is in characters: a column is charWidthPx wide and a Visual Line is
// charHeightPx tall. That holds exactly for the fixed-width fonts LogSquirl
// supports.
//
// Every computation here is bounded by the size of the viewport. Nothing in it
// is proportional to the number of Log Lines in the Log File.

// Where a text view stands in its Log File: the Log Line at the top of the
// Viewport, and which of its Visual Lines is shown first. Without text wrapping
// that is always the first one.
//
// The vertical scrollbar counts whole Log Lines (docs/adr/0001), so it knows
// only lineNumber; visualLineIndex is what lets every Visual Line of a Log Line
// taller than the Viewport be reached.
struct ScrollPosition {
    LineNumber lineNumber{ 0 };
    // Index, within lineNumber, of the Visual Line on the top row.
    size_t visualLineIndex = 0;

    bool operator==( const ScrollPosition& other ) const
    {
        return lineNumber == other.lineNumber && visualLineIndex == other.visualLineIndex;
    }

    // Ordered by Log Line, then by Visual Line: the order they appear in going
    // down the Log File.
    std::strong_ordering operator<=>( const ScrollPosition& other ) const
    {
        if ( const auto byLine = lineNumber.get() <=> other.lineNumber.get(); byLine != 0 ) {
            return byLine;
        }
        return visualLineIndex <=> other.visualLineIndex;
    }
};

// How many Visual Lines a Log Line wraps into.
using VisualLineCounter = std::function<size_t( LineNumber )>;

// The Scroll Position visualLines Visual Lines below from (above it when
// negative), kept between the top of the Log File and last.
//
// Every Visual Line passed counts once, whether or not it belongs to the same
// Log Line, so a step moves the same distance anywhere in the Log File.
// visualLineCount is asked only about the Log Lines the move passes over, which
// keeps the cost of a step bounded by its length, never by the Log File. A
// count of zero is taken as one.
ScrollPosition moveScrollPosition( ScrollPosition from, int64_t visualLines, ScrollPosition last,
                                   const VisualLineCounter& visualLineCount );

// One Visual Line of the Viewport: a whole Log Line when text wrapping is
// off, one wrapped part of a Log Line when it is on.
struct VisualLine {
    // The Log Line this Visual Line shows part of.
    LineNumber lineNumber{ 0 };
    // Index of this Visual Line within its Log Line (0 without wrapping).
    size_t wrappedLineIndex = 0;
    // Display column of the Log Line at which this Visual Line starts.
    LineColumn firstColumn{ 0 };
    // Number of display columns this Visual Line holds.
    LineLength length{ 0 };
    // Number of display columns of the whole Log Line.
    LineLength lineLength{ 0 };

    bool operator==( const VisualLine& other ) const
    {
        return lineNumber == other.lineNumber && wrappedLineIndex == other.wrappedLineIndex
               && firstColumn == other.firstColumn && length == other.length
               && lineLength == other.lineLength;
    }
};

using VisualLines = logsquirl::vector<VisualLine>;

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
    // Where the view stands. The first Visual Line laid out is the one it
    // names, so visualLines() starts there.
    ScrollPosition scrollPosition;
    // First display column shown at the left edge (text wrapping off only).
    LineColumn firstColumn{ 0 };
    bool lineNumbersVisible = false;
    // The largest line number that can be displayed; it fixes the width of the
    // line number area.
    LineNumber::UnderlyingType largestDisplayLineNumber = 0;
    bool textWrap = false;
    // Vertical offset (pixels) at which the first Visual Line is drawn. Negative when
    // the view is pulled up (last line aligned, or pull to follow).
    int drawingTopOffsetPx = 0;
};

// The follow mode state the pull-to-follow geometry depends on.
struct PullToFollowState {
    // How far the elastic hook has been pulled, in its own units.
    int elasticHookLength = 0;
    // Whether the elastic hook has hooked, i.e. follow mode is about to engage.
    bool hooked = false;
    // Whether the bottom of the last Log Line is aligned with the bottom of the
    // viewport rather than the top of the first one with its top.
    bool lastLineAligned = false;
    LinesCount totalLines{ 0 };
};

// Where the text and the pull-to-follow bar are drawn, in viewport pixels.
struct PullToFollowGeometry {
    // Height of the pull-to-follow bar, 0 when there is none to draw.
    int barHeightPx = 0;
    // Vertical offset of the first Visual Line; what drawingTopOffsetPx is set to.
    int textTopPx = 0;
    // Top of the pull-to-follow bar.
    int barTopPx = 0;

    bool operator==( const PullToFollowGeometry& ) const = default;
};

class ViewportLayout {
public:
    // Margin geometry. One definition, used by hit testing, the scrollbars and
    // painting alike.
    static constexpr int SeparatorWidth = 1;
    static constexpr int BulletAreaWidth = 11;
    static constexpr int ContentMarginWidth = 1;
    static constexpr int LineNumberPadding = 3;
    // Extra height of the pull-to-follow bar once the elastic hook has hooked.
    static constexpr int PullToFollowHookedHeight = 10;

    explicit ViewportLayout( ViewportLayoutInput input, VisualLines visualLines = {} );

    const ViewportLayoutInput& input() const
    {
        return input_;
    }

    const VisualLines& visualLines() const
    {
        return visualLines_;
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
    // Pixel column of the first character of a Visual Line.
    int textOriginX() const;

    // --- visible counts ------------------------------------------------

    // Visual Lines with any part in the Viewport, the partly hidden last one
    // included. This is how many Visual Lines the layout holds at most.
    LinesCount visibleLines() const;
    LineLength visibleColumns() const;
    // Visual Lines a page moves: the ones that fit wholly in the Viewport
    // height, and at least one.
    LinesCount visualLinesPerPage() const;

    // --- hit testing ---------------------------------------------------

    // Index into visualLines() of the Visual Line at yPos, or nothing if outside.
    std::optional<size_t> visualLineAtPoint( int yPos ) const;
    // The Log Line at yPos, or nothing if no Visual Line lives there.
    OptionalLineNumber lineAtPoint( int yPos ) const;
    // The position in the Log File at a viewport point. Always inside the
    // known Visual Lines; returns the first position when there are none.
    FilePosition filePositionAtPoint( int xPos, int yPos ) const;

    // --- rectangles ----------------------------------------------------

    // The full-width band a Log Line occupies (all its Visual Lines).
    ViewportRect rectForLine( LineNumber line ) const;
    // The single character cell of a display column of a Log Line.
    ViewportRect rectForColumn( LineNumber line, LineColumn column ) const;

    // --- scroll ranges -------------------------------------------------

    // Maximum value of the vertical scrollbar.
    // bottomWrappedVisibleLines is how many Visual Lines the last screenful of Log
    // Lines needs once wrapped; it equals visibleLines() without wrapping.
    int verticalScrollRange( LinesCount totalLines, LinesCount bottomWrappedVisibleLines ) const;
    // Maximum value of the horizontal scrollbar.
    int horizontalScrollRange( LineLength maxLineLength ) const;
    // The largest Scroll Position whose unwrapped Log Lines still fill the
    // viewport.
    ScrollPosition lastValidScrollPosition( LinesCount totalLines ) const;
    // position, brought back into the Log Lines the Log File actually has. A
    // Scroll Position moved to another Log Line starts at its first Visual
    // Line, and without text wrapping every Scroll Position does. Whether the
    // Visual Line exists is not known here: that needs the Log Line wrapped.
    ScrollPosition clampScrollPosition( ScrollPosition position, LinesCount totalLines ) const;

    // --- pull to follow ------------------------------------------------

    // Where the text and the pull-to-follow bar go. The one definition that
    // painting and hit testing both read, so they cannot place the text at
    // different positions. Independent of drawingTopOffsetPx, which is derived
    // from it.
    PullToFollowGeometry pullToFollowGeometry( const PullToFollowState& state ) const;

private:
    // Never zero: a degenerate font metric must not divide by zero, and a
    // one-pixel cell keeps every answer finite and in range.
    int charWidth() const;
    int charHeight() const;

    ViewportLayoutInput input_;
    VisualLines visualLines_;
};

// Number of decimal digits of x (x == 0 counts as one digit).
int countLineNumberDigits( uint64_t x );

#endif
