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

// Scrolling a text view by Visual Lines, its bottom, re-wrapping it and jumping
// in it, checked the same way for the main view and the Filtered View (#153,
// #154, #155). The Log Files these run on are in log_view_log_files.h.
//
// Input is simulated: the wheel, the keys, the mouse and the scrollbar all go
// through the widget. What sits on a row of the Viewport is then asked of the
// view's own Viewport layout, the one its hit testing and its painting read.
// Nothing here re-derives where a Log Line is drawn.

#include <catch2/catch.hpp>

#include <cstdint>
#include <functional>

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QWheelEvent>

#include "abstractlogview.h"
#include "log_view_log_files.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "viewportlayout.h"

namespace logviewscrolling {

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

    // What the application hands a view it builds, so that scrolling here
    // behaves as it does there. A view reads no setting of its own.
    view.setPresentationPolicy( testSettingsPolicies().presentation );

    view.updateData();
}

// The Viewport's top row.
constexpr int TopRowY = 1;

// The Viewport's last row.
inline int lastRowY( const AbstractLogView& view )
{
    return view.viewport()->height() - 1;
}

// A point on the text of the top row.
inline QPointF topRowText()
{
    return QPointF{ ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth + 2,
                    TopRowY };
}

// --- what the view says sits where ----------------------------------------

// The Visual Line the view draws on the Viewport row at yPos, as its own
// Viewport layout places it. Every row asked about below shows text.
inline VisualLine visualLineAtRow( const AbstractLogView& view, int yPos )
{
    const auto layout = view.viewportLayout();
    const auto onRow = layout.visualLineAtPoint( yPos );
    REQUIRE( onRow.has_value() );
    return layout.visualLines()[ *onRow ];
}

// The display column of its Log Line the Viewport's top row starts at.
inline LineColumn topRowColumn( const AbstractLogView& view )
{
    return visualLineAtRow( view, TopRowY ).firstColumn;
}

// Requires that the Viewport row at yPos holds display column column: the
// character that was there is still on that row, wherever along it a re-wrap
// has moved it.
inline void requireRowHoldsColumn( const AbstractLogView& view, int yPos, LineColumn column )
{
    const auto row = visualLineAtRow( view, yPos );
    INFO( "row " << yPos << " holds columns " << row.firstColumn.get() << " up to "
                 << ( row.firstColumn + row.length ).get() << ", looking for " << column.get() );
    REQUIRE( row.firstColumn <= column );
    REQUIRE( column < row.firstColumn + row.length );
}

// Requires that the Viewport row at yPos shows the end of the Log File: the
// Visual Line holding the last character of its last Log Line.
//
// logLines is what the Log File holds now. The Viewport layout cannot know it:
// it is bounded by the Viewport and counts no Visual Lines per Log File
// (docs/adr/0001), so the Log File itself is asked.
inline void requireLogFileEndsOnRow( const AbstractLogView& view, int yPos, LinesCount logLines )
{
    const auto row = visualLineAtRow( view, yPos );
    REQUIRE( row.lineNumber == LineNumber( logLines.get() - 1 ) );
    REQUIRE( row.firstColumn + row.length == LineColumn( row.lineLength.get() ) );
}

// --- simulated input ------------------------------------------------------

// Turns the wheel one notch of angleDeltaY over the top row. modifiers are the
// keys held while turning it -- the fast scroll modifier among them.
inline void turnWheel( AbstractLogView& view, int angleDeltaY,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    const auto inside = topRowText();
    QWheelEvent wheel( inside, view.viewport()->mapToGlobal( inside ), QPoint{},
                       QPoint{ 0, angleDeltaY }, Qt::NoButton, modifiers, Qt::NoScrollPhase,
                       false );
    QCoreApplication::sendEvent( view.viewport(), &wheel );
}

inline void pressKey( AbstractLogView& view, Qt::Key key )
{
    QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
    QCoreApplication::sendEvent( &view, &press );
}

// The same key pressed times over.
inline void pressKey( AbstractLogView& view, Qt::Key key, int times )
{
    for ( int step = 0; step < times; ++step ) {
        pressKey( view, key );
    }
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
    pressKey( view, Qt::Key_Down, static_cast<int>( position.visualLineIndex ) );
    REQUIRE( view.scrollPosition() == position );
}

// Drags the thumb to the top, then all the way down.
inline void dragToScrollbarMaximum( AbstractLogView& view )
{
    auto* scrollBar = view.verticalScrollBar();
    scrollBar->setSliderPosition( 0 );
    scrollBar->setSliderPosition( scrollBar->maximum() );
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

// --- scrolling by Visual Lines (#153) -------------------------------------

// Requires that the notch just turned, from before to after, moved the view as
// far as its own Visual Lines of arrow key: back over it, and forward again.
// The yardstick is the view's single-Visual-Line step, not arithmetic of ours.
inline void requireNotchMatchesArrowKeys( AbstractLogView& view, ScrollPosition before,
                                          ScrollPosition after, Qt::Key back, Qt::Key forward )
{
    pressKey( view, back, PinnedWheelScrollLines::Lines );
    REQUIRE( view.scrollPosition() == before );
    pressKey( view, forward, PinnedWheelScrollLines::Lines );
    REQUIRE( view.scrollPosition() == after );
}

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
        requireNotchMatchesArrowKeys( view, before, after, Qt::Key_Up, Qt::Key_Down );
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
        requireNotchMatchesArrowKeys( view, before, after, Qt::Key_Down, Qt::Key_Up );
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

inline void requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( AbstractLogView& view,
                                                                       LinesCount logLines )
{
    auto* scrollBar = view.verticalScrollBar();
    REQUIRE( scrollBar->maximum() > 0 );

    dragToScrollbarMaximum( view );

    // The Log Line of the bottom Scroll Position, and on the last row, the
    // last Visual Line of the Log File.
    REQUIRE( view.scrollPosition().lineNumber.get()
             == static_cast<uint64_t>( scrollBar->maximum() ) );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );
}

inline void requireScrollingStopsAtTheBottom( AbstractLogView& view, LinesCount logLines )
{
    const PinnedWheelScrollLines pinned;
    dragToScrollbarMaximum( view );
    const auto bottom = view.scrollPosition();

    pressKey( view, Qt::Key_Down );
    REQUIRE( view.scrollPosition() == bottom );
    pressKey( view, Qt::Key_PageDown );
    REQUIRE( view.scrollPosition() == bottom );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );

    turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    REQUIRE( view.scrollPosition() == bottom );
}

inline void requireScrollingDownFromTheTopReachesTheBottom( AbstractLogView& view,
                                                            LinesCount logLines )
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
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );

    moveTo( view, ScrollPosition{} );
    for ( int notch = 0; notch < 1000 && view.scrollPosition() != reachedByPages; ++notch ) {
        turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    }
    REQUIRE( view.scrollPosition() == reachedByPages );

    dragToScrollbarMaximum( view );
    REQUIRE( view.scrollPosition() == reachedByPages );
}

// The view, one column wide and showing tallLastLogLines().
inline void requireScrollbarMovedToItsMaximumLandsAtTheBottom( AbstractLogView& view,
                                                               LinesCount logLines )
{
    auto* scrollBar = view.verticalScrollBar();
    dragToScrollbarMaximum( view );
    const auto bottom = view.scrollPosition();
    REQUIRE( bottom.visualLineIndex > 5 );

    // Partway up the last Log Line, where the scrollbar is at its maximum already.
    const auto moveUpToFifthVisualLine = [ & ]() {
        while ( view.scrollPosition().visualLineIndex > 5 ) {
            pressKey( view, Qt::Key_Up );
        }
        REQUIRE( view.scrollPosition() == ScrollPosition{ bottom.lineNumber, 5 } );
        REQUIRE( scrollBar->value() == scrollBar->maximum() );
    };

    moveUpToFifthVisualLine();
    scrollBar->triggerAction( QAbstractSlider::SliderToMaximum );
    REQUIRE( view.scrollPosition() == bottom );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );

    // The thumb pressed, dragged down to where it already is, and released.
    moveUpToFifthVisualLine();
    scrollBar->setSliderDown( true );
    scrollBar->setSliderPosition( scrollBar->maximum() );
    scrollBar->setSliderDown( false );
    REQUIRE( view.scrollPosition() == bottom );

    // Moving between Visual Lines still leaves the scrollbar out of it.
    pressKey( view, Qt::Key_Up );
    REQUIRE( view.scrollPosition()
             == ScrollPosition{ bottom.lineNumber, bottom.visualLineIndex - 1 } );
}

inline void requireFewerVisualLinesThanRowsShowFromTheTop( AbstractLogView& view )
{
    const PinnedWheelScrollLines pinned;

    REQUIRE( view.verticalScrollBar()->maximum() == 0 );
    REQUIRE( view.scrollPosition() == ScrollPosition{} );

    pressKey( view, Qt::Key_PageDown );
    turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
    REQUIRE( view.scrollPosition() == ScrollPosition{} );

    const auto topRow = visualLineAtRow( view, TopRowY );
    REQUIRE( topRow.lineNumber == 0_lnum );
    REQUIRE( topRow.wrappedLineIndex == 0 );
}

// Adds to the Log Lines the view shows and tells the view, returning what the
// Log File holds afterwards.
using GrowLogFile = std::function<LinesCount()>;

inline void requireFollowKeepsTheLastVisualLineOnTheLastRow( AbstractLogView& view,
                                                             const GrowLogFile& grow )
{
    view.followSet( true );
    const auto logLines = grow();
    const auto followed = view.scrollPosition();

    // Follow mode hooks the pull-to-follow bar under the last Visual Line.
    requireLogFileEndsOnRow( view, lastRowY( view ) - ViewportLayout::PullToFollowHookedHeight,
                             logLines );

    // Where it follows to is the bottom Scroll Position.
    view.followSet( false );
    dragToScrollbarMaximum( view );
    REQUIRE( view.scrollPosition() == followed );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );
}

inline void requireGrowthWithoutFollowLeavesTheBottomView( AbstractLogView& view,
                                                           const GrowLogFile& grow )
{
    dragToScrollbarMaximum( view );
    const auto before = view.scrollPosition();
    const auto lastRowBefore = visualLineAtRow( view, lastRowY( view ) );

    grow();

    REQUIRE( view.scrollPosition() == before );
    REQUIRE( visualLineAtRow( view, lastRowY( view ) ) == lastRowBefore );
    REQUIRE( view.verticalScrollBar()->maximum() > static_cast<int>( before.lineNumber.get() ) );
}

// --- re-wrapping and jumps (#155) ------------------------------------------

// How many pixels wider than one column showWide() makes the text: at least
// six columns of any font up to 33 pixels wide.
constexpr int WideTextPx = 200;

inline void resizeText( AbstractLogView& view, int extraWidthPx )
{
    view.resize( ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth + 7
                     + extraWidthPx,
                 view.height() );
    QCoreApplication::processEvents();
}

// From one column wide, WideTextPx wider.
inline void showWide( AbstractLogView& view )
{
    resizeText( view, WideTextPx );
}

// Back to one column wide.
inline void showNarrow( AbstractLogView& view )
{
    resizeText( view, 0 );
}

// The view, WideTextPx wider than one column and showing tallLogLines(),
// partway through the Log Line taller than the Viewport; rewrap changes how
// many columns its text has.
inline void requireRewrapKeepsTheTopRowText( AbstractLogView& view,
                                             const std::function<void()>& rewrap )
{
    moveTo( view, ScrollPosition{ TallLine, 5 } );
    const auto topColumn = topRowColumn( view );
    REQUIRE( topColumn > 0_lcol );

    rewrap();

    REQUIRE( view.scrollPosition().lineNumber == TallLine );
    REQUIRE( view.scrollPosition().visualLineIndex > 0 );
    requireRowHoldsColumn( view, TopRowY, topColumn );
}

// The view, one column wide and showing tallLogLines().
inline void requireResizingKeepsTheTopRowText( AbstractLogView& view )
{
    // Partway down the Log Line taller than the Viewport. One column wide, a
    // Visual Line is exactly one display column.
    constexpr size_t PartwayDown = 224;
    const LineColumn partway{ static_cast<LineColumn::UnderlyingType>( PartwayDown ) };
    moveTo( view, ScrollPosition{ TallLine, PartwayDown } );
    REQUIRE( topRowColumn( view ) == partway );

    showWide( view );
    REQUIRE( view.scrollPosition().lineNumber == TallLine );
    REQUIRE( view.scrollPosition().visualLineIndex > 0 );
    requireRowHoldsColumn( view, TopRowY, partway );
    const auto wideTopColumn = topRowColumn( view );

    // Narrowed again, the top row holds the first character of the wide one.
    showNarrow( view );
    REQUIRE( view.scrollPosition()
             == ScrollPosition{ TallLine, static_cast<size_t>( wideTopColumn.get() ) } );
    REQUIRE( topRowColumn( view ) == wideTopColumn );
}

// The view, one column wide and showing tallLastLogLines().
inline void requireResizingKeepsTheViewAtTheBottom( AbstractLogView& view, LinesCount logLines )
{
    showWide( view );
    dragToScrollbarMaximum( view );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );

    showNarrow( view );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );
    REQUIRE( view.verticalScrollBar()->value() == view.verticalScrollBar()->maximum() );

    showWide( view );
    requireLogFileEndsOnRow( view, lastRowY( view ), logLines );
    REQUIRE( view.verticalScrollBar()->value() == view.verticalScrollBar()->maximum() );
}

using JumpToLogLine = std::function<void( LineNumber )>;

// The view, one column wide and showing tallLogLines().
inline void requireJumpToAWhollyVisibleLogLineDoesNotScroll( AbstractLogView& view,
                                                             const JumpToLogLine& jump )
{
    moveTo( view, ScrollPosition{ 5_lnum, 0 } );
    jump( 7_lnum );
    REQUIRE( view.scrollPosition() == ScrollPosition{ 5_lnum, 0 } );
}

// The view, one column wide and showing tallLogLines().
inline void requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( AbstractLogView& view,
                                                                   const JumpToLogLine& jump )
{
    moveTo( view, ScrollPosition{} );
    jump( TallLine + 50_lcount );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine + 50_lcount, 0 } );

    // Its first Visual Line is above the Viewport, the rest of it below.
    moveTo( view, ScrollPosition{ TallLine, 150 } );
    jump( TallLine );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 0 } );

    // The last Log Line goes no further than the bottom.
    dragToScrollbarMaximum( view );
    const auto bottom = view.scrollPosition();
    moveTo( view, ScrollPosition{} );
    jump( LineNumber( LinesBeforeTallLine + LinesAfterTallLine ) );
    REQUIRE( view.scrollPosition() == bottom );
}

// Searches forward for FoundText with QuickFind, and waits for its result.
inline void quickFindFoundText( AbstractLogView& view, QuickFindPattern& quickFindPattern )
{
    quickFindPattern.changeSearchPattern( FoundText, /* useExtendedRegexp */ false );
    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    view.searchForward();
    REQUIRE( ( selected.count() > 0 || selected.wait( 10000 ) ) );
}

// The view, one column wide and showing quickFindLogLines().
inline void
requireQuickFindPutsTheVisualLineOfTheFoundTextOnTheTopRow( AbstractLogView& view,
                                                            QuickFindPattern& quickFindPattern )
{
    moveTo( view, ScrollPosition{} );
    quickFindFoundText( view, quickFindPattern );
    REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, FoundVisualLine } );

    const auto topRow = visualLineAtRow( view, TopRowY );
    REQUIRE( topRow.lineNumber == TallLine );
    REQUIRE( topRow.wrappedLineIndex == FoundVisualLine );
}

// The view, one column wide and showing quickFindLogLines().
inline void
requireQuickFindOnAWhollyVisibleVisualLineDoesNotScroll( AbstractLogView& view,
                                                         QuickFindPattern& quickFindPattern )
{
    const ScrollPosition twoAbove{ TallLine, FoundVisualLine - 2 };
    moveTo( view, twoAbove );
    quickFindFoundText( view, quickFindPattern );
    REQUIRE( view.scrollPosition() == twoAbove );
}

} // namespace logviewscrolling

#endif // LOG_VIEW_SCROLLING_H
