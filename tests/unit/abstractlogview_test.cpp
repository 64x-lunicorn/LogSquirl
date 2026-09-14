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

#include <catch2/catch.hpp>

#include <QFont>
#include <QWidget>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "log_view_scrolling.h"
#include "logdata.h"
#include "qfnotifications.h"
#include "quickfindpattern.h"
#include "test_policies.h"

#include <QSignalSpy>

namespace {

// Minimal concrete subclass for testing AbstractLogView
class TestLogView : public AbstractLogView {
    Q_OBJECT
public:
    TestLogView( const AbstractLogData* logData, const QuickFindPattern* qfp,
                 QWidget* parent = nullptr, bool initialTextWrap = false )
        : AbstractLogView( logData, qfp, initialTextWrap, parent )
    {
    }

    using AbstractLogView::linesToSave;

protected:
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
};

} // namespace

SCENARIO( "AbstractLogView updateDisplaySize keeps charWidth_ safe", "[abstractlogview][viewport]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    QuickFindPattern qfp;

    GIVEN( "A log view widget created with default font" )
    {
        TestLogView view( &logData, &qfp );
        view.resize( 800, 600 );

        WHEN( "updateFont is called with a very small font" )
        {
            // A 1-pixel font may report zero width for "m" on some platforms
            QFont tinyFont( "Monospace", 1 );
            view.updateFont( tinyFont );

            THEN( "the view does not crash and remains in a valid state" )
            {
                // If charWidth_ were 0, this would trigger a division by zero
                // internally in getNbVisibleCols(). The show()/repaint() path
                // exercises that code.
                view.show();
                view.repaint();
                REQUIRE( true ); // Reaching here means no crash
            }
        }

        WHEN( "updateFont is called with a normal font" )
        {
            QFont normalFont( "Courier", 12 );
            view.updateFont( normalFont );

            THEN( "the view renders without crashing" )
            {
                view.show();
                view.repaint();
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "A text view scrolls by Visual Lines", "[abstractlogview][scrollposition]" )
{
    using namespace logviewscrolling;

    const FakeLogData logData{ tallLogLines() };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "a Log Line taller than the Viewport in the middle of the Log File" )
    {
        THEN( "a wheel step moves the same number of Visual Lines, inside it and across Log Lines, "
              "and reaches its last Visual Line" )
        {
            requireWheelStepsMoveTheSameVisualLinesEverywhere( view );
        }

        THEN( "an arrow key moves one Visual Line, into the next Log Line and back" )
        {
            requireArrowKeysStepOneVisualLineAcrossLogLines( view );
        }

        THEN( "Page Down followed by Page Up returns to the same Scroll Position" )
        {
            requirePageDownThenPageUpReturns( view );
        }

        THEN( "the scrollbar counts whole Log Lines" )
        {
            requireScrollbarCountsWholeLogLines( view );
        }

        THEN( "turning wrapping off and on keeps the same Log Line at the top" )
        {
            requireWrapToggleKeepsTheLogLineAtTheTop( view );
        }
    }
}

SCENARIO( "Every Visual Line of a Log Line with more than 10,000 of them can be reached",
          "[abstractlogview][scrollposition]" )
{
    using namespace logviewscrolling;

    // 2,000 numbered words of six characters: 12,000 Visual Lines one column wide.
    QString numberedWords;
    for ( int word = 0; word < 2000; ++word ) {
        numberedWords += QStringLiteral( "%1 " ).arg( word, 5, 10, QLatin1Char( '0' ) );
    }
    QStringList lines;
    for ( int line = 0; line < 10; ++line ) {
        lines << QStringLiteral( "a" );
    }
    lines << numberedWords;
    for ( int line = 0; line < 100; ++line ) {
        lines << QStringLiteral( "b" );
    }

    const FakeLogData logData{ lines };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "the view scrolled a page at a time, then a Visual Line at a time, to the last digit" )
    {
        // Word 1999 takes the characters 11994 to 11998.
        const ScrollPosition lastDigit{ 10_lnum, 11998 };
        for ( int page = 0; page < 20000 && view.scrollPosition() < lastDigit; ++page ) {
            pressKey( view, Qt::Key_PageDown );
        }
        for ( int step = 0; step < 20000 && view.scrollPosition() > lastDigit; ++step ) {
            pressKey( view, Qt::Key_Up );
        }
        REQUIRE( view.scrollPosition() == lastDigit );

        WHEN( "its top row is double-clicked" )
        {
            doubleClickTopRow( view );

            THEN( "the word under it is selected" )
            {
                REQUIRE( view.getSelectedText() == QStringLiteral( "01999" ) );
            }
        }

        WHEN( "the view moves up two words and its top row is double-clicked" )
        {
            for ( int step = 0; step < 12; ++step ) {
                pressKey( view, Qt::Key_Up );
            }
            doubleClickTopRow( view );

            THEN( "the word under it is selected" )
            {
                REQUIRE( view.getSelectedText() == QStringLiteral( "01997" ) );
            }
        }
    }
}

SCENARIO( "The bottom of a wrapped text view shows exactly the last Visual Line of the Log File",
          "[abstractlogview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log File whose last Log Lines are one Visual Line each" )
    {
        const FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view );
        }

        THEN( "keys and the wheel go no further" )
        {
            requireScrollingStopsAtTheBottom( view );
        }
    }

    GIVEN( "a Log File whose last Log Line is taller than the Viewport" )
    {
        const FakeLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view );
        }

        THEN( "pages and the wheel scroll through it down to that same bottom" )
        {
            requireScrollingDownFromTheTopReachesTheBottom( view );
        }

        THEN( "keys and the wheel go no further" )
        {
            requireScrollingStopsAtTheBottom( view );
        }
    }

    GIVEN( "a Log File of fewer Log Lines than rows, one of them taller than the Viewport" )
    {
        const FakeLogData logData{ fewLogLinesOneTallerThanTheViewport() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "it can be scrolled, down to its last Visual Line on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view );
            requireScrollingDownFromTheTopReachesTheBottom( view );
        }
    }

    GIVEN( "a Log File with fewer Visual Lines than the Viewport has rows" )
    {
        const FakeLogData logData{ fewerVisualLinesThanRows() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "it shows from its top, with no scroll range" )
        {
            requireFewerVisualLinesThanRowsShowFromTheTop( view );
        }
    }
}

SCENARIO( "A wrapped text view at the bottom as its Log File grows",
          "[abstractlogview][scrollposition][bottom][follow]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "Log Lines appended to the Log File" )
    {
        FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        const auto appendLines = [ & ]() {
            auto lines = tallLogLines();
            lines << QStringLiteral( "b" ) << tallLineEndingIn( QStringLiteral( "w" ) );
            logData.setLines( lines );
            view.updateData();
        };

        THEN( "follow mode keeps the new last Visual Line on the last row" )
        {
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, appendLines,
                                                             QStringLiteral( "w" ) );
        }

        THEN( "without follow mode, a view at the bottom stays where it is" )
        {
            requireGrowthWithoutFollowLeavesTheBottomView( view, appendLines );
        }

        THEN( "without follow mode, a view partway through a Log Line stays where it is" )
        {
            moveTo( view, ScrollPosition{ TallLine, 150 } );
            appendLines();
            REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 150 } );
        }
    }

    GIVEN( "the last Log Line of the Log File growing longer" )
    {
        FakeLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        const auto extendLastLine = [ & ]() {
            auto lines = tallLastLogLines();
            lines.last() += QStringLiteral( " x x x w" );
            logData.setLines( lines );
            view.updateData();
        };

        THEN( "follow mode keeps its new last Visual Line on the last row" )
        {
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, extendLastLine,
                                                             QStringLiteral( "w" ) );
        }
    }
}

SCENARIO( "A re-wrap keeps the text on the top row of a wrapped text view",
          "[abstractlogview][scrollposition][rewrap]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log Line of numbered words taller than the Viewport" )
    {
        const FakeLogData logData{ numberedWordsLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "widening and narrowing the view keep the text at the top on the top row" )
        {
            requireResizingKeepsTheTopRowText( view );
        }

        THEN( "a larger font keeps the text at the top on the top row" )
        {
            showWide( view );
            requireRewrapKeepsTheTopRowText( view, [ &view ]() {
                auto font = view.font();
                font.setPointSize( font.pointSize() > 0 ? font.pointSize() * 2 : 24 );
                view.updateFont( font );
            } );
        }

        THEN( "showing and hiding line numbers keep the text at the top on the top row" )
        {
            showWide( view );
            requireRewrapKeepsTheTopRowText( view,
                                             [ &view ]() { view.setLineNumbersVisible( true ); } );
            requireRewrapKeepsTheTopRowText( view,
                                             [ &view ]() { view.setLineNumbersVisible( false ); } );
        }
    }

    GIVEN( "a view at the bottom of a Log File whose last Log Line is taller than the Viewport" )
    {
        const FakeLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "it stays at the bottom through a resize" )
        {
            requireResizingKeepsTheViewAtTheBottom( view );
        }
    }
}

SCENARIO( "A jump moves a wrapped text view only when its target is off screen",
          "[abstractlogview][scrollposition][jump]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log Line taller than the Viewport in the middle of the Log File" )
    {
        const FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        const JumpToLogLine selectLine
            = [ &view ]( LineNumber line ) { view.selectAndDisplayLine( line ); };
        // What selecting a Match in the Filtered View does to the main view.
        const JumpToLogLine selectMatch = [ &view ]( LineNumber line ) {
            view.selectPortionAndDisplayLine( line, 1_lcount, 0_lcol, 1_length );
        };

        THEN( "going to a Match or Mark already wholly visible does not scroll" )
        {
            requireJumpToAWhollyVisibleLogLineDoesNotScroll( view, selectLine );
            requireJumpToAWhollyVisibleLogLineDoesNotScroll( view, selectMatch );
        }

        THEN( "going to a Match or Mark off screen puts its first Visual Line on the top row" )
        {
            requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( view, selectLine );
            requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( view, selectMatch );
        }
    }

    GIVEN( "text QuickFind finds in the 40th Visual Line of a tall Log Line" )
    {
        const FakeLogData logData{ quickFindLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "off screen, that Visual Line is put on the top row" )
        {
            requireQuickFindPutsTheVisualLineOfTheFoundTextOnTheTopRow( view, qfp );
        }

        THEN( "already wholly visible, the view does not scroll" )
        {
            requireQuickFindOnAWhollyVisibleVisualLineDoesNotScroll( view, qfp );
        }
    }
}

namespace {

// A FakeLogData that counts the Log Lines read from it.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable uint64_t linesRead = 0;

protected:
    // Every other read of FakeLogData goes through this one.
    QString doGetLineString( LineNumber line ) const override
    {
        ++linesRead;
        return FakeLogData::doGetLineString( line );
    }
};

} // namespace

SCENARIO( "Updating the scroll bars reads no more than one Viewport height of Log Lines",
          "[abstractlogview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a wrapped view of 10,000 Log Lines of one Visual Line each" )
    {
        QStringList lines;
        for ( int line = 0; line < 10000; ++line ) {
            lines << QStringLiteral( "b" );
        }
        const CountingLogData logData{ lines };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        WHEN( "the Log File changes" )
        {
            logData.linesRead = 0;
            view.updateData();

            THEN( "no more Log Lines are read than the Viewport has rows" )
            {
                // Every row plus a partly hidden one: at least as many as it has rows.
                REQUIRE( logData.linesRead > 0 );
                REQUIRE( logData.linesRead
                         <= static_cast<uint64_t>( view.verticalScrollBar()->pageStep() ) );
            }
        }
    }

    GIVEN( "a wrapped view whose last Log Line is taller than the Viewport" )
    {
        const CountingLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        WHEN( "the Log File changes" )
        {
            logData.linesRead = 0;
            view.updateData();

            THEN( "only that last Log Line is read" )
            {
                REQUIRE( logData.linesRead == 1 );
            }
        }
    }
}

SCENARIO( "QuickFind in the main view searches every Log Line", "[abstractlogview][quickfind]" )
{
    const FakeLogData logData{ QStringList{
        QStringLiteral( "alpha" ), QStringLiteral( "found one" ), QStringLiteral( "beta" ),
        QStringLiteral( "found two" ), QStringLiteral( "gamma" ) } };
    QuickFindPattern qfp;
    qfp.changeSearchPattern( QStringLiteral( "found" ), /* useExtendedRegexp */ false );
    TestLogView view( &logData, &qfp );
    view.selectAndDisplayLine( 0_lnum );

    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    const auto waitForSelection
        = [ & ]( qsizetype count ) { return selected.count() >= count || selected.wait( 10000 ); };
    const auto selectedLine = [ & ]( qsizetype index ) {
        return qvariant_cast<LineNumber>( selected.at( index ).at( 0 ) );
    };

    SECTION( "searching forward selects each match, then reports the end of the file" )
    {
        QSignalSpy notifications( &view, &AbstractLogView::notifyQuickFind );

        view.searchForward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 1_lnum );

        view.searchForward();
        REQUIRE( waitForSelection( 2 ) );
        REQUIRE( selectedLine( 1 ) == 3_lnum );

        view.searchForward();
        const auto reachedEndOfFile = [ & ]() {
            for ( const auto& arguments : notifications ) {
                if ( qvariant_cast<QFNotification>( arguments.at( 0 ) ).message()
                     == QFNotificationReachedEndOfFile{}.message() ) {
                    return true;
                }
            }
            return false;
        };
        REQUIRE( ( reachedEndOfFile() || notifications.wait( 10000 ) ) );
        QCoreApplication::processEvents();
        REQUIRE( reachedEndOfFile() );
        REQUIRE( selected.count() == 2 );
    }

    SECTION( "searching backward from the last Log Line selects the last match" )
    {
        view.selectAndDisplayLine( 4_lnum );
        selected.clear();
        view.searchBackward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 3_lnum );
    }

    SECTION( "an aborted incremental QuickFind restores the initial selection" )
    {
        view.incrementallySearchForward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 1_lnum );

        view.incrementalSearchAbort();
        REQUIRE( view.getSelectedText() == QStringLiteral( "alpha" ) );
    }
}

SCENARIO( "a save from the main view reads the lines of its data", "[abstractlogview][linessaver]" )
{
    QStringList lines;
    for ( int line = 0; line < 7001; ++line ) {
        lines.append( QStringLiteral( "line %1" ).arg( line ) );
    }
    FakeLogData logData{ lines };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp );

    const auto readLines = view.linesToSave();

    THEN( "it reads the lines at the positions asked for" )
    {
        REQUIRE( readLines( 0_lnum, 7001_lcount ) == logData.getLines( 0_lnum, 7001_lcount ) );
        REQUIRE( readLines( 4998_lnum, 3_lcount ) == logData.getLines( 4998_lnum, 3_lcount ) );
    }
}

#include "abstractlogview_test.moc"
