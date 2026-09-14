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

#ifndef LOG_VIEW_SCROLLING_H
#define LOG_VIEW_SCROLLING_H

// Scrolling a text view by Visual Lines, checked the same way for the main
// view and the Filtered View (#153).
//
// The view is shown one column wide, so every character of a Log Line is one
// Visual Line whatever font the platform picks, and every expected Scroll
// Position follows from the text alone.

#include <catch2/catch.hpp>

#include <cstdint>

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStringList>
#include <QWheelEvent>

#include "abstractlogview.h"
#include "viewportlayout.h"

namespace logviewscrolling {

// A Log Line of TallLineVisualLines characters, between Log Lines of one.
constexpr uint64_t LinesBeforeTallLine = 10;
constexpr uint64_t TallLineVisualLines = 300;
constexpr uint64_t LinesAfterTallLine = 100;
inline const LineNumber TallLine{ LinesBeforeTallLine };

inline QStringList tallLogLines()
{
    QStringList lines;
    for ( uint64_t i = 0; i < LinesBeforeTallLine; ++i ) {
        lines << QStringLiteral( "a" );
    }
    lines << QString( static_cast<qsizetype>( TallLineVisualLines ), QLatin1Char( 'x' ) );
    for ( uint64_t i = 0; i < LinesAfterTallLine; ++i ) {
        lines << QStringLiteral( "b" );
    }
    return lines;
}

// How many Visual Lines of tallLogLines() are above position.
inline uint64_t visualLinesAbove( ScrollPosition position )
{
    const auto line = position.lineNumber.get();
    if ( line <= LinesBeforeTallLine ) {
        return line + position.visualLineIndex;
    }
    return LinesBeforeTallLine + TallLineVisualLines + ( line - LinesBeforeTallLine - 1 )
           + position.visualLineIndex;
}

// Shows view with text wrapping in a Viewport one column wide, and lower than
// the tall Log Line: the margins plus seven pixels, too few for two columns of
// any font at least four pixels wide.
inline void showOneColumnWide( AbstractLogView& view )
{
    view.setFrameShape( QFrame::NoFrame );
    view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.resize( ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth + 7, 200 );
    view.show();
    QCoreApplication::processEvents();
    view.updateData();
}

// A point on the text of the top row.
inline QPointF topRowText()
{
    return QPointF{ ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth + 2, 1 };
}

inline void turnWheel( AbstractLogView& view, int angleDeltaY )
{
    const auto inside = topRowText();
    QWheelEvent wheel( inside, view.viewport()->mapToGlobal( inside ), QPoint{},
                       QPoint{ 0, angleDeltaY }, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                       false );
    QCoreApplication::sendEvent( view.viewport(), &wheel );
}

inline void pressKey( AbstractLogView& view, Qt::Key key )
{
    QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
    QCoreApplication::sendEvent( &view, &press );
}

inline void doubleClickTopRow( AbstractLogView& view )
{
    const auto onText = topRowText();
    QMouseEvent click( QEvent::MouseButtonDblClick, onText, view.viewport()->mapToGlobal( onText ),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &click );
}

// Moves view to position: the scrollbar to its Log Line, then the arrow key
// down its Visual Lines.
inline void moveTo( AbstractLogView& view, ScrollPosition position )
{
    const auto line = static_cast<int>( position.lineNumber.get() );
    view.verticalScrollBar()->setValue( line == 0 ? 1 : 0 );
    view.verticalScrollBar()->setValue( line );
    for ( size_t step = 0; step < position.visualLineIndex; ++step ) {
        pressKey( view, Qt::Key_Down );
    }
    REQUIRE( view.scrollPosition() == position );
}

// Three Visual Lines per notch of the wheel for as long as this lives,
// whatever the platform is set to.
class PinnedWheelScrollLines {
public:
    PinnedWheelScrollLines()
        : previous_( QApplication::wheelScrollLines() )
    {
        QApplication::setWheelScrollLines( Lines );
    }
    ~PinnedWheelScrollLines()
    {
        QApplication::setWheelScrollLines( previous_ );
    }

    PinnedWheelScrollLines( const PinnedWheelScrollLines& ) = delete;
    PinnedWheelScrollLines& operator=( const PinnedWheelScrollLines& ) = delete;

    static constexpr int Lines = 3;

private:
    int previous_;
};

inline void requireWheelStepsMoveTheSameVisualLinesEverywhere( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;
    moveTo( view, ScrollPosition{} );

    bool reachedLastVisualLineOfTallLine = false;
    while ( view.scrollPosition().lineNumber.get() <= LinesBeforeTallLine + 5 ) {
        const auto before = view.scrollPosition();
        turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
        const auto after = view.scrollPosition();
        INFO( "down from " << before.lineNumber.get() << ":" << before.visualLineIndex << " to "
                           << after.lineNumber.get() << ":" << after.visualLineIndex );
        REQUIRE( visualLinesAbove( after )
                 == visualLinesAbove( before ) + PinnedWheelScrollLines::Lines );
        reachedLastVisualLineOfTallLine
            = reachedLastVisualLineOfTallLine
              || after == ScrollPosition{ TallLine, TallLineVisualLines - 1 };
    }
    REQUIRE( reachedLastVisualLineOfTallLine );

    while ( view.scrollPosition() != ScrollPosition{} ) {
        const auto before = view.scrollPosition();
        turnWheel( view, QWheelEvent::DefaultDeltasPerStep );
        const auto after = view.scrollPosition();
        INFO( "up from " << before.lineNumber.get() << ":" << before.visualLineIndex << " to "
                         << after.lineNumber.get() << ":" << after.visualLineIndex );
        REQUIRE( visualLinesAbove( after ) + PinnedWheelScrollLines::Lines
                 == visualLinesAbove( before ) );
    }
}

inline void requireArrowKeysStepOneVisualLineAcrossLogLines( AbstractLogView& view )
{
    const ScrollPosition lastOfTallLine{ TallLine, TallLineVisualLines - 1 };
    moveTo( view, lastOfTallLine );

    pressKey( view, Qt::Key_Down );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine + 1_lcount, 0 } );

    pressKey( view, Qt::Key_Up );
    REQUIRE( view.scrollPosition() == lastOfTallLine );
}

inline void requirePageDownThenPageUpReturns( AbstractLogView& view )
{
    for ( const auto start : { ScrollPosition{ 5_lnum, 0 }, ScrollPosition{ TallLine, 150 },
                               ScrollPosition{ TallLine, TallLineVisualLines - 2 } } ) {
        INFO( "from " << start.lineNumber.get() << ":" << start.visualLineIndex );
        moveTo( view, start );

        pressKey( view, Qt::Key_PageDown );
        REQUIRE( view.scrollPosition() > start );

        pressKey( view, Qt::Key_PageUp );
        REQUIRE( view.scrollPosition() == start );
    }
}

inline void requireScrollbarCountsWholeLogLines( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;
    auto* scrollBar = view.verticalScrollBar();
    const auto tallLineValue = static_cast<int>( TallLine.get() );

    moveTo( view, ScrollPosition{ TallLine, 150 } );
    REQUIRE( scrollBar->value() == tallLineValue );

    // Inside one Log Line the scrollbar stays, and does not pull the view back
    // to that Log Line's first Visual Line.
    pressKey( view, Qt::Key_Down );
    turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    QCoreApplication::processEvents();
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 154 } );
    REQUIRE( scrollBar->value() == tallLineValue );

    // Dragging the thumb lands on the first Visual Line of a Log Line.
    scrollBar->setSliderPosition( 20 );
    REQUIRE( view.scrollPosition() == ScrollPosition{ 20_lnum, 0 } );
    scrollBar->setSliderPosition( tallLineValue );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 0 } );
}

inline void requireWrapToggleKeepsTheLogLineAtTheTop( AbstractLogView& view )
{
    moveTo( view, ScrollPosition{ TallLine, 150 } );

    view.textWrapSet( false );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 0 } );
    REQUIRE( view.getTopLine() == TallLine );

    view.textWrapSet( true );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 0 } );
}

} // namespace logviewscrolling

#endif // LOG_VIEW_SCROLLING_H
