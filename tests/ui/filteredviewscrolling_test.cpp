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

// The Filtered View scrolls by Visual Lines, stops at its bottom, keeps its
// reading position through a re-wrap and jumps exactly as the main view does
// (#153, #154, #155): the same checks as
// tests/unit/abstractlogview_test.cpp, run on a FilteredView showing the Matches
// of a Search over a real Log File. The scrolling rules themselves are checked
// without a widget in tests/textviewscrolling (#246); these check that the
// Filtered View hands them its Displayed Lines.

#include <catch2/catch.hpp>

#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryFile>

#include "filteredview.h"
#include "log_view_scrolling.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"
#include "test_policies.h"
#include "test_utils.h"

#include "configuration.h"

namespace {

// A Log File of the given Log Lines, loaded, and a Search that selects every
// one of its first searchedLines Log Lines (all of them by default).
struct FilteredLogFile {
    explicit FilteredLogFile( const QStringList& lines, qsizetype searchedLines = -1 )
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        // The last Log Line has no line ending, so text appended to the file
        // goes on it.
        file.write( lines.join( QLatin1Char( '\n' ) ).toLatin1() );
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
        searchUpTo( searchedLines < 0 ? lines.size() : searchedLines );
    }

    // Searches the first searchedLines Log Lines, and waits for the Matches.
    void searchUpTo( qsizetype searchedLines )
    {
        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( QStringLiteral( "." ) ), 0_lnum,
                               LineNumber( static_cast<uint64_t>( searchedLines ) ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

        REQUIRE( filteredData->getNbLine().get() == static_cast<uint64_t>( searchedLines ) );
    }

    // Appends text to the last Log Line, reloads the Log File and searches it
    // all again.
    void appendToLastLogLine( const QString& text )
    {
        const auto nbLines = logData.getNbLine();
        file.write( text.toLatin1() );
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.reload();
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
        REQUIRE( logData.getNbLine() == nbLines );

        searchUpTo( static_cast<qsizetype>( nbLines.get() ) );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "filtered_view_scrolling_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

} // namespace

SCENARIO( "The Filtered View scrolls by Visual Lines", "[filteredview][scrollposition]" )
{
    using namespace logviewscrolling;

    FilteredLogFile logFile{ tallLogLines() };
    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "a Match taller than the Viewport in the middle of the Search results" )
    {
        THEN( "a wheel step moves the same number of Visual Lines, inside it and across Log Lines, "
              "and reaches its last Visual Line" )
        {
            requireWheelStepsMoveTheSameVisualLinesEverywhere( view );
        }

        THEN( "the scrollbar counts whole Log Lines" )
        {
            requireScrollbarCountsWholeLogLines( view );
        }
    }
}

SCENARIO( "The bottom of the Filtered View shows exactly its last Visual Line",
          "[filteredview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern quickFindPattern;

    GIVEN( "Search results whose last Matches are one Visual Line each" )
    {
        FilteredLogFile logFile{ tallLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum the last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow(
                view, logFile.filteredData->getNbLine() );
        }
    }

    GIVEN( "Search results whose last Match is taller than the Viewport" )
    {
        FilteredLogFile logFile{ tallLastLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow(
                view, logFile.filteredData->getNbLine() );
        }

        THEN( "moving the scrollbar to its maximum from partway up that Match lands at the "
              "bottom" )
        {
            requireScrollbarMovedToItsMaximumLandsAtTheBottom( view,
                                                               logFile.filteredData->getNbLine() );
        }

        THEN( "follow mode keeps its new last Visual Line on the last row as it grows" )
        {
            const auto extendLastLine = [ & ]() {
                logFile.appendToLastLogLine( QStringLiteral( " x x x x" ) );
                view.updateData();
                return logFile.filteredData->getNbLine();
            };
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, extendLastLine );
        }
    }

    GIVEN( "a Search that has covered part of the Log File so far" )
    {
        const auto lines = tallLogLines();
        FilteredLogFile logFile{ lines, 50 };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        const auto searchTheRest = [ & ]() {
            logFile.searchUpTo( lines.size() );
            view.updateData();
            return logFile.filteredData->getNbLine();
        };

        THEN( "follow mode keeps the new last Visual Line on the last row as Matches are added" )
        {
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, searchTheRest );
        }
    }
}

SCENARIO( "A re-wrap keeps the text on the top row of the Filtered View",
          "[filteredview][scrollposition][rewrap]" )
{
    using namespace logviewscrolling;

    QuickFindPattern quickFindPattern;

    GIVEN( "a Match taller than the Viewport" )
    {
        FilteredLogFile logFile{ tallLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
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

        THEN( "showing line numbers keeps the text at the top on the top row" )
        {
            showWide( view );
            requireRewrapKeepsTheTopRowText( view,
                                             [ &view ]() { view.setLineNumbersVisible( true ); } );
        }
    }

    GIVEN( "a Filtered View at the bottom of Matches whose last one is taller than the Viewport" )
    {
        FilteredLogFile logFile{ tallLastLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "it stays at the bottom through a resize" )
        {
            requireResizingKeepsTheViewAtTheBottom( view, logFile.filteredData->getNbLine() );
        }
    }
}

SCENARIO( "A jump moves the Filtered View only when its target is off screen",
          "[filteredview][scrollposition][jump]" )
{
    using namespace logviewscrolling;

    QuickFindPattern quickFindPattern;

    GIVEN( "a Match taller than the Viewport in the middle of the Search results" )
    {
        FilteredLogFile logFile{ tallLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        const JumpToLogLine selectLine
            = [ &view ]( LineNumber line ) { view.selectAndDisplayLine( line ); };

        THEN( "going to a Match already wholly visible does not scroll" )
        {
            requireJumpToAWhollyVisibleLogLineDoesNotScroll( view, selectLine );
        }

        THEN( "going to a Match off screen puts its first Visual Line on the top row" )
        {
            requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( view, selectLine );
        }
    }

    GIVEN( "text QuickFind finds in the 40th Visual Line of a tall Match" )
    {
        FilteredLogFile logFile{ quickFindLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "off screen, that Visual Line is put on the top row" )
        {
            requireQuickFindPutsTheVisualLineOfTheFoundTextOnTheTopRow( view, quickFindPattern );
        }

        THEN( "already wholly visible, the view does not scroll" )
        {
            requireQuickFindOnAWhollyVisibleVisualLineDoesNotScroll( view, quickFindPattern );
        }
    }
}

SCENARIO( "Jumping to a Mark moves the Filtered View only when the Mark is off screen",
          "[filteredview][scrollposition][jump]" )
{
    using namespace logviewscrolling;

    FilteredLogFile logFile{ tallLogLines() };
    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
    showOneColumnWide( view );
    view.registerShortcuts();

    QSignalSpy selected( &view, &AbstractLogView::newSelection );

    // Marks line, selects the Log Line on the top row by clicking it, and goes
    // from there to the Mark: down to the next one or up to the previous one.
    const auto jumpToMark = [ & ]( LineNumber line ) {
        logFile.filteredData->addMark( line );

        const auto onText = topRowText();
        const auto global = view.viewport()->mapToGlobal( onText );
        QMouseEvent press( QEvent::MouseButtonPress, onText, global, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier );
        QCoreApplication::sendEvent( view.viewport(), &press );
        QMouseEvent release( QEvent::MouseButtonRelease, onText, global, Qt::LeftButton,
                             Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.viewport(), &release );

        // "Next mark" goes down, "previous mark" up, as in the main view (#233).
        const auto action = line > view.scrollPosition().lineNumber
                                ? ShortcutAction::LogViewNextMark
                                : ShortcutAction::LogViewPrevMark;
        const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
        REQUIRE_FALSE( keys.isEmpty() );

        QShortcut* shortcut = nullptr;
        for ( auto* candidate : view.findChildren<QShortcut*>() ) {
            if ( candidate->key() == keys.first() ) {
                shortcut = candidate;
            }
        }
        REQUIRE( shortcut != nullptr );

        selected.clear();
        Q_EMIT shortcut->activated();
        REQUIRE( selected.count() > 0 );
        REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == line );
    };

    THEN( "a Mark already wholly visible does not scroll" )
    {
        moveTo( view, ScrollPosition{ 5_lnum, 0 } );
        jumpToMark( 7_lnum );
        REQUIRE( view.scrollPosition() == ScrollPosition{ 5_lnum, 0 } );
    }

    THEN( "a Mark below the Viewport puts its first Visual Line on the top row" )
    {
        moveTo( view, ScrollPosition{} );
        jumpToMark( TallLine + 50_lcount );
        REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine + 50_lcount, 0 } );
    }

    THEN( "a Mark above the Viewport puts its first Visual Line on the top row" )
    {
        moveTo( view, ScrollPosition{ TallLine + 1_lcount, 0 } );
        jumpToMark( TallLine );
        REQUIRE( view.scrollPosition() == ScrollPosition{ TallLine, 0 } );
    }
}
