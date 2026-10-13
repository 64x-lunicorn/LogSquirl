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

// The Table View's Matches Only (#798): only the Rows whose Log Line is a
// Match of the current Search, following the Search, off once it is cleared,
// composed with the Sort Order; and the cell menu's Search this value in a
// column.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include <QAction>
#include <QCoreApplication>
#include <QHeaderView>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"
#include "logtableview.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "rowmapping.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

// timestamp, Δt, level, size, body
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_matchesonly_test" );
    format.setTitle( "Matches Only test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) )"
                       R"((?<level>\w+) (?<size>\d*) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );
    QHash<QString, LogFormatValueDef> values;
    values[ "size" ] = LogFormatValueDef{ "integer", false, false };
    format.setValueDefinitions( values );
    return format;
}

const QStringList Lines = {
    "2026-01-01 10:00:00.000 INFO 30 beta",    // 0
    "2026-01-01 10:00:02.000 WARN 7 Alpha",    // 1
    "not a log line",                          // 2
    "2026-01-01 10:00:01.000 ERROR 100 gamma", // 3
    "2026-01-01 10:00:03.000 DEBUG  delta",    // 4
};

using Rows = std::vector<LineNumber>;
const Rows LogLineOrder = { 0_lnum, 1_lnum, 2_lnum, 3_lnum, 4_lnum };

int columnOf( const LogTableView& view, const QString& name )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        if ( view.model()->headerData( column, Qt::Horizontal ).toString() == name ) {
            return column;
        }
    }
    return -1;
}

// The Log Lines of every Row, read through the model's mapping.
Rows rowsOf( const LogTableView& view )
{
    Rows lines;
    const auto* model = qobject_cast<const LogFormatTableModel*>( view.model() );
    for ( int row = 0; row < model->rowCount(); ++row ) {
        lines.push_back( model->rowMapping().logLineAt( row ) );
    }
    return lines;
}

bool sorted( const LogTableView& view )
{
    return waitUiState( [ &view ]() { return !view.sortInProgress().has_value(); }, 30000 );
}

// A loaded Log File of Lines, searched; the Search's Matches are what
// Matches Only shows.
struct SearchedLogFile {
    SearchedLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( const auto& line : Lines ) {
            file.write( line.toUtf8() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
        filteredData = logData.getNewFilteredData();
    }

    // Searches for pattern and waits for the Matches.
    void search( const QString& pattern, LinesCount expectedMatches )
    {
        filteredData->request( RegularExpressionPattern( pattern ) );
        REQUIRE( waitUiState( [ this, expectedMatches ]() {
            const auto state = filteredData->searchState();
            return state.phase == SearchSession::Phase::Complete
                   && state.matchCount == expectedMatches;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    void clearSearch()
    {
        filteredData->request();
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "logtableview_matchesonly_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

// A Table View whose menus can be looked at without opening them.
class MenuTableView : public LogTableView {
public:
    using LogTableView::createContextMenu;
    using LogTableView::createHeaderMenu;
    using LogTableView::LogTableView;
};

QAction* entryOf( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

void open( LogTableView& view, const LogFormatDefinition& format, SearchedLogFile& logFile )
{
    view.resize( 900, 400 );
    view.setLogFormat( &format, &logFile.logData );
    view.setCurrentSearch( logFile.filteredData.get() );
    view.updateData( false );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
}

} // namespace

SCENARIO( "QuickFind under Matches Only searches the Rows shown",
          "[logtableview][matchesonly][quickfind]" )
{
    const auto format = makeFormat();
    SearchedLogFile logFile;
    // The Log Lines beta and delta: 0 and 4. gamma, Log Line 3, is no Row.
    logFile.search( "beta|delta", 2_lcount );
    MenuTableView view;
    open( view, format, logFile );
    view.setMatchesOnly( true );
    REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 4_lnum } );

    auto pattern = std::make_shared<QuickFindPattern>();
    view.setQuickFindPattern( pattern );
    view.showLogLine( 0_lnum );
    QSignalSpy newSelection( &view, &LogTableView::newSelection );

    WHEN( "QuickFind looks for the next gamma or delta from Row 0" )
    {
        pattern->changeSearchPattern( "gamma|delta", true, true );
        view.searchForward();

        THEN( "it finds delta in Row 1, past gamma's Log Line, which is no Row" )
        {
            REQUIRE( ( !newSelection.isEmpty() || newSelection.wait( 10000 ) ) );
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 4_lnum } );
            REQUIRE( view.currentIndex().row() == 1 );
        }
    }
}

SCENARIO( "Matches Only shows the Rows of the current Search's Matches",
          "[logtableview][matchesonly]" )
{
    const auto format = makeFormat();
    SearchedLogFile logFile;
    // The Log Lines beta and Alpha: 0 and 1. A Mark on 3, which is no Match.
    logFile.search( "beta|Alpha", 2_lcount );
    logFile.filteredData->addMark( 3_lnum );
    MenuTableView view;
    open( view, format, logFile );
    QSignalSpy changed( &view, &LogTableView::matchesOnlyChanged );
    REQUIRE( rowsOf( view ) == LogLineOrder );
    REQUIRE_FALSE( view.matchesOnly() );

    WHEN( "Matches Only is switched on" )
    {
        view.setMatchesOnly( true );

        THEN( "exactly the Matches are Rows, the Mark that is no Match hidden" )
        {
            REQUIRE( view.matchesOnly() );
            REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 1_lnum } );
            REQUIRE( changed.count() == 1 );
            REQUIRE( changed.last().front().toBool() );
        }

        THEN( "selection and Go to Log Line address the Log Lines through the Rows" )
        {
            view.showLogLine( 1_lnum );
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 1_lnum } );
            REQUIRE( view.currentIndex().row() == 1 );
            view.selectRow( 0 );
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 0_lnum } );
            REQUIRE( view.selectedText().contains( QStringLiteral( "beta" ) ) );
        }

        AND_WHEN( "Rows 0 and 1 are selected and copied" )
        {
            view.selectRow( 0 );
            view.selectionModel()->select( view.model()->index( 1, 0 ),
                                           QItemSelectionModel::Select
                                               | QItemSelectionModel::Rows );

            THEN( "Copy copies the Log Lines of those Rows, nothing hidden between them" )
            {
                REQUIRE( view.selectedLogLines()
                         == logsquirl::vector<LineNumber>{ 0_lnum, 1_lnum } );
                REQUIRE( view.selectedText()
                         == QStringList{ "2026-01-01 10:00:00.000\t\tINFO\t30\tbeta",
                                         "2026-01-01 10:00:02.000\t+2.0s\tWARN\t7\tAlpha" }
                                .join( '\n' ) );
            }
        }

        THEN( "the next and the previous Match move by the Rows shown" )
        {
            view.selectionModel()->setCurrentIndex( view.model()->index( 0, 0 ),
                                                    QItemSelectionModel::NoUpdate );
            view.selectNextMatch();
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 1_lnum } );
            view.selectPreviousMatch();
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 0_lnum } );
        }

        AND_WHEN( "the Search is replaced by one with other Matches" )
        {
            // The Log Lines ending in "ta": 0 and 4.
            logFile.search( "ta$", 2_lcount );
            view.updateDecorations();

            THEN( "its Rows follow the Matches" )
            {
                REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 4_lnum } );
            }
        }

        AND_WHEN( "the Search finds a Match after the ones shown, as auto-refresh does" )
        {
            logFile.search( "beta|Alpha|delta", 3_lcount );
            REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 1_lnum } );
            view.selectRow( 1 );
            view.updateDecorations();

            THEN( "the new Match's Row joins, the selection kept" )
            {
                REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 1_lnum, 4_lnum } );
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 1_lnum } );
            }
        }

        AND_WHEN( "the Search is cleared" )
        {
            logFile.clearSearch();
            view.updateDecorations();

            THEN( "Matches Only is off and every Row is back" )
            {
                REQUIRE_FALSE( view.matchesOnly() );
                REQUIRE( rowsOf( view ) == LogLineOrder );
                REQUIRE( changed.count() == 2 );
                REQUIRE_FALSE( changed.last().front().toBool() );
            }
        }

        AND_WHEN( "it is switched off" )
        {
            view.setMatchesOnly( false );

            THEN( "every Row is back" )
            {
                REQUIRE_FALSE( view.matchesOnly() );
                REQUIRE( rowsOf( view ) == LogLineOrder );
            }
        }
    }

    WHEN( "the Rows are sorted by level, descending, and Matches Only is switched on" )
    {
        view.sortBy( LogTableView::SortOrder{ columnOf( view, "level" ), Qt::DescendingOrder } );
        REQUIRE( sorted( view ) );
        view.setMatchesOnly( true );
        REQUIRE( sorted( view ) );

        THEN( "the Matches are the Rows, in the Sort Order: WARN before INFO" )
        {
            REQUIRE( rowsOf( view ) == Rows{ 1_lnum, 0_lnum } );
            REQUIRE( view.sortOrder().has_value() );
            REQUIRE( view.horizontalHeader()->isSortIndicatorShown() );
        }

        AND_WHEN( "the Search finds another Match" )
        {
            logFile.search( "beta|Alpha|delta", 3_lcount );
            view.updateDecorations();
            REQUIRE( sorted( view ) );

            THEN( "it lands at its place in the Sort Order: DEBUG last" )
            {
                REQUIRE( rowsOf( view ) == Rows{ 1_lnum, 0_lnum, 4_lnum } );
            }
        }

        AND_WHEN( "Matches Only is switched off" )
        {
            view.setMatchesOnly( false );
            REQUIRE( sorted( view ) );

            THEN( "every Row is back, still in the Sort Order" )
            {
                REQUIRE( rowsOf( view ) == Rows{ 1_lnum, 0_lnum, 3_lnum, 4_lnum, 2_lnum } );
            }
        }
    }

    WHEN( "Matches Only is on and the Rows are then sorted by level, descending" )
    {
        view.setMatchesOnly( true );
        view.sortBy( LogTableView::SortOrder{ columnOf( view, "level" ), Qt::DescendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "the Sort Order orders the Matches' Rows" )
        {
            REQUIRE( rowsOf( view ) == Rows{ 1_lnum, 0_lnum } );
        }
    }
}

SCENARIO( "Matches Only needs a Search to narrow to", "[logtableview][matchesonly]" )
{
    const auto format = makeFormat();
    SearchedLogFile logFile;
    MenuTableView view;
    open( view, format, logFile );
    QSignalSpy changed( &view, &LogTableView::matchesOnlyChanged );

    WHEN( "it is asked for without a Search" )
    {
        view.setMatchesOnly( true );

        THEN( "it stays off, every Row shown, and says so" )
        {
            REQUIRE_FALSE( view.matchesOnly() );
            REQUIRE( rowsOf( view ) == LogLineOrder );
            REQUIRE( changed.count() == 1 );
            REQUIRE_FALSE( changed.last().front().toBool() );
        }
    }

    WHEN( "the header menu is opened without a Search" )
    {
        const auto menu = view.createHeaderMenu( columnOf( view, "level" ) );
        auto* entry = entryOf( *menu, "Matches Only" );

        THEN( "its Matches Only entry is there, unchecked and disabled" )
        {
            REQUIRE( entry != nullptr );
            REQUIRE( entry->isCheckable() );
            REQUIRE_FALSE( entry->isChecked() );
            REQUIRE_FALSE( entry->isEnabled() );
        }
    }

    WHEN( "a Search runs and the header menu's Matches Only is checked" )
    {
        logFile.search( "ta$", 2_lcount );
        const auto menu = view.createHeaderMenu( columnOf( view, "level" ) );
        auto* entry = entryOf( *menu, "Matches Only" );
        REQUIRE( entry != nullptr );
        REQUIRE( entry->isEnabled() );
        entry->setChecked( true );

        THEN( "the Table View shows the Matches only, and the entry is checked next time" )
        {
            REQUIRE( view.matchesOnly() );
            REQUIRE( rowsOf( view ) == Rows{ 0_lnum, 4_lnum } );
            const auto again = view.createHeaderMenu( columnOf( view, "level" ) );
            REQUIRE( entryOf( *again, "Matches Only" )->isChecked() );
        }
    }
}

SCENARIO( "A cell's context menu offers to search its value in its column",
          "[logtableview][fieldterm]" )
{
    const auto format = makeFormat();
    SearchedLogFile logFile;
    MenuTableView view;
    open( view, format, logFile );
    QSignalSpy asked( &view, &LogTableView::searchValueInColumn );

    WHEN( "the context menu opens on the level cell of the first Row" )
    {
        const auto cell = view.model()->index( 0, columnOf( view, "level" ) );
        const auto menu = view.createContextMenu( view.visualRect( cell ).center() );
        auto* entry = entryOf( *menu, "Search this value in level" );

        THEN( "it offers to search INFO in level, which asks for that" )
        {
            REQUIRE( entry != nullptr );
            entry->trigger();
            REQUIRE( asked.count() == 1 );
            REQUIRE( asked.last()[ 0 ].toString() == "level" );
            REQUIRE( asked.last()[ 1 ].toString() == "INFO" );
        }
    }

    WHEN( "the context menu opens on the elapsed-time cell" )
    {
        const auto cell = view.model()->index( 1, columnOf( view, QString::fromUtf8( "Δt" ) ) );
        const auto menu = view.createContextMenu( view.visualRect( cell ).center() );

        THEN( "it offers no such entry: the elapsed time is no field" )
        {
            for ( const auto* action : menu->actions() ) {
                REQUIRE_FALSE( action->text().startsWith( "Search this value" ) );
            }
        }
    }

    WHEN( "the context menu opens on the body cell of a Log Line the Log Format does not parse" )
    {
        // Log Line 2 shows whole in the body cell: it has no fields.
        const auto cell = view.model()->index( 2, columnOf( view, "body" ) );
        REQUIRE( cell.data( Qt::DisplayRole ).toString() == "not a log line" );
        const auto menu = view.createContextMenu( view.visualRect( cell ).center() );

        THEN( "it offers no such entry: no Field Term matches that Log Line" )
        {
            REQUIRE( entryOf( *menu, "Search this value in body" ) == nullptr );
        }
    }

    WHEN( "the context menu opens on an empty cell" )
    {
        // Log Line 4 has no size.
        const auto cell = view.model()->index( 4, columnOf( view, "size" ) );
        REQUIRE( cell.data( Qt::DisplayRole ).toString().isEmpty() );
        const auto menu = view.createContextMenu( view.visualRect( cell ).center() );

        THEN( "it offers no such entry" )
        {
            REQUIRE( entryOf( *menu, "Search this value in size" ) == nullptr );
        }
    }
}
