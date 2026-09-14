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

// Scrolling a text view by Visual Lines, and its bottom, checked the same way
// for the main view and the Filtered View (#153, #154).
//
// The view is shown one column wide, so every character of a Log Line is one
// Visual Line whatever font the platform picks, and every expected Scroll
// Position follows from the text alone.
//
// What is on a row of the Viewport is found by double-clicking it: the word
// under the click gets selected. A Log Line that ends in "x z" has a space as
// the Visual Line above its last one, so a word on the last row is exact to
// the row whatever the font.

#include <catch2/catch.hpp>

#include <cstdint>
#include <functional>

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

// The word the Log Files below end in.
inline const QString LastWord = QStringLiteral( "z" );

// Log Lines of one Visual Line after the tall one, the last of them LastWord.
inline QStringList tallLogLines()
{
    QStringList lines;
    for ( uint64_t i = 0; i < LinesBeforeTallLine; ++i ) {
        lines << QStringLiteral( "a" );
    }
    lines << QString( static_cast<qsizetype>( TallLineVisualLines ), QLatin1Char( 'x' ) );
    for ( uint64_t i = 1; i < LinesAfterTallLine; ++i ) {
        lines << QStringLiteral( "b" );
    }
    lines << LastWord;
    return lines;
}

// A Log Line of 301 Visual Lines, far taller than the Viewport: "x x ... x"
// ending in lastWord.
inline QString tallLineEndingIn( const QString& lastWord )
{
    return QStringLiteral( "x " ).repeated( 150 ) + lastWord;
}

// A Log File whose last Log Line is taller than the Viewport.
inline QStringList tallLastLogLines()
{
    QStringList lines;
    for ( uint64_t i = 0; i < LinesBeforeTallLine; ++i ) {
        lines << QStringLiteral( "a" );
    }
    lines << tallLineEndingIn( LastWord );
    return lines;
}

// Fewer Log Lines than the Viewport has rows, one of them taller than the Viewport.
inline QStringList fewLogLinesOneTallerThanTheViewport()
{
    return QStringList{ QStringLiteral( "a" ), tallLineEndingIn( QStringLiteral( "y" ) ),
                        LastWord };
}

// Four Visual Lines: fewer than any Viewport 200 px high has rows.
inline QStringList fewerVisualLinesThanRows()
{
    return QStringList{ QStringLiteral( "a" ), QStringLiteral( "bb" ), LastWord };
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

// --- the bottom (#154) ----------------------------------------------------

// The word on the Viewport row at y, as double-clicking it selects it. When
// there is no word there -- a space, or no Visual Line at all -- the whole Log
// File stays selected instead.
inline QString wordAtRow( AbstractLogView& view, int y )
{
    view.selectAll();
    const QPointF onText{ topRowText().x(), static_cast<qreal>( y ) };
    QMouseEvent click( QEvent::MouseButtonDblClick, onText, view.viewport()->mapToGlobal( onText ),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &click );
    return view.getSelectedText();
}

inline QString wordOnLastRow( AbstractLogView& view )
{
    return wordAtRow( view, view.viewport()->height() - 1 );
}

// The row right above the bar follow mode hooks at the bottom of the Viewport.
inline QString wordAbovePullToFollowBar( AbstractLogView& view )
{
    return wordAtRow( view,
                      view.viewport()->height() - 1 - ViewportLayout::PullToFollowHookedHeight );
}

// Drags the thumb to the top, then all the way down.
inline void dragToScrollbarMaximum( AbstractLogView& view )
{
    auto* scrollBar = view.verticalScrollBar();
    scrollBar->setSliderPosition( 0 );
    scrollBar->setSliderPosition( scrollBar->maximum() );
}

inline void requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( AbstractLogView& view )
{
    auto* scrollBar = view.verticalScrollBar();
    REQUIRE( scrollBar->maximum() > 0 );

    dragToScrollbarMaximum( view );

    // The Log Line of the bottom Scroll Position, and on the last row, the
    // last Visual Line of the Log File.
    REQUIRE( view.scrollPosition().lineNumber.get()
             == static_cast<uint64_t>( scrollBar->maximum() ) );
    REQUIRE( wordOnLastRow( view ) == LastWord );
}

inline void requireScrollingStopsAtTheBottom( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;
    dragToScrollbarMaximum( view );
    const auto bottom = view.scrollPosition();

    pressKey( view, Qt::Key_Down );
    REQUIRE( view.scrollPosition() == bottom );
    pressKey( view, Qt::Key_PageDown );
    REQUIRE( view.scrollPosition() == bottom );
    REQUIRE( wordOnLastRow( view ) == LastWord );

    turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    REQUIRE( view.scrollPosition() == bottom );
}

inline void requireScrollingDownFromTheTopReachesTheBottom( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;

    moveTo( view, ScrollPosition{} );
    for ( int page = 0; page < 1000; ++page ) {
        const auto before = view.scrollPosition();
        pressKey( view, Qt::Key_PageDown );
        if ( view.scrollPosition() == before ) {
            break;
        }
    }
    const auto reachedByPages = view.scrollPosition();
    REQUIRE( wordOnLastRow( view ) == LastWord );

    moveTo( view, ScrollPosition{} );
    for ( int notch = 0; notch < 1000 && view.scrollPosition() != reachedByPages; ++notch ) {
        turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    }
    REQUIRE( view.scrollPosition() == reachedByPages );

    dragToScrollbarMaximum( view );
    REQUIRE( view.scrollPosition() == reachedByPages );
}

inline void requireFewerVisualLinesThanRowsShowFromTheTop( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;

    REQUIRE( view.verticalScrollBar()->maximum() == 0 );
    REQUIRE( view.scrollPosition() == ScrollPosition{} );

    pressKey( view, Qt::Key_PageDown );
    turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    REQUIRE( view.scrollPosition() == ScrollPosition{} );
    REQUIRE( wordAtRow( view, 1 ) == QStringLiteral( "a" ) );
}

// grow adds to the Log Lines the view shows, ending them in newLastWord, and
// tells the view.
inline void requireFollowKeepsTheLastVisualLineOnTheLastRow( AbstractLogView& view,
                                                             const std::function<void()>& grow,
                                                             const QString& newLastWord )
{
    view.followSet( true );
    grow();
    const auto followed = view.scrollPosition();

    // Follow mode hooks the pull-to-follow bar under the last Visual Line.
    REQUIRE( wordAbovePullToFollowBar( view ) == newLastWord );

    // Where it follows to is the bottom Scroll Position.
    view.followSet( false );
    dragToScrollbarMaximum( view );
    REQUIRE( view.scrollPosition() == followed );
    REQUIRE( wordOnLastRow( view ) == newLastWord );
}

// grow adds Log Lines below the last one the view shows, and tells the view.
inline void requireGrowthWithoutFollowLeavesTheBottomView( AbstractLogView& view,
                                                           const std::function<void()>& grow )
{
    dragToScrollbarMaximum( view );
    const auto before = view.scrollPosition();
    const auto lastWordBefore = wordOnLastRow( view );

    grow();

    REQUIRE( view.scrollPosition() == before );
    REQUIRE( wordOnLastRow( view ) == lastWordBefore );
    REQUIRE( view.verticalScrollBar()->maximum() > static_cast<int>( before.lineNumber.get() ) );
}

} // namespace logviewscrolling

#endif // LOG_VIEW_SCROLLING_H
