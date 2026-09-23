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

// The Filtered View keeps its selection in Log Lines, not in positions among
// the Displayed Lines: a Mark or a Match added above the selection leaves it
// on the same Log Line (#243).

#include "configuration.h"
#include "filteredview.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryFile>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int NbLogLines = 200;

QString logLineText( int line )
{
    return QStringLiteral( "this is line %1" ).arg( line, 6, 10, QLatin1Char( '0' ) );
}

// A loaded Log File whose Log Lines read "this is line NNNNNN", and a Search
// matching every 10th of them.
struct SelectionLogFile {
    SelectionLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            file.write( logLineText( line ).toLatin1() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();

        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( "this is line [0-9]{5}0" ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "filtered_view_selection_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

// The selected text, one Log Line per entry, whatever the line ending.
QStringList selectedLines( const FilteredView& view )
{
    return view.getSelectedText().remove( QChar::CarriageReturn ).split( QChar::LineFeed );
}

void triggerShortcut( QWidget& view, const char* action )
{
    const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
    REQUIRE_FALSE( keys.isEmpty() );

    for ( auto* shortcut : view.findChildren<QShortcut*>() ) {
        if ( shortcut->key() == keys.first() ) {
            Q_EMIT shortcut->activated();
            return;
        }
    }
    FAIL( "no shortcut for " << action );
}

} // namespace

SCENARIO( "The Filtered View keeps the same Log Line selected when a Mark is added above it",
          "[filteredview][selection][marks]" )
{
    SelectionLogFile logFile;
    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, false );
    view.resize( 400, 200 );
    view.show();
    view.registerShortcuts();

    GIVEN( "Log Line 100 selected, the 11th line the Filtered View displays" )
    {
        view.selectAndDisplayLine( 100_lnum );
        REQUIRE( view.getSelectedText() == logLineText( 100 ) );

        WHEN( "Marks are added on Log Lines 1 and 2, above it" )
        {
            logFile.filteredData->addMark( 1_lnum );
            logFile.filteredData->addMark( 2_lnum );
            view.updateData();
            REQUIRE( logFile.filteredData->getMatchingLineNumber( 10_lnum ) == 80_lnum );

            THEN( "Log Line 100 is still selected" )
            {
                REQUIRE( view.getSelectedText() == logLineText( 100 ) );
            }

            THEN( "marking the selection marks Log Line 100" )
            {
                QSignalSpy marked( &view, &AbstractLogView::markLines );
                triggerShortcut( view, ShortcutAction::LogViewMark );

                REQUIRE( marked.count() == 1 );
                REQUIRE( qvariant_cast<logsquirl::vector<LineNumber>>( marked.last().at( 0 ) )
                         == logsquirl::vector<LineNumber>{ 100_lnum } );
            }

            THEN( "moving the selection down selects the Log Line displayed below Log Line 100" )
            {
                QSignalSpy selected( &view, &AbstractLogView::newSelection );
                triggerShortcut( view, ShortcutAction::LogViewSelectionDown );

                REQUIRE( selected.count() == 1 );
                REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 110_lnum );
                REQUIRE( view.getSelectedText() == logLineText( 110 ) );
            }
        }
    }

    GIVEN( "Log Lines 100 to 120 selected" )
    {
        view.selectAndDisplayLine( 100_lnum );
        triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
        triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
        REQUIRE( selectedLines( view )
                 == QStringList{ logLineText( 100 ), logLineText( 110 ), logLineText( 120 ) } );

        WHEN( "a Mark is added on Log Line 105, between them" )
        {
            logFile.filteredData->addMark( 105_lnum );
            view.updateData();

            THEN( "the selection holds the Log Lines displayed from 100 to 120, 105 included" )
            {
                REQUIRE( selectedLines( view )
                         == QStringList{ logLineText( 100 ), logLineText( 105 ), logLineText( 110 ),
                                         logLineText( 120 ) } );
            }
        }
    }
}
