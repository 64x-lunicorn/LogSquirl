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

// QuickFind in the Filtered View searches a copy of the displayed lines taken
// when it starts, and works in Log Line numbers (#156,
// docs/adr/0002-quickfind-searches-a-copy-of-the-displayed-lines.md): a change
// to the displayed lines while it runs neither races its worker thread nor
// moves its result onto another Log Line.

#include <catch2/catch.hpp>

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QThreadPool>

#include "filteredview.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "qfnotifications.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

using VisibilityFlags = LogFilteredData::VisibilityFlags;

// Matches every 10th Log Line.
const QString EveryTenthLine = QStringLiteral( "this is line [0-9]{5}0" );

// A loaded Log File whose Log Lines read "this is line NNNNNN", and its
// Filtered View's data.
struct QuickFindLogFile {
    explicit QuickFindLogFile( int nbLines, SettingsPolicies filePolicies = testSettingsPolicies() )
        : policies( filePolicies )
        , logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < nbLines; ++line ) {
            file.write( logLineText( line ).toLatin1() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
    }

    static QString logLineText( int line )
    {
        return QStringLiteral( "this is line %1" ).arg( line, 6, 10, QLatin1Char( '0' ) );
    }

    // Starts a Search for pattern, without waiting for it.
    void startSearch( const QString& pattern )
    {
        filteredData->request( RegularExpressionPattern( pattern ) );
    }

    // Searches for pattern, and waits for the Search to complete.
    void search( const QString& pattern )
    {
        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        startSearch( pattern );
        REQUIRE( waitUiState( [ & ]() { return searchIsComplete( searchStateSpy ); } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    static bool searchIsComplete( const QSignalSpy& searchStateSpy )
    {
        return searchStateSpy.count() > 0
               && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                      >= 100;
    }

    SettingsPolicies policies;
    QTemporaryFile file{ "filtered_view_quickfind_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

// Waits for QuickFind's worker thread to finish its search. Its result only
// reaches the view once events are processed, which this does not do.
void waitForQuickFindWorker()
{
    REQUIRE( QThreadPool::globalInstance()->waitForDone( 10000 ) );
}

// The Log Line the view selected with the newSelection signal at index: the
// Filtered View hands out Log Lines, never its positions.
LineNumber selectedLogLine( const QSignalSpy& selected, qsizetype index = 0 )
{
    return qvariant_cast<LineNumber>( selected.at( index ).at( 0 ) );
}

void usePattern( QuickFindPattern& quickFindPattern, const QString& regularExpression )
{
    quickFindPattern.changeSearchPattern( regularExpression, /* useExtendedRegexp */ true,
                                          /* isRegex */ true );
}

bool receivedNotification( const QSignalSpy& notifications, const QFNotification& expected )
{
    for ( const auto& arguments : notifications ) {
        if ( qvariant_cast<QFNotification>( arguments.at( 0 ) ).message() == expected.message() ) {
            return true;
        }
    }
    return false;
}

} // namespace

SCENARIO( "a Filtered View QuickFind lands on the Log Line that matched when Marks are added "
          "before its result arrives",
          "[filteredview][quickfind]" )
{
    QuickFindLogFile logFile{ 2000 };
    logFile.search( EveryTenthLine );

    QuickFindPattern quickFindPattern;
    usePattern( quickFindPattern, QStringLiteral( "line 000500" ) );
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );
    QSignalSpy selected( &view, &AbstractLogView::newSelection );

    view.searchForward();
    waitForQuickFindWorker();

    // Three Marks above the match push it three lines down the Filtered View.
    logFile.filteredData->addMark( 1_lnum );
    logFile.filteredData->addMark( 2_lnum );
    logFile.filteredData->addMark( 3_lnum );
    view.updateData();

    REQUIRE( ( selected.count() > 0 || selected.wait( 10000 ) ) );
    REQUIRE( selectedLogLine( selected ) == 500_lnum );
    REQUIRE( view.getSelectedText() == QStringLiteral( "line 000500" ) );
}

SCENARIO( "a Filtered View QuickFind whose matched line is no longer displayed when its result "
          "arrives goes on in the same direction",
          "[filteredview][quickfind]" )
{
    QuickFindLogFile logFile{ 2000 };
    logFile.search( EveryTenthLine );

    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    GIVEN( "Marks on Log Lines 5 and 15, the only displayed lines ending in 5" )
    {
        logFile.filteredData->addMark( 5_lnum );
        logFile.filteredData->addMark( 15_lnum );
        view.updateData();
        usePattern( quickFindPattern, QStringLiteral( "line 0000[01]5" ) );

        WHEN( "a forward QuickFind matched Log Line 5, and its Mark is deleted" )
        {
            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            view.searchForward();
            waitForQuickFindWorker();
            logFile.filteredData->deleteMark( 5_lnum );
            view.updateData();

            THEN( "it selects the next match, Log Line 15" )
            {
                REQUIRE( ( selected.count() > 0 || selected.wait( 10000 ) ) );
                REQUIRE( selected.count() == 1 );
                REQUIRE( selectedLogLine( selected ) == 15_lnum );
            }
        }

        WHEN( "a backward QuickFind from the last line matched Log Line 15, and its Mark is "
              "deleted" )
        {
            view.selectAndDisplayLine( logFile.filteredData->getMatchingLineNumber(
                LineNumber( logFile.filteredData->getNbLine().get() - 1 ) ) );
            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            view.searchBackward();
            waitForQuickFindWorker();
            logFile.filteredData->deleteMark( 15_lnum );
            view.updateData();

            THEN( "it selects the previous match, Log Line 5" )
            {
                REQUIRE( ( selected.count() > 0 || selected.wait( 10000 ) ) );
                REQUIRE( selected.count() == 1 );
                REQUIRE( selectedLogLine( selected ) == 5_lnum );
            }
        }
    }

    GIVEN( "a Mark on Log Line 5, the only displayed line ending in 5" )
    {
        logFile.filteredData->addMark( 5_lnum );
        view.updateData();
        usePattern( quickFindPattern, QStringLiteral( "line 000005" ) );

        WHEN( "a forward QuickFind matched it, and its Mark is deleted" )
        {
            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            QSignalSpy notifications( &view, &AbstractLogView::notifyQuickFind );
            view.searchForward();
            waitForQuickFindWorker();
            logFile.filteredData->deleteMark( 5_lnum );
            view.updateData();

            THEN( "it reports the end of the file and selects nothing" )
            {
                REQUIRE( waitUiState(
                    [ & ]() {
                        return receivedNotification( notifications,
                                                     QFNotificationReachedEndOfFile{} );
                    },
                    10000 ) );
                QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
                REQUIRE( selected.count() == 0 );
            }
        }
    }
}

SCENARIO( "incremental QuickFind in the Filtered View restores its initial selection on the same "
          "Log Line after Marks change",
          "[filteredview][quickfind]" )
{
    QuickFindLogFile logFile{ 2000 };
    logFile.search( EveryTenthLine );

    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    // Log Line 100 is the 11th displayed line.
    view.selectAndDisplayLine( 100_lnum );
    REQUIRE( view.getSelectedText() == QuickFindLogFile::logLineText( 100 ) );

    usePattern( quickFindPattern, QStringLiteral( "line 000500" ) );
    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    view.incrementallySearchForward();
    REQUIRE( ( selected.count() > 0 || selected.wait( 10000 ) ) );
    REQUIRE( selectedLogLine( selected ) == 500_lnum );

    SECTION( "aborting after Marks were added selects Log Line 100 again" )
    {
        // Three Marks above Log Line 100 push it three lines down the Filtered View.
        logFile.filteredData->addMark( 1_lnum );
        logFile.filteredData->addMark( 2_lnum );
        logFile.filteredData->addMark( 3_lnum );
        view.updateData();

        view.incrementalSearchAbort();
        REQUIRE( view.getSelectedText() == QuickFindLogFile::logLineText( 100 ) );
    }

    SECTION( "stopping keeps the match" )
    {
        view.incrementalSearchStop();
        REQUIRE( view.getSelectedText() == QStringLiteral( "line 000500" ) );
    }
}

SCENARIO( "a Filtered View QuickFind runs while Marks change",
          "[filteredview][quickfind][threading]" )
{
    auto policies = testSettingsPolicies();
    policies.search.contextLinesCount = 2;
    QuickFindLogFile logFile{ 2000, policies };
    logFile.search( EveryTenthLine );
    logFile.filteredData->setVisibility( VisibilityFlags::Matches | VisibilityFlags::Marks
                                         | VisibilityFlags::Context );

    // Matches the Log Lines ending in 7: displayed only while Marked.
    const QRegularExpression endsInSeven{ QStringLiteral( "line [0-9]{5}7" ) };
    QuickFindPattern quickFindPattern;
    usePattern( quickFindPattern, endsInSeven.pattern() );
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    // Counts the selections QuickFind makes, not the ones this test makes.
    bool selectingByHand = false;
    int selections = 0;
    int wrongSelections = 0;
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber logLine, LinesCount, LineColumn, LineLength ) {
                          if ( selectingByHand ) {
                              return;
                          }
                          ++selections;
                          const auto text = logFile.logData.getExpandedLineString( logLine );
                          if ( !endsInSeven.match( text ).hasMatch() ) {
                              ++wrongSelections;
                          }
                      } );

    for ( int round = 0; round < 300; ++round ) {
        if ( round % 50 == 0 ) {
            // Start over from the top.
            selectingByHand = true;
            view.selectAndDisplayLine( 0_lnum );
            selectingByHand = false;
        }
        view.searchForward();
        logFile.filteredData->toggleMark(
            LineNumber( static_cast<uint64_t>( round % 60 ) * 10 + 7 ) );
        if ( round % 3 == 0 ) {
            logFile.filteredData->clearMarks();
        }
        view.updateData();
        QCoreApplication::processEvents();
    }
    waitForQuickFindWorker();
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

    REQUIRE( selections > 0 );
    REQUIRE( wrongSelections == 0 );
}

SCENARIO( "a Filtered View QuickFind runs while a Search adds Matches",
          "[filteredview][quickfind][threading]" )
{
    auto policies = testSettingsPolicies();
    policies.search.contextLinesCount = 2;
    policies.search.useParallelSearch = false;
    policies.search.threadPoolSize = 1;
    policies.search.readBufferSizeLines = 10;
    QuickFindLogFile logFile{ 20000, policies };
    logFile.filteredData->setVisibility( VisibilityFlags::Matches | VisibilityFlags::Marks
                                         | VisibilityFlags::Context );

    QuickFindPattern quickFindPattern;
    // Never matches, so every QuickFind reads every displayed line.
    usePattern( quickFindPattern, QStringLiteral( "no such text" ) );
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    SafeQSignalSpy searchStateSpy{ logFile.filteredData.get(),
                                   &LogFilteredData::searchStateChanged };
    logFile.startSearch( EveryTenthLine );

    int quickFindsWhileSearching = 0;
    REQUIRE( waitUiState( [ & ]() {
        if ( QuickFindLogFile::searchIsComplete( searchStateSpy ) ) {
            return true;
        }
        view.searchForward();
        logFile.filteredData->toggleMark( 7_lnum );
        view.updateData();
        ++quickFindsWhileSearching;
        return false;
    } ) );
    waitForQuickFindWorker();
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

    REQUIRE( quickFindsWhileSearching > 0 );
}
