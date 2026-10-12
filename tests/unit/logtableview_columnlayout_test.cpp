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

// The Column Layout of the Table View (#792): the header's context menu hides
// and shows columns, a dragged header moves one, and the layout is kept per
// Log Format beside the widths. A Row is unchanged by its layout.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include "columnlayout.h"
#include "fake_log_data.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatdefinition.h"
#include "logtablehighlightdelegate.h"
#include "logtableview.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

// A Log Format of its own, so the layout this test keeps cannot meet any a
// real Log Format has kept: timestamp, host and body, with the elapsed-time
// column after the timestamp.
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_columnlayout_test" );
    format.setTitle( "Table View Column Layout test" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>\S+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );

    QHash<QString, LogFormatValueDef> values;
    values[ "host" ] = LogFormatValueDef{ "string", true, false };
    format.setValueDefinitions( values );

    return format;
}

// The same Log Format with one more field, so its columns differ.
LogFormatDefinition makeFormatWithMoreColumns()
{
    auto format = makeFormat();
    QHash<QString, QString> regex;
    regex[ "basic" ]
        = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>\S+) (?<word>\S+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    auto values = format.valueDefinitions();
    values[ "word" ] = LogFormatValueDef{ "string", false, false };
    format.setValueDefinitions( values );
    return format;
}

const QStringList Lines = {
    "Jan  1 12:00:00 host1 first message",
    "Jan  1 12:00:01 host2 second message",
    "Jan  1 12:00:02 host3 third message",
};

const QStringList OtherLines = {
    "Feb  2 13:00:00 other1 another message",
    "Feb  2 13:00:01 other2 one more message",
};

constexpr int WidenedWidth = 321;

// Drops the layout kept for the Log Format before and after a test.
struct NoKeptLayout {
    explicit NoKeptLayout( LogFormatDefinition kept )
        : format( std::move( kept ) )
    {
        dropColumnLayout( format );
    }
    ~NoKeptLayout()
    {
        dropColumnLayout( format );
    }
    NoKeptLayout( const NoKeptLayout& ) = delete;
    NoKeptLayout& operator=( const NoKeptLayout& ) = delete;

    LogFormatDefinition format;
};

// A Table View whose menus can be looked at without opening them.
class MenuTableView : public LogTableView {
public:
    using LogTableView::createContextMenu;
    using LogTableView::createHeaderMenu;
    using LogTableView::LogTableView;
};

// Show the Log File in a Table View, and let its deferred column sizing run.
void open( LogTableView& view, const LogFormatDefinition& format, AbstractLogData& logData )
{
    view.resize( 800, 400 );
    view.setLogFormat( &format, &logData );
    view.updateData( false );
    QTest::qWait( 20 );
}

// The model's columns, in the Log Format's order.
QStringList columnNamesOf( const LogTableView& view )
{
    QStringList names;
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        names << view.model()->headerData( column, Qt::Horizontal ).toString();
    }
    return names;
}

int columnOf( const LogTableView& view, const QString& name )
{
    const auto column = static_cast<int>( columnNamesOf( view ).indexOf( name ) );
    REQUIRE( column >= 0 );
    return column;
}

std::vector<int> everyColumnInOrder( const LogTableView& view )
{
    std::vector<int> columns( static_cast<size_t>( view.model()->columnCount() ) );
    std::iota( columns.begin(), columns.end(), 0 );
    return columns;
}

QStringList entriesOf( const QMenu& menu )
{
    QStringList entries;
    for ( const auto* action : menu.actions() ) {
        if ( !action->isSeparator() ) {
            entries << action->text();
        }
    }
    return entries;
}

QAction* entryOf( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    FAIL( "no entry " << text.toStdString() );
    return nullptr;
}

// Chooses the entry of a menu, as if the user had.
void choose( const QMenu& menu, const QString& entry )
{
    auto* action = entryOf( menu, entry );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

// Unchecks the column in the header menu of the body column.
void hideColumn( MenuTableView& view, const QString& name )
{
    const auto menu = view.createHeaderMenu( columnOf( view, "body" ) );
    entryOf( *menu, name )->setChecked( false );
}

// Moves the column to the front, as a drag of its header there does.
void moveFirst( LogTableView& view, int column )
{
    auto* header = view.horizontalHeader();
    header->moveSection( header->visualIndex( column ), 0 );
}

void selectRows( LogTableView& view, std::initializer_list<int> rows )
{
    view.clearSelection();
    for ( const auto row : rows ) {
        view.selectionModel()->select( view.model()->index( row, 0 ),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows );
    }
}

// Double-clicks the character at charIndex of the cell of the Row whose text
// is text, just inside that character's left edge.
void doubleClickCharacter( LogTableView& view, int row, const QString& text, int charIndex )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        const auto index = view.model()->index( row, column );
        if ( index.data( Qt::DisplayRole ).toString() == text ) {
            const auto rect = view.visualRect( index );
            const auto x = rect.left() + LogTableHighlightDelegate::HorizontalTextPadding
                           + QFontMetrics( view.font() ).horizontalAdvance( text.left( charIndex ) )
                           + 1;
            QTest::mouseDClick( view.viewport(), Qt::LeftButton, {},
                                QPoint( x, rect.center().y() ) );
            return;
        }
    }
    FAIL( "no cell holding " << text.toStdString() );
}

// Drags across the whole cell of the Row whose text is exactly text.
void selectCell( LogTableView& view, int row, const QString& text )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        const auto index = view.model()->index( row, column );
        if ( index.data( Qt::DisplayRole ).toString() == text ) {
            const auto cell = view.visualRect( index );
            QTest::mousePress( view.viewport(), Qt::LeftButton, {},
                               QPoint( cell.left(), cell.center().y() ) );
            QTest::mouseMove( view.viewport(), QPoint( cell.right(), cell.center().y() ) );
            QTest::mouseRelease( view.viewport(), Qt::LeftButton, {},
                                 QPoint( cell.right(), cell.center().y() ) );
            return;
        }
    }
    FAIL( "no cell holding " << text.toStdString() );
}

// A point in the header, within its first column.
const QPoint HeaderStart( 5, 5 );

} // namespace

SCENARIO( "The header menu of the Table View hides and shows columns, one always staying shown",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );
    MenuTableView view;
    open( view, kept.format, logData );
    const auto names = columnNamesOf( view );
    const int host = columnOf( view, "host" );
    const int body = columnOf( view, "body" );

    GIVEN( "the header menu of the host column" )
    {
        const auto menu = view.createHeaderMenu( host );

        THEN( "it offers Count values, every column checked and enabled, then Reset columns" )
        {
            REQUIRE( entriesOf( *menu )
                     == QStringList{ "Count values" } + names + QStringList{ "Reset columns" } );
            for ( const auto& name : names ) {
                INFO( "column " << name.toStdString() );
                REQUIRE( entryOf( *menu, name )->isCheckable() );
                REQUIRE( entryOf( *menu, name )->isChecked() );
                REQUIRE( entryOf( *menu, name )->isEnabled() );
            }
        }

        THEN( "the menu of the elapsed-time column, no field of the Log Format, counts nothing" )
        {
            REQUIRE( entriesOf( *view.createHeaderMenu( columnOf( view, "Δt" ) ) )
                     == names + QStringList{ "Reset columns" } );
        }

        WHEN( "host is unchecked" )
        {
            entryOf( *menu, "host" )->setChecked( false );

            THEN( "the host column is hidden, and the Rows keep every column" )
            {
                REQUIRE( view.isColumnHidden( host ) );
                auto shown = everyColumnInOrder( view );
                std::erase( shown, host );
                REQUIRE( view.columnLayout().shownColumns() == shown );
                REQUIRE( view.model()->columnCount() == names.size() );
                REQUIRE_FALSE( entryOf( *view.createHeaderMenu( body ), "host" )->isChecked() );
            }

            AND_WHEN( "host is checked again" )
            {
                entryOf( *view.createHeaderMenu( body ), "host" )->setChecked( true );

                THEN( "the host column is shown again, where it was" )
                {
                    REQUIRE_FALSE( view.isColumnHidden( host ) );
                    REQUIRE( view.columnLayout().shownColumns() == everyColumnInOrder( view ) );
                }
            }
        }
    }

    GIVEN( "every column but body hidden" )
    {
        for ( const auto& name : names ) {
            if ( name != "body" ) {
                hideColumn( view, name );
            }
        }
        REQUIRE( view.columnLayout().shownColumns() == std::vector<int>{ body } );

        THEN( "body's entry is checked and disabled, the others unchecked and enabled" )
        {
            const auto menu = view.createHeaderMenu( body );
            for ( const auto& name : names ) {
                INFO( "column " << name.toStdString() );
                REQUIRE( entryOf( *menu, name )->isChecked() == ( name == "body" ) );
                REQUIRE( entryOf( *menu, name )->isEnabled() == ( name != "body" ) );
            }
        }

        THEN( "body stays shown even when its entry is unchecked" )
        {
            entryOf( *view.createHeaderMenu( body ), "body" )->setChecked( false );
            REQUIRE_FALSE( view.isColumnHidden( body ) );
        }
    }
}

SCENARIO( "A column header moved to another place moves the column, not the Log Format's order",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );
    MenuTableView view;
    open( view, kept.format, logData );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const int host = columnOf( view, "host" );
    const int body = columnOf( view, "body" );

    GIVEN( "the body header dragged to the front with the mouse" )
    {
        auto* header = view.horizontalHeader();
        const QPoint from( header->sectionViewportPosition( body )
                               + header->sectionSize( body ) / 2,
                           header->height() / 2 );
        const QPoint to( HeaderStart.x(), header->height() / 2 );
        QTest::mousePress( header->viewport(), Qt::LeftButton, {}, from );
        // Past the start-drag distance first, then to the front.
        QTest::mouseMove( header->viewport(), from - QPoint( 40, 0 ) );
        QTest::mouseMove( header->viewport(), to );
        QTest::mouseRelease( header->viewport(), Qt::LeftButton, {}, to );
        QCoreApplication::processEvents();

        THEN( "the body column is shown first, and the model keeps the Log Format's order" )
        {
            REQUIRE( header->logicalIndex( 0 ) == body );
            REQUIRE( view.columnLayout().order.front() == body );
            REQUIRE( columnOf( view, "body" ) == body );
        }
    }

    GIVEN( "the body column moved to the front" )
    {
        moveFirst( view, body );
        QCoreApplication::processEvents();

        THEN( "it is shown first, and the model keeps the Log Format's order" )
        {
            REQUIRE( view.horizontalHeader()->logicalIndexAt( HeaderStart ) == body );
            auto order = everyColumnInOrder( view );
            std::erase( order, body );
            order.insert( order.begin(), body );
            REQUIRE( view.columnLayout().order == order );
            REQUIRE( columnOf( view, "body" ) == body );
            REQUIRE( view.model()->index( 0, body ).data().toString() == "first message" );
        }

        WHEN( "a word of the first Row's body is double-clicked" )
        {
            doubleClickCharacter( view, 0, "first message", 0 );

            THEN( "the word is selected in the body column, and the context menu hands it on" )
            {
                REQUIRE( view.selectedText() == "first" );
                REQUIRE( view.selection().inCell().has_value() );
                REQUIRE( view.selection().inCell()->column == body );

                QSignalSpy replaced( &view, &LogTableView::replaceSearch );
                const auto cell = view.visualRect( view.model()->index( 0, body ) );
                choose( *view.createContextMenu( cell.center() ), "&Replace search" );
                REQUIRE( replaced.count() == 1 );
                REQUIRE( replaced.first().at( 0 ).toString() == "first" );
            }
        }

        WHEN( "the host cell of the second Row is dragged across" )
        {
            selectCell( view, 1, "host2" );

            THEN( "its characters are selected in the host column" )
            {
                REQUIRE( view.selectedText() == "host2" );
                REQUIRE( view.selection().inCell().has_value() );
                REQUIRE( view.selection().inCell()->column == host );
            }
        }

        WHEN( "Count values is chosen in the header menu of the first shown column" )
        {
            QSignalSpy counted( &view, &LogTableView::countValuesRequested );
            const int first = view.horizontalHeader()->logicalIndexAt( HeaderStart );
            choose( *view.createHeaderMenu( first ), "Count values" );

            THEN( "the body field is counted" )
            {
                REQUIRE( counted.count() == 1 );
                REQUIRE( counted.first().at( 0 ).toString() == "body" );
            }
        }
    }
}

SCENARIO( "A Column Layout is kept per Log Format and applied to the next Log File of it",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );

    GIVEN( "a Table View in which the user hid host, moved body first and widened timestamp" )
    {
        int host = -1;
        int body = -1;
        int timestamp = -1;
        {
            MenuTableView view;
            open( view, kept.format, logData );
            host = columnOf( view, "host" );
            body = columnOf( view, "body" );
            timestamp = columnOf( view, "timestamp" );
            hideColumn( view, "host" );
            moveFirst( view, body );
            view.horizontalHeader()->resizeSection( timestamp, WidenedWidth );
        }

        WHEN( "another Log File of the Log Format opens in a new Table View" )
        {
            FakeLogData other( OtherLines );
            LogTableView view;
            open( view, kept.format, other );

            THEN( "its columns are as the user left them" )
            {
                const auto layout = view.columnLayout();
                REQUIRE( layout.isHidden( host ) );
                REQUIRE( layout.shownColumns().front() == body );
                REQUIRE( view.columnWidth( timestamp ) == WidenedWidth );
            }
        }

        WHEN( "the Log Format's columns changed" )
        {
            const auto changed = makeFormatWithMoreColumns();
            LogTableView view;
            open( view, changed, logData );

            THEN( "the defaults apply: every column shown in the Log Format's order, not widened" )
            {
                const auto layout = view.columnLayout();
                REQUIRE( layout.shownColumns() == everyColumnInOrder( view ) );
                REQUIRE( view.columnWidth( columnOf( view, "timestamp" ) ) != WidenedWidth );
            }
        }
    }
}

SCENARIO( "Reset columns restores the Log Format's order, shows every column and drops the "
          "saved widths",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );
    MenuTableView view;
    open( view, kept.format, logData );
    const int host = columnOf( view, "host" );
    const int body = columnOf( view, "body" );
    const int timestamp = columnOf( view, "timestamp" );
    const int defaultTimestampWidth = view.columnWidth( timestamp );
    REQUIRE( defaultTimestampWidth != WidenedWidth );

    GIVEN( "host hidden, body moved first and timestamp widened" )
    {
        hideColumn( view, "host" );
        moveFirst( view, body );
        view.horizontalHeader()->resizeSection( timestamp, WidenedWidth );
        REQUIRE( loadColumnLayout( kept.format, columnNamesOf( view ) ).has_value() );

        WHEN( "Reset columns is chosen" )
        {
            choose( *view.createHeaderMenu( body ), "Reset columns" );

            THEN( "every column is shown in the Log Format's order with its default width, and "
                  "nothing is kept" )
            {
                REQUIRE_FALSE( view.isColumnHidden( host ) );
                REQUIRE( view.columnLayout().shownColumns() == everyColumnInOrder( view ) );
                REQUIRE( view.columnWidth( timestamp ) == defaultTimestampWidth );
                REQUIRE_FALSE( loadColumnLayout( kept.format, columnNamesOf( view ) ).has_value() );
            }
        }
    }
}

SCENARIO( "A Row is unchanged by its Column Layout: Copy copies every cell",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );
    MenuTableView view;
    open( view, kept.format, logData );
    selectRows( view, { 0 } );
    const auto everyCell = view.selectedText();
    REQUIRE( everyCell.contains( "host1" ) );

    GIVEN( "the host column hidden" )
    {
        hideColumn( view, "host" );

        THEN( "the selected Row's text still holds the host cell" )
        {
            REQUIRE( view.selectedText() == everyCell );
        }

        WHEN( "QuickFind finds text in the hidden column" )
        {
            auto pattern = std::make_shared<QuickFindPattern>();
            view.setQuickFindPattern( pattern );
            pattern->changeSearchPattern( "host2", false, false );
            QSignalSpy newSelection( &view, &LogTableView::newSelection );
            view.searchForward();

            THEN( "its Row is selected, and no characters of the hidden cell" )
            {
                REQUIRE( ( !newSelection.isEmpty() || newSelection.wait( 10000 ) ) );
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 1_lnum } );
                REQUIRE_FALSE( view.selection().hasInCellSelection() );
            }
        }
    }
}

namespace {

// A loaded Log File of Lines, searched for the host of Log Line 2: that Row
// is the one Match.
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
        filteredData->request( RegularExpressionPattern( "host3" ) );
        REQUIRE( waitUiState( [ this ]() {
            const auto state = filteredData->searchState();
            return state.phase == SearchSession::Phase::Complete
                   && state.matchCount == LinesCount( 1 );
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "logtableview_columnlayout_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

// The color the Row is painted in, right of the text of its cell in column.
QColor rowColorAt( LogTableView& view, int row, int column )
{
    const auto cell = view.visualRect( view.model()->index( row, column ) );
    const auto image = view.viewport()->grab().toImage();
    return image.pixelColor( cell.right() - 2, cell.center().y() );
}

} // namespace

SCENARIO( "A Row whose match is in a hidden column is still painted as a Match",
          "[logtableview][columnlayout]" )
{
    const NoKeptLayout kept( makeFormat() );
    SearchedLogFile logFile;
    MenuTableView view;
    const auto policies = testSettingsPolicies();
    view.setDecorationPolicy( policies.decoration );
    view.setPresentationPolicy( policies.presentation );
    view.updateFont( QApplication::font() );
    open( view, kept.format, logFile.logData );
    view.setCurrentSearch( logFile.filteredData.get() );
    view.setActive( true );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const int body = columnOf( view, "body" );

    // Rows 0 and 2 alternate alike: only the Match sets them apart.
    const auto matchColor = rowColorAt( view, 2, body );
    REQUIRE( matchColor != rowColorAt( view, 0, body ) );

    WHEN( "the host column, which holds the matched text, is hidden" )
    {
        hideColumn( view, "host" );
        QCoreApplication::processEvents();

        THEN( "the Row is still painted as a Match" )
        {
            REQUIRE( view.isColumnHidden( columnOf( view, "host" ) ) );
            REQUIRE( rowColorAt( view, 2, body ) == matchColor );
            REQUIRE( rowColorAt( view, 0, body ) != matchColor );
        }
    }
}

namespace {

QByteArray contentOf( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

QByteArray csvFile( std::initializer_list<QByteArray> lines )
{
    QByteArray bytes = "\xEF\xBB\xBF";
    for ( const auto& line : lines ) {
        bytes += line + "\r\n";
    }
    return bytes;
}

} // namespace

SCENARIO( "Export as CSV in the Table View follows the Column Layout",
          "[logtableview][columnlayout][csvexport]" )
{
    const NoKeptLayout kept( makeFormat() );
    FakeLogData logData( Lines );
    MenuTableView view;
    open( view, kept.format, logData );
    const auto names = columnNamesOf( view );

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GIVEN( "host hidden and body moved first" )
    {
        hideColumn( view, "host" );
        moveFirst( view, columnOf( view, "body" ) );

        WHEN( "the dialog is set up" )
        {
            const auto setup = view.csvExportSetup();

            THEN( "it lists the columns in the shown order, Line and the hidden host unchecked" )
            {
                QStringList expectedNames{ "Line", "body" };
                std::vector<bool> expectedChecked{ false, true };
                for ( const auto& name : names ) {
                    if ( name != "body" ) {
                        expectedNames << name;
                        expectedChecked.push_back( name != "host" );
                    }
                }
                QStringList listedNames;
                std::vector<bool> listedChecked;
                for ( const auto& column : setup.columns ) {
                    listedNames << column.name;
                    listedChecked.push_back( column.checked );
                }
                REQUIRE( listedNames == expectedNames );
                REQUIRE( listedChecked == expectedChecked );
            }
        }

        WHEN( "the hidden host column is checked and exported with the body" )
        {
            const auto setup = view.csvExportSetup();
            CsvExportDialog::Choices choices;
            choices.fileName = dir.filePath( "export.csv" );
            choices.header = true;
            for ( size_t index = 0; index < setup.columns.size(); ++index ) {
                if ( setup.columns[ index ].name == "host" ) {
                    choices.columns.push_back( index );
                }
            }
            choices.columns.push_back( 1 );
            view.exportCsvTo( choices );

            THEN( "the file holds the host values" )
            {
                REQUIRE( contentOf( choices.fileName )
                         == csvFile( { "host,body", "host1,first message", "host2,second message",
                                       "host3,third message" } ) );
            }
        }
    }
}
