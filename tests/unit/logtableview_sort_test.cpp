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

// The Table View's Sort Order (#797): a click on a column header sorts the
// Rows by that column in the background, a second click the other way round,
// a third restores Log Line order; a Row stays its Log Line throughout.

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>

#include "counting_log_data.h"
#include "fake_log_data.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "regularexpressionpattern.h"
#include "rowmapping.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

// timestamp, Δt, level (text), size (integer), body
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_sort_test" );
    format.setTitle( "Sort Order test" );
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
    "2026-01-01 10:00:03.000 DEBUG  delta",    // 4: no size
};

// By level, ascending: DEBUG, ERROR, INFO, WARN, then the unmatched Log Line.
const std::vector<LineNumber> ByLevel = { 4_lnum, 3_lnum, 0_lnum, 1_lnum, 2_lnum };
const std::vector<LineNumber> ByLevelDescending = { 1_lnum, 0_lnum, 3_lnum, 4_lnum, 2_lnum };
const std::vector<LineNumber> LogLineOrder = { 0_lnum, 1_lnum, 2_lnum, 3_lnum, 4_lnum };

int columnOf( const LogTableView& view, const QString& name )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        if ( view.model()->headerData( column, Qt::Horizontal ).toString() == name ) {
            return column;
        }
    }
    return -1;
}

// The Log Lines of every Row, in Row order.
std::vector<LineNumber> rowsOf( const LogTableView& view )
{
    std::vector<LineNumber> lines;
    for ( int row = 0; row < view.model()->rowCount(); ++row ) {
        lines.push_back( view.logLineAt( view.visualRect( view.model()->index( row, 0 ) ).center() )
                             .value_or( LineNumber( 999 ) ) );
    }
    return lines;
}

// The Log Lines of every Row, read through the model's mapping: a Row that
// is not on screen has no position to ask the view at.
std::vector<LineNumber> mappedRowsOf( const LogTableView& view )
{
    std::vector<LineNumber> lines;
    const auto* model = qobject_cast<const LogFormatTableModel*>( view.model() );
    for ( int row = 0; row < model->rowCount(); ++row ) {
        lines.push_back( model->rowMapping().logLineAt( row ) );
    }
    return lines;
}

void open( LogTableView& view, const LogFormatDefinition& format, AbstractLogData& logData )
{
    view.resize( 900, 400 );
    view.setLogFormat( &format, &logData );
    view.updateData( false );
    QTest::qWait( 20 );
}

// Waits until no sort is under way.
bool sorted( const LogTableView& view )
{
    return waitUiState( [ &view ]() { return !view.sortInProgress().has_value(); }, 30000 );
}

// Log Lines whose size runs through every value once in a scrambled order.
QStringList manyLinesOf( int count )
{
    QStringList lines;
    for ( int line = 0; line < count; ++line ) {
        lines << QString( "2026-01-01 10:00:00.%1 INFO %2 message %2" )
                     .arg( line % 1000, 3, 10, QChar( '0' ) )
                     .arg( ( line * 7919 ) % count );
    }
    return lines;
}

void selectRows( LogTableView& view, std::initializer_list<int> rows )
{
    view.clearSelection();
    for ( const auto row : rows ) {
        view.selectionModel()->select( view.model()->index( row, 0 ),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows );
    }
}

} // namespace

SCENARIO( "A click on a column header cycles the Sort Order of the Table View",
          "[logtableview][sortorder]" )
{
    const auto format = makeFormat();
    FakeLogData logData( Lines );
    LogTableView view;
    open( view, format, logData );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const auto* header = view.horizontalHeader();
    const int level = columnOf( view, "level" );
    REQUIRE( level >= 0 );
    REQUIRE_FALSE( view.sortOrder().has_value() );
    REQUIRE_FALSE( header->isSortIndicatorShown() );
    REQUIRE( mappedRowsOf( view ) == LogLineOrder );
    QSignalSpy changed( &view, &LogTableView::sortOrderChanged );

    WHEN( "the level header is clicked" )
    {
        QTest::mouseClick(
            view.horizontalHeader()->viewport(), Qt::LeftButton, {},
            QPoint( header->sectionViewportPosition( level ) + 5, header->height() / 2 ) );

        THEN( "the Rows sort by level, ascending, the header shows the indicator and ties keep "
              "Log Line order with the unmatched Log Line last" )
        {
            REQUIRE( sorted( view ) );
            REQUIRE( view.sortOrder() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
            REQUIRE( header->isSortIndicatorShown() );
            REQUIRE( header->sortIndicatorSection() == level );
            REQUIRE( header->sortIndicatorOrder() == Qt::AscendingOrder );
            REQUIRE( mappedRowsOf( view ) == ByLevel );
            REQUIRE( rowsOf( view ) == ByLevel );
            REQUIRE( changed.count() == 1 );
        }

        AND_WHEN( "it is clicked again" )
        {
            REQUIRE( sorted( view ) );
            view.cycleSortOrder( level );

            THEN( "the Rows sort descending, the unmatched Log Line still last" )
            {
                REQUIRE( sorted( view ) );
                REQUIRE( view.sortOrder()
                         == LogTableView::SortOrder{ level, Qt::DescendingOrder } );
                REQUIRE( header->sortIndicatorOrder() == Qt::DescendingOrder );
                REQUIRE( mappedRowsOf( view ) == ByLevelDescending );
            }

            AND_WHEN( "it is clicked a third time" )
            {
                REQUIRE( sorted( view ) );
                view.cycleSortOrder( level );

                THEN( "the Rows are back in Log Line order, without an indicator" )
                {
                    REQUIRE_FALSE( view.sortOrder().has_value() );
                    REQUIRE_FALSE( view.sortInProgress().has_value() );
                    REQUIRE_FALSE( header->isSortIndicatorShown() );
                    REQUIRE( mappedRowsOf( view ) == LogLineOrder );
                    REQUIRE( changed.count() == 3 );
                }
            }
        }
    }

    WHEN( "the Rows are sorted by the integer column" )
    {
        const int size = columnOf( view, "size" );
        view.sortBy( LogTableView::SortOrder{ size, Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "they sort by number, the Rows without one last" )
        {
            REQUIRE( mappedRowsOf( view )
                     == std::vector<LineNumber>{ 1_lnum, 0_lnum, 3_lnum, 2_lnum, 4_lnum } );
        }

        AND_WHEN( "descending" )
        {
            view.sortBy( LogTableView::SortOrder{ size, Qt::DescendingOrder } );
            REQUIRE( sorted( view ) );

            THEN( "the Rows without a number are still last" )
            {
                REQUIRE( mappedRowsOf( view )
                         == std::vector<LineNumber>{ 3_lnum, 0_lnum, 1_lnum, 2_lnum, 4_lnum } );
            }
        }
    }

    WHEN( "the Rows are sorted by the timestamp column" )
    {
        view.sortBy( LogTableView::SortOrder{ columnOf( view, "timestamp" ), Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "they sort by Timestamp" )
        {
            REQUIRE( mappedRowsOf( view )
                     == std::vector<LineNumber>{ 0_lnum, 3_lnum, 1_lnum, 4_lnum, 2_lnum } );
        }
    }

    WHEN( "the Rows are sorted by the elapsed column" )
    {
        view.sortBy( LogTableView::SortOrder{ columnOf( view, QString( QChar( 0x0394 ) ) + 't' ),
                                              Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "they sort by duration, the Rows with an empty cell last" )
        {
            // 1: +2 s after Log Line 0; 3: -1 s after 1; 4: +2 s after 3
            REQUIRE( mappedRowsOf( view )
                     == std::vector<LineNumber>{ 3_lnum, 1_lnum, 4_lnum, 0_lnum, 2_lnum } );
        }
    }

    WHEN( "the sorted column is hidden" )
    {
        view.sortBy( LogTableView::SortOrder{ level, Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );
        view.setColumnHidden( level, true );

        THEN( "the Sort Order stays, and the indicator comes back with the column" )
        {
            REQUIRE( view.sortOrder() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
            REQUIRE( mappedRowsOf( view ) == ByLevel );
            view.setColumnHidden( level, false );
            REQUIRE( header->isSortIndicatorShown() );
            REQUIRE( header->sortIndicatorSection() == level );
        }
    }

    WHEN( "the Log Format goes" )
    {
        view.sortBy( LogTableView::SortOrder{ level, Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );
        view.setLogFormat( nullptr, nullptr );

        THEN( "the Sort Order is dropped" )
        {
            REQUIRE_FALSE( view.sortOrder().has_value() );
            REQUIRE_FALSE( header->isSortIndicatorShown() );
        }

        AND_WHEN( "the same Log Format is set again" )
        {
            open( view, format, logData );

            THEN( "the Rows are in Log Line order" )
            {
                REQUIRE_FALSE( view.sortOrder().has_value() );
                REQUIRE( mappedRowsOf( view ) == LogLineOrder );
            }
        }
    }
}

SCENARIO( "A Row of a sorted Table View stays its Log Line", "[logtableview][sortorder]" )
{
    const auto format = makeFormat();
    FakeLogData logData( Lines );
    LogTableView view;
    open( view, format, logData );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const int level = columnOf( view, "level" );

    GIVEN( "Rows of Log Lines 1 and 3 selected, 3 the current one" )
    {
        selectRows( view, { 1, 3 } );
        view.selectionModel()->setCurrentIndex( view.model()->index( 3, 0 ),
                                                QItemSelectionModel::NoUpdate );

        WHEN( "the Rows are sorted by level" )
        {
            view.sortBy( LogTableView::SortOrder{ level, Qt::AscendingOrder } );
            REQUIRE( sorted( view ) );

            THEN( "the same Log Lines are selected, in the order shown, and the current Row "
                  "shows Log Line 3" )
            {
                REQUIRE( view.selectedLogLines()
                         == logsquirl::vector<LineNumber>{ 3_lnum, 1_lnum } );
                REQUIRE( view.currentIndex().row() == 1 );
                REQUIRE( view.logLineAt( view.visualRect( view.currentIndex() ).center() )
                         == 3_lnum );
            }

            THEN( "Copy copies the selected Rows in the order shown" )
            {
                REQUIRE( view.selectedText()
                         == QStringList{ "2026-01-01 10:00:01.000\t-1.0s\tERROR\t100\tgamma",
                                         "2026-01-01 10:00:02.000\t+2.0s\tWARN\t7\tAlpha" }
                                .join( '\n' ) );
            }

            THEN( "the CSV export of the selected Rows writes them in the order shown" )
            {
                const QTemporaryDir dir;
                REQUIRE( dir.isValid() );
                CsvExportDialog::Choices choices;
                choices.fileName = dir.filePath( "sorted.csv" );
                choices.selectedRowsOnly = true;
                choices.columns = { 0, 3 };
                choices.header = false;
                view.exportCsvTo( choices );
                QFile file( choices.fileName );
                REQUIRE( file.open( QIODevice::ReadOnly ) );
                REQUIRE( file.readAll() == QByteArray( "\xEF\xBB\xBF" ) + "4,ERROR\r\n2,WARN\r\n" );
            }

            AND_WHEN( "a Log Line is shown" )
            {
                view.showLogLine( 0_lnum );

                THEN( "its Row is selected, wherever the Sort Order put it" )
                {
                    REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 0_lnum } );
                    REQUIRE( view.currentIndex().row() == 2 );
                }
            }
        }
    }
}

namespace {

// A loaded Log File of Lines with Marks on Log Lines 0 and 4, searched for
// the Log Lines ending in "ta": 0 and 4 again, as Matches.
struct MarkedLogFile {
    MarkedLogFile()
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
        filteredData->request( RegularExpressionPattern( "ta$" ) );
        REQUIRE( waitUiState( [ this ]() {
            const auto state = filteredData->searchState();
            return state.phase == SearchSession::Phase::Complete
                   && state.matchCount == LinesCount( 2 );
        } ) );
        filteredData->addMark( 0_lnum );
        filteredData->addMark( 4_lnum );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "logtableview_sort_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

} // namespace

SCENARIO( "Next and previous Mark and Match in a sorted Table View move by Rows",
          "[logtableview][sortorder][marks]" )
{
    const auto format = makeFormat();
    MarkedLogFile logFile;
    LogTableView view;
    open( view, format, logFile.logData );
    view.setCurrentSearch( logFile.filteredData.get() );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();

    GIVEN( "the Rows sorted by level, and the Row of Log Line 3 current" )
    {
        view.sortBy( LogTableView::SortOrder{ columnOf( view, "level" ), Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );
        REQUIRE( mappedRowsOf( view ) == ByLevel );
        view.selectionModel()->setCurrentIndex( view.model()->index( 1, 0 ),
                                                QItemSelectionModel::NoUpdate );

        WHEN( "the next Mark is selected" )
        {
            view.selectNextMark();

            THEN( "it is the Mark in the Row below, Log Line 0, not the next Log Line marked" )
            {
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 0_lnum } );
                REQUIRE( view.currentIndex().row() == 2 );
            }
        }

        WHEN( "the previous Mark is selected" )
        {
            view.selectPreviousMark();

            THEN( "it is the Mark in the Row above, Log Line 4" )
            {
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 4_lnum } );
                REQUIRE( view.currentIndex().row() == 0 );
            }
        }

        WHEN( "the next and the previous Match are selected" )
        {
            view.selectNextMatch();
            const auto next = view.selectedLogLines();
            view.selectionModel()->setCurrentIndex( view.model()->index( 1, 0 ),
                                                    QItemSelectionModel::NoUpdate );
            view.selectPreviousMatch();

            THEN( "they are the Matches in the Rows below and above" )
            {
                REQUIRE( next == logsquirl::vector<LineNumber>{ 0_lnum } );
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 4_lnum } );
            }
        }

        WHEN( "the next Mark is asked for from the last Row with a Mark below" )
        {
            view.selectionModel()->setCurrentIndex( view.model()->index( 2, 0 ),
                                                    QItemSelectionModel::NoUpdate );
            view.selectNextMark();

            THEN( "the selection stays where it was" )
            {
                REQUIRE( view.currentIndex().row() == 2 );
            }
        }
    }
}

namespace {

// A Log File whose reads wait at a gate the test opens: a sort of it stays
// under way for as long as the test wants.
class GatedLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    void openGate()
    {
        setGate( true );
    }
    void closeGate()
    {
        setGate( false );
    }

protected:
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        std::unique_lock<std::mutex> lock( mutex_ );
        opened_.wait( lock, [ this ] { return open_; } );
        lock.unlock();
        return FakeLogData::doGetLines( first, count );
    }

private:
    void setGate( bool open )
    {
        {
            const std::lock_guard<std::mutex> lock( mutex_ );
            open_ = open;
        }
        opened_.notify_all();
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable opened_;
    bool open_ = false;
};

// Opens the gate as it goes: declared after the Table View, it goes before
// the Table View waits for its sort, also when a check failed before the
// test could open the gate itself.
class GateOpenedAtEnd {
public:
    explicit GateOpenedAtEnd( GatedLogData& logData )
        : logData_( logData )
    {
    }
    ~GateOpenedAtEnd()
    {
        logData_.openGate();
    }
    GateOpenedAtEnd( const GateOpenedAtEnd& ) = delete;
    GateOpenedAtEnd& operator=( const GateOpenedAtEnd& ) = delete;

private:
    GatedLogData& logData_;
};

} // namespace

SCENARIO( "A sort under way shows its progress and can be stopped", "[logtableview][sortorder]" )
{
    const auto format = makeFormat();
    GatedLogData logData( Lines );
    LogTableView view;
    const GateOpenedAtEnd opened( logData );
    open( view, format, logData );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const auto* header = view.horizontalHeader();
    const int level = columnOf( view, "level" );
    const int size = columnOf( view, "size" );

    GIVEN( "a sort by level under way" )
    {
        view.cycleSortOrder( level );
        QTest::qWait( 150 );
        REQUIRE( view.sortInProgress() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
        REQUIRE_FALSE( view.sortOrder().has_value() );

        THEN( "the header says so with the indicator, and the Rows are still in Log Line order" )
        {
            REQUIRE( view.model()->headerData( level, Qt::Horizontal ).toString()
                     == "level (sorting 0%)" );
            REQUIRE( header->isSortIndicatorShown() );
            REQUIRE( header->sortIndicatorSection() == level );
            REQUIRE( mappedRowsOf( view ) == LogLineOrder );
            logData.openGate();
        }

        WHEN( "the level header is clicked again" )
        {
            view.cycleSortOrder( level );
            logData.openGate();
            QTest::qWait( 100 );

            THEN( "the sort is stopped, the Rows stay in Log Line order and the header reads its "
                  "name without an indicator" )
            {
                REQUIRE_FALSE( view.sortInProgress().has_value() );
                REQUIRE_FALSE( view.sortOrder().has_value() );
                REQUIRE( mappedRowsOf( view ) == LogLineOrder );
                REQUIRE( view.model()->headerData( level, Qt::Horizontal ).toString() == "level" );
                REQUIRE_FALSE( header->isSortIndicatorShown() );
            }
        }

        WHEN( "Esc is pressed" )
        {
            view.setFocus();
            QTest::keyClick( &view, Qt::Key_Escape );
            logData.openGate();
            QTest::qWait( 100 );

            THEN( "the sort is stopped and the Rows stay in Log Line order" )
            {
                REQUIRE_FALSE( view.sortInProgress().has_value() );
                REQUIRE( mappedRowsOf( view ) == LogLineOrder );
            }
        }

        WHEN( "another header is clicked" )
        {
            view.cycleSortOrder( size );
            logData.openGate();

            THEN( "that column is sorted instead" )
            {
                REQUIRE( sorted( view ) );
                REQUIRE( view.sortOrder() == LogTableView::SortOrder{ size, Qt::AscendingOrder } );
            }
        }
    }

    GIVEN( "the Rows sorted by level, and a sort by size under way" )
    {
        logData.openGate();
        view.cycleSortOrder( level );
        REQUIRE( sorted( view ) );
        REQUIRE( mappedRowsOf( view ) == ByLevel );
        logData.closeGate();
        view.cycleSortOrder( size );
        QTest::qWait( 50 );
        REQUIRE( view.sortInProgress() == LogTableView::SortOrder{ size, Qt::AscendingOrder } );

        THEN( "the Rows are still sorted by level, the indicator on the column being sorted" )
        {
            REQUIRE( mappedRowsOf( view ) == ByLevel );
            REQUIRE( header->sortIndicatorSection() == size );
            logData.openGate();
        }

        WHEN( "the sort by size is stopped" )
        {
            view.stopSorting();
            logData.openGate();
            QTest::qWait( 50 );

            THEN( "the Rows are still sorted by level, with its indicator" )
            {
                REQUIRE_FALSE( view.sortInProgress().has_value() );
                REQUIRE( view.sortOrder() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
                REQUIRE( mappedRowsOf( view ) == ByLevel );
                REQUIRE( header->sortIndicatorSection() == level );
            }
        }

        WHEN( "the other direction of the level column is asked for" )
        {
            // Its keys are read: no Log Line is read for it, so the gate
            // does not hold it.
            view.cycleSortOrder( level );

            THEN( "the Rows sort descending by level" )
            {
                REQUIRE( sorted( view ) );
                REQUIRE( mappedRowsOf( view ) == ByLevelDescending );
            }
        }
    }

    GIVEN( "the Rows sorted by level, and the merge of an appended Log Line under way" )
    {
        logData.openGate();
        view.cycleSortOrder( level );
        REQUIRE( sorted( view ) );
        logData.closeGate();
        auto lines = Lines;
        lines << "2026-01-01 10:00:04.000 CRITICAL 1 epsilon";
        logData.setLines( lines );
        view.updateData( false );
        QTest::qWait( 50 );
        REQUIRE( view.sortInProgress() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
        REQUIRE( view.isMerging() );

        WHEN( "Esc is pressed" )
        {
            view.setFocus();
            QTest::keyClick( &view, Qt::Key_Escape );

            THEN( "the merge goes on, and the Log Line gets its Row" )
            {
                REQUIRE( view.isMerging() );
                logData.openGate();
                REQUIRE( sorted( view ) );
                REQUIRE(
                    mappedRowsOf( view )
                    == std::vector<LineNumber>{ 5_lnum, 4_lnum, 3_lnum, 0_lnum, 1_lnum, 2_lnum } );
            }
        }

        WHEN( "the level header is clicked" )
        {
            view.cycleSortOrder( level );

            THEN( "the Rows sort descending by level, the appended Log Line among them" )
            {
                REQUIRE_FALSE( view.isMerging() );
                logData.openGate();
                REQUIRE( sorted( view ) );
                REQUIRE( view.sortOrder()
                         == LogTableView::SortOrder{ level, Qt::DescendingOrder } );
                // WARN, INFO, ERROR, DEBUG, CRITICAL, then the unmatched Log Line
                REQUIRE(
                    mappedRowsOf( view )
                    == std::vector<LineNumber>{ 1_lnum, 0_lnum, 3_lnum, 4_lnum, 5_lnum, 2_lnum } );
            }
        }
    }
}

SCENARIO( "A Table View with every Row selected keeps them all selected across a sort",
          "[logtableview][sortorder]" )
{
    const auto format = makeFormat();
    FakeLogData logData( manyLinesOf( 20'000 ) );
    LogTableView view;
    open( view, format, logData );
    view.selectAll();
    REQUIRE( view.selectionModel()->selectedRows().size() == 20'000 );

    WHEN( "the Rows are sorted" )
    {
        view.sortBy( LogTableView::SortOrder{ columnOf( view, "size" ), Qt::AscendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "every Row is still selected" )
        {
            REQUIRE( view.selectionModel()->selectedRows().size() == 20'000 );
        }
    }
}

SCENARIO( "A Sort Order is kept by the name of its column", "[logtableview][sortorder]" )
{
    const auto format = makeFormat();
    FakeLogData logData( Lines );
    LogTableView view;
    open( view, format, logData );
    REQUIRE_FALSE( view.namedSortOrder().has_value() );

    GIVEN( "the Rows sorted by level, descending" )
    {
        view.sortBy( LogTableView::NamedSortOrder{ "level", Qt::DescendingOrder } );
        REQUIRE( sorted( view ) );

        THEN( "the Sort Order names its column" )
        {
            const auto named = view.namedSortOrder();
            REQUIRE( named.has_value() );
            REQUIRE( named->column == "level" );
            REQUIRE( named->order == Qt::DescendingOrder );
            REQUIRE( mappedRowsOf( view ) == ByLevelDescending );
        }

        WHEN( "a column the Log Format lacks is asked for" )
        {
            view.sortBy( LogTableView::NamedSortOrder{ "no such column", Qt::AscendingOrder } );

            THEN( "the Sort Order stays" )
            {
                REQUIRE_FALSE( view.sortInProgress().has_value() );
                REQUIRE( view.namedSortOrder()->column == "level" );
            }
        }
    }
}

SCENARIO( "A followed Log File keeps its Sort Order as it grows",
          "[logtableview][sortorder][follow]" )
{
    const auto format = makeFormat();
    CountingLogData logData( Lines );
    LogTableView view;
    open( view, format, logData );
    const int level = columnOf( view, "level" );
    view.sortBy( LogTableView::SortOrder{ level, Qt::AscendingOrder } );
    REQUIRE( sorted( view ) );
    REQUIRE( mappedRowsOf( view ) == ByLevel );
    QSignalSpy changed( &view, &LogTableView::sortOrderChanged );

    WHEN( "two Log Lines are appended, a CRITICAL one and one the Log Format does not match" )
    {
        auto lines = Lines;
        lines << "2026-01-01 10:00:04.000 CRITICAL 1 epsilon" << "another stray line";
        logData.setLines( lines );
        const auto readBefore = logData.linesRead;
        QSignalSpy inserted( view.model(), &QAbstractItemModel::rowsInserted );
        view.updateData( false );

        THEN( "they appear at their place in the Sort Order, inserted, not re-sorted, and only "
              "they were read" )
        {
            REQUIRE( sorted( view ) );
            REQUIRE( mappedRowsOf( view )
                     == std::vector<LineNumber>{ 5_lnum, 4_lnum, 3_lnum, 0_lnum, 1_lnum, 2_lnum,
                                                 6_lnum } );
            REQUIRE( logData.linesRead - readBefore == 2 );
            REQUIRE( inserted.count() == 2 );
            REQUIRE( changed.count() == 1 );
        }
    }

    WHEN( "the Log File is truncated to three Log Lines" )
    {
        logData.setLines( Lines.mid( 0, 3 ) );
        view.reloaded();
        view.updateData( false );

        THEN( "the Rows are sorted anew" )
        {
            REQUIRE( sorted( view ) );
            REQUIRE( view.sortOrder() == LogTableView::SortOrder{ level, Qt::AscendingOrder } );
            REQUIRE( mappedRowsOf( view ) == std::vector<LineNumber>{ 0_lnum, 1_lnum, 2_lnum } );
        }
    }

    WHEN( "the Log File is reloaded with other Log Lines" )
    {
        auto lines = Lines;
        lines[ 4 ] = "2026-01-01 10:00:03.000 ZZZ 1 delta";
        logData.setLines( lines );
        view.reloaded();
        view.updateData( false );

        THEN( "the Rows are sorted anew, by the Log Lines as they are now" )
        {
            REQUIRE( sorted( view ) );
            REQUIRE( mappedRowsOf( view )
                     == std::vector<LineNumber>{ 3_lnum, 0_lnum, 1_lnum, 4_lnum, 2_lnum } );
        }
    }
}

namespace {

QStringList manyLines( int count )
{
    QStringList lines;
    for ( int line = 0; line < count; ++line ) {
        lines << QString( "2026-01-01 10:00:00.%1 INFO %2 message %2" )
                     .arg( line % 1000, 3, 10, QChar( '0' ) )
                     .arg( ( line * 7919 ) % 1000 );
    }
    return lines;
}

bool isAtBottom( const LogTableView& view )
{
    const auto* scrollBar = view.verticalScrollBar();
    return scrollBar->maximum() > 0 && scrollBar->value() == scrollBar->maximum();
}

} // namespace

SCENARIO( "Follow sticks to the last Row of the Sort Order", "[logtableview][sortorder][follow]" )
{
    const auto format = makeFormat();
    FakeLogData logData( manyLines( 200 ) );
    LogTableView view;
    open( view, format, logData );
    view.setActive( true );
    showUntilExposed( view );
    view.sortBy( LogTableView::SortOrder{ columnOf( view, "size" ), Qt::AscendingOrder } );
    REQUIRE( sorted( view ) );
    view.followSet( true );
    QCoreApplication::processEvents();
    REQUIRE( isAtBottom( view ) );

    WHEN( "Log Lines are appended whose place is in the middle of the Sort Order" )
    {
        logData.setLines( manyLines( 260 ) );
        view.updateData( true );
        REQUIRE( sorted( view ) );
        QCoreApplication::processEvents();

        THEN( "the Rows are still at the bottom" )
        {
            REQUIRE( view.model()->rowCount() == 260 );
            REQUIRE( isAtBottom( view ) );
        }
    }
}

namespace {

// A Log File of lines Log Lines, every one with a different size, written to
// disk: a sort of it reads and keys every Log Line.
QString writeLargeLogFile( QTemporaryFile& file, int lines )
{
    REQUIRE( file.open() );
    std::string block;
    for ( int line = 0; line < lines; ++line ) {
        char buffer[ 160 ];
        std::snprintf( buffer, sizeof( buffer ),
                       "2026-01-01 10:%02d:%02d.%03d INFO %d request %d handled by worker-%02d\n",
                       line / 60000 % 60, line / 1000 % 60, line % 1000,
                       static_cast<int>( ( static_cast<long long>( line ) * 7919 ) % 1000003 ),
                       line, line % 16 );
        block += buffer;
        if ( block.size() > ( 1 << 20 ) ) {
            file.write( block.data(), static_cast<qint64>( block.size() ) );
            block.clear();
        }
    }
    file.write( block.data(), static_cast<qint64>( block.size() ) );
    file.flush();
    return file.fileName();
}

} // namespace

SCENARIO( "The UI thread keeps turning while a large Log File is sorted",
          "[logtableview][sortorder][ui]" )
{
    constexpr int LineCount = 300'000;
    QTemporaryFile file( "logtableview_sort_test_large_XXXXXX" );
    const auto fileName = writeLargeLogFile( file, LineCount );

    const auto policies = testSettingsPolicies();
    LogData logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding );
    SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    logData.attachFile( fileName );
    REQUIRE( loadEndSpy.safeWait( 60000 ) );
    REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );

    const auto format = makeFormat();
    LogTableView view;
    open( view, format, logData );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const int size = columnOf( view, "size" );

    WHEN( "the Rows are sorted by the integer column while a timer ticks on the UI thread" )
    {
        // Ticks while the sort is under way: none, were the sort run on the
        // UI thread, which the timer shares.
        int ticksDuringSort = 0;
        qint64 longestGap = 0;
        QElapsedTimer sinceTick;
        sinceTick.start();
        QTimer ticker;
        ticker.setInterval( 10 );
        QObject::connect( &ticker, &QTimer::timeout, [ & ] {
            if ( view.sortInProgress().has_value() ) {
                ++ticksDuringSort;
            }
            longestGap = std::max( longestGap, sinceTick.restart() );
        } );
        ticker.start();

        QElapsedTimer sortTime;
        sortTime.start();
        view.cycleSortOrder( size );
        REQUIRE( sorted( view ) );
        const auto sortMs = sortTime.elapsed();
        ticker.stop();

        THEN( "the timer ticked throughout: the sort ran off the UI thread, and the Rows are "
              "sorted" )
        {
            INFO( "the sort took " << sortMs << " ms, the timer ticked " << ticksDuringSort
                                   << " times meanwhile, the longest gap was " << longestGap
                                   << " ms" );
            REQUIRE( ticksDuringSort >= 2 );
            REQUIRE( longestGap < std::max<qint64>( 1000, sortMs / 2 ) );
            REQUIRE( view.sortOrder() == LogTableView::SortOrder{ size, Qt::AscendingOrder } );
            const auto* model = qobject_cast<const LogFormatTableModel*>( view.model() );
            REQUIRE( model->rowCount() == LineCount );
            REQUIRE( model->rowMapping().logLineAt( 0 ) == 0_lnum );
            auto previous = model->index( 0, size ).data().toString().toLongLong();
            for ( int row = 1; row < 2000; ++row ) {
                const auto value = model->index( row, size ).data().toString().toLongLong();
                REQUIRE( value >= previous );
                previous = value;
            }
        }
    }
}
