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

// "Next Mark" goes down to the nearest Mark below the selected Log Line and
// "previous Mark" up to the nearest Mark above, in the main view and in the
// Filtered View alike; with no Mark further that way neither moves (#233).

#include "configuration.h"
#include "filteredview.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logmainview.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryFile>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int NbLogLines = 20;
const LineNumber FirstMark{ 5 };
const LineNumber LastMark{ 12 };

// A loaded Log File of NbLogLines Log Lines with Marks on FirstMark and
// LastMark, and a Search that matches every Log Line, so the Filtered View
// shows each Log Line at its own line number.
struct MarkedLogFile {
    MarkedLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            file.write( QStringLiteral( "log line %1\n" ).arg( line ).toLatin1() );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();

        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( QStringLiteral( "." ) ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

        filteredData->addMark( FirstMark );
        filteredData->addMark( LastMark );
        REQUIRE( filteredData->getNbLine().get() == NbLogLines );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "mark_navigation_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

// No Mark selected: the view did not move.
constexpr int64_t NoMark = -1;

// Selects from, triggers the shortcut of action on view, and returns the Log
// Line it selected, or NoMark when it selected none.
int64_t goToMark( AbstractLogView& view, LineNumber from, const char* action )
{
    view.selectAndDisplayLine( from );

    const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
    REQUIRE_FALSE( keys.isEmpty() );

    QShortcut* shortcut = nullptr;
    for ( auto* candidate : view.findChildren<QShortcut*>() ) {
        if ( candidate->key() == keys.first() ) {
            shortcut = candidate;
        }
    }
    REQUIRE( shortcut != nullptr );

    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    Q_EMIT shortcut->activated();
    if ( selected.isEmpty() ) {
        return NoMark;
    }
    return static_cast<int64_t>( qvariant_cast<LineNumber>( selected.last().at( 0 ) ).get() );
}

// Requires that next and previous Mark move view the same way from every
// selected Log Line: above, on, between and below the Marks.
void requireMarkNavigationMovesDownAndUp( AbstractLogView& view )
{
    const auto next = [ &view ]( LineNumber from ) {
        return goToMark( view, from, ShortcutAction::LogViewNextMark );
    };
    const auto previous = [ &view ]( LineNumber from ) {
        return goToMark( view, from, ShortcutAction::LogViewPrevMark );
    };

    const LineNumber aboveMarks{ 2 };
    const LineNumber betweenMarks{ 8 };
    const LineNumber belowMarks{ 15 };
    // As goToMark() returns them.
    const auto firstMark = static_cast<int64_t>( FirstMark.get() );
    const auto lastMark = static_cast<int64_t>( LastMark.get() );

    // Next Mark goes down to the nearest Mark below.
    CHECK( next( aboveMarks ) == firstMark );
    CHECK( next( FirstMark ) == lastMark );
    CHECK( next( betweenMarks ) == lastMark );
    CHECK( next( LastMark ) == NoMark );
    CHECK( next( belowMarks ) == NoMark );

    // Previous Mark goes up to the nearest Mark above.
    CHECK( previous( belowMarks ) == lastMark );
    CHECK( previous( LastMark ) == firstMark );
    CHECK( previous( betweenMarks ) == firstMark );
    CHECK( previous( FirstMark ) == NoMark );
    CHECK( previous( aboveMarks ) == NoMark );
    CHECK( previous( 0_lnum ) == NoMark );
}

} // namespace

SCENARIO( "Next and previous Mark move the same way in the main view and the Filtered View",
          "[marks][filteredview][logmainview]" )
{
    MarkedLogFile logFile;
    QuickFindPattern quickFindPattern;

    GIVEN( "the main view" )
    {
        LogMainView view( &logFile.logData, &quickFindPattern, nullptr, nullptr, false );
        view.setCurrentSearch( logFile.filteredData.get() );
        view.resize( 400, 200 );
        view.show();
        view.registerShortcuts();

        requireMarkNavigationMovesDownAndUp( view );
    }

    GIVEN( "the Filtered View" )
    {
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );
        view.resize( 400, 200 );
        view.show();
        view.registerShortcuts();

        requireMarkNavigationMovesDownAndUp( view );
    }
}
