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

// What a user does with the mouse in a text view -- selecting, hovering over
// the bullet zone, marking a Log Line, autoscrolling a selection -- driven
// through the view's own events and checked by what the view reports (#149).
//
// The view draws with the painting test's font, so every character is
// 8 x 16 px on every platform and each point below lies on a known character
// of a known Visual Line.

#include <catch2/catch.hpp>

#include <algorithm>
#include <vector>

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QCursor>
#include <QFontInfo>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimerEvent>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "painting_test_font.h"
#include "quickfindpattern.h"
#include "viewportlayout.h"

namespace {

using paintingtestfont::CharHeight;
using paintingtestfont::CharWidth;

constexpr int ViewWidth = 480;
constexpr int ViewHeight = 224;

// Where the text starts with line numbers hidden: after the bullet zone and
// its separator.
constexpr int TextLeftPx = ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth;
// How many columns of text fit, and so where text wrapping splits a Log Line
// without spaces.
constexpr int Columns = ( ViewWidth - TextLeftPx ) / CharWidth;

// A Log Line without spaces, wrapped into LongLineVisualLines Visual Lines.
const LineNumber LongLine{ 1 };
constexpr int LongLineVisualLines = 3;

QString longLineText()
{
    QString text;
    for ( int column = 0; column < ( LongLineVisualLines - 1 ) * Columns + Columns / 2; ++column ) {
        text += QLatin1Char( static_cast<char>( 'a' + column % 26 ) );
    }
    return text;
}

QStringList interactionLines()
{
    QStringList lines{ QStringLiteral( "the first Log Line" ), longLineText() };
    for ( int line = 2; line < 40; ++line ) {
        lines << QStringLiteral( "Log Line %1" ).arg( line );
    }
    return lines;
}

// The Viewport row a Visual Line of a Log Line is on, with the view at the
// top of the Log File. With text wrapping the long Log Line takes
// LongLineVisualLines rows.
int rowOf( bool textWrap, LineNumber line, int visualLine = 0 )
{
    const auto lineRow = static_cast<int>( line.get() );
    if ( !textWrap || line <= LongLine ) {
        return lineRow + visualLine;
    }
    return lineRow + LongLineVisualLines - 1 + visualLine;
}

QPointF onText( int row, int column )
{
    return QPointF{ TextLeftPx + column * CharWidth + CharWidth / 2.0,
                    row * CharHeight + CharHeight / 2.0 };
}

QPointF onBulletZone( int row )
{
    return QPointF{ ViewportLayout::BulletAreaWidth / 2.0, row * CharHeight + CharHeight / 2.0 };
}

class InteractionLogView : public AbstractLogView {
public:
    InteractionLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern,
                        bool textWrap )
        : AbstractLogView( logData, quickFindPattern, textWrap )
    {
    }
};

void showForInteraction( AbstractLogView& view )
{
    const auto font = paintingtestfont::requirePaintingTestFont();

    view.setFrameShape( QFrame::NoFrame );
    view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.resize( ViewWidth, ViewHeight );
    view.show();
    QCoreApplication::processEvents();
    view.updateFont( font );
    view.updateData();

    REQUIRE( QFontInfo( view.font() ).family() == font.family() );
    REQUIRE( view.viewport()->size() == QSize( ViewWidth, ViewHeight ) );
}

void sendMouse( AbstractLogView& view, QEvent::Type type, QPointF pos, Qt::MouseButton button,
                Qt::MouseButtons buttons )
{
    QMouseEvent event( type, pos, view.viewport()->mapToGlobal( pos ), button, buttons,
                       Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &event );
}

void press( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton );
}

void dragTo( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton );
}

void release( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton );
}

void hoverAt( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton );
}

// The timers running on the view itself.
std::vector<int> timersOf( QObject& object )
{
    std::vector<int> ids;
    for ( const auto& timer : QAbstractEventDispatcher::instance()->registeredTimers( &object ) ) {
        ids.push_back( timer.timerId );
    }
    std::ranges::sort( ids );
    return ids;
}

// Selection autoscroll steps on a timer of the view's own. The test fires
// that timer by hand instead of waiting for it, so each tick is exactly one
// step and none depends on how fast the host is. The timer is the one the
// drag started: every step restarts it, possibly under a new id.
void tickAutoscroll( AbstractLogView& view, const std::vector<int>& timersBeforeDrag )
{
    std::vector<int> started;
    std::ranges::set_difference( timersOf( view ), timersBeforeDrag,
                                 std::back_inserter( started ) );
    REQUIRE( started.size() == 1 );

    QTimerEvent tick( started.front() );
    QCoreApplication::sendEvent( &view, &tick );
}

} // namespace

SCENARIO( "Selecting, hovering, marking and autoscrolling through the log view",
          "[logviewinteractions]" )
{
    const FakeLogData logData{ interactionLines() };
    const QuickFindPattern quickFindPattern;

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a text view with text wrapping " << ( textWrap ? "on" : "off" ) )
        {
            InteractionLogView view( &logData, &quickFindPattern, textWrap );
            showForInteraction( view );

            // With text wrapping, the second Visual Line of the long Log Line,
            // whose first character is its column Columns.
            const int longLineRow = rowOf( textWrap, LongLine, textWrap ? 1 : 0 );
            const int longLineRowFirstColumn = textWrap ? Columns : 0;
            const int longLineLastRow
                = rowOf( textWrap, LongLine, textWrap ? LongLineVisualLines - 1 : 0 );

            WHEN( "text is dragged across on one Visual Line" )
            {
                press( view, onText( longLineRow, 3 ) );
                dragTo( view, onText( longLineRow, 10 ) );
                release( view, onText( longLineRow, 10 ) );

                THEN( "the characters from the press to the release are selected" )
                {
                    REQUIRE( view.getSelectedText()
                             == longLineText().mid( longLineRowFirstColumn + 3, 8 ) );
                }
            }

            WHEN( "text is dragged from one Log Line down to another below the long one" )
            {
                press( view, onText( rowOf( textWrap, 0_lnum ), 2 ) );
                dragTo( view, onText( rowOf( textWrap, 2_lnum ), 4 ) );
                release( view, onText( rowOf( textWrap, 2_lnum ), 4 ) );

                THEN( "those Log Lines are selected whole" )
                {
                    auto selected = view.getSelectedText();
                    selected.remove( QChar::CarriageReturn );
                    REQUIRE( selected == interactionLines().mid( 0, 3 ).join( QChar::LineFeed ) );
                }
            }

            WHEN( "the mouse moves over the bullet zone, and then onto the text" )
            {
                std::vector<LineNumber> hovered;
                int leftZone = 0;
                QObject::connect( &view, &AbstractLogView::mouseHoveredOverLine, &view,
                                  [ & ]( LineNumber line ) { hovered.push_back( line ); } );
                QObject::connect( &view, &AbstractLogView::mouseLeftHoveringZone, &view,
                                  [ & ]() { ++leftZone; } );

                hoverAt( view, onBulletZone( longLineLastRow ) );
                hoverAt( view, onBulletZone( rowOf( textWrap, 2_lnum ) ) );
                hoverAt( view, onText( rowOf( textWrap, 2_lnum ), 4 ) );

                THEN( "each Log Line beside it is reported, and then that the mouse left the zone" )
                {
                    REQUIRE( hovered.size() == 2 );
                    REQUIRE( hovered[ 0 ] == LongLine );
                    REQUIRE( hovered[ 1 ] == 2_lnum );
                    REQUIRE( leftZone == 1 );
                }
            }

            WHEN( "the bullet zone is clicked beside the last Visual Line of the long Log Line" )
            {
                std::vector<LineNumber> marked;
                QObject::connect( &view, &AbstractLogView::markLines, &view,
                                  [ & ]( const logsquirl::vector<LineNumber>& lines ) {
                                      marked.insert( marked.end(), lines.begin(), lines.end() );
                                  } );

                press( view, onBulletZone( longLineLastRow ) );
                release( view, onBulletZone( longLineLastRow ) );

                THEN( "that Log Line is marked, and nothing is selected" )
                {
                    REQUIRE( marked.size() == 1 );
                    REQUIRE( marked.front() == LongLine );
                    REQUIRE( view.getSelectedText().isEmpty() );
                }
            }

            WHEN( "the bullet zone is pressed beside one Log Line and released beside another" )
            {
                int markings = 0;
                QObject::connect( &view, &AbstractLogView::markLines, &view,
                                  [ & ]( const logsquirl::vector<LineNumber>& ) { ++markings; } );

                press( view, onBulletZone( rowOf( textWrap, 2_lnum ) ) );
                release( view, onBulletZone( rowOf( textWrap, 3_lnum ) ) );

                THEN( "nothing is marked" )
                {
                    REQUIRE( markings == 0 );
                }
            }

            WHEN( "a selection is dragged below the Viewport and autoscroll steps twice" )
            {
                view.verticalScrollBar()->setValue( static_cast<int>( LongLine.get() ) );
                REQUIRE( view.scrollPosition() == ScrollPosition{ LongLine, 0 } );

                const auto timersBeforeDrag = timersOf( view );
                press( view, onText( 0, 2 ) );

                const QPoint below{ static_cast<int>( onText( 0, 2 ).x() ), ViewHeight + 8 };
                const auto belowGlobal = view.viewport()->mapToGlobal( below );
                // Autoscroll reads where the mouse is from the cursor.
                QCursor::setPos( belowGlobal );
                INFO( "This platform does not let the test place the cursor" );
                REQUIRE( QCursor::pos() == belowGlobal );
                dragTo( view, below );

                tickAutoscroll( view, timersBeforeDrag );
                const auto afterOneStep = view.scrollPosition();
                tickAutoscroll( view, timersBeforeDrag );
                const auto afterTwoSteps = view.scrollPosition();
                auto selected = view.getSelectedText();
                selected.remove( QChar::CarriageReturn );

                release( view, below );

                THEN( "each step moves the view one Visual Line down" )
                {
                    if ( textWrap ) {
                        REQUIRE( afterOneStep == ScrollPosition{ LongLine, 1 } );
                        REQUIRE( afterTwoSteps == ScrollPosition{ LongLine, 2 } );
                    }
                    else {
                        REQUIRE( afterOneStep == ScrollPosition{ 2_lnum, 0 } );
                        REQUIRE( afterTwoSteps == ScrollPosition{ 3_lnum, 0 } );
                    }
                }

                THEN( "the selection reaches from the long Log Line past the bottom of the "
                      "Viewport" )
                {
                    REQUIRE( selected.startsWith( longLineText() + QChar::LineFeed ) );
                }

                THEN( "releasing the mouse stops autoscroll" )
                {
                    REQUIRE( timersOf( view ) == timersBeforeDrag );
                }
            }
        }
    }
}
