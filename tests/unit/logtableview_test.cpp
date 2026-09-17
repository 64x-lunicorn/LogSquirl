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

#include <optional>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "persistentinfo.h"
#include "quickfindpattern.h"
#include "rowmapping.h"
#include "test_policies.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QHeaderView>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

// A Log Format of its own, so the column widths this test saves cannot meet
// any a real Log Format has saved.
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_test_column_widths" );
    format.setTitle( "Table View column widths test" );

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

const QStringList Lines = {
    "Jan  1 12:00:00 host1 first message",
    "Jan  1 12:00:01 host2 second message",
};

constexpr int WidenedWidth = 321;

// Show the Log File in a Table View, and let its deferred column sizing run.
void open( LogTableView& view, const LogFormatDefinition& format, FakeLogData& logData )
{
    view.resize( 800, 400 );
    view.setLogFormat( &format, &logData );
    view.updateData( nullptr, false );
    QTest::qWait( 20 );
}

} // namespace

SCENARIO( "Column widths saved for a Log Format are restored when its Log File is opened again",
          "[logtableview][columnwidths]" )
{
    const auto format = makeFormat();
    FakeLogData logData( Lines );
    const QString settingsGroup = "logformat/columns/" + format.name();
    PersistentInfo::getSettings( app_settings{} ).remove( settingsGroup );

    GIVEN( "a Table View in which the user widened the first column" )
    {
        {
            LogTableView view;
            open( view, format, logData );
            REQUIRE( view.columnWidth( 0 ) != WidenedWidth );

            view.horizontalHeader()->resizeSection( 0, WidenedWidth );
        }

        WHEN( "the Log File is opened again in a new Table View" )
        {
            LogTableView view;
            open( view, format, logData );

            THEN( "the first column has the width the user gave it" )
            {
                REQUIRE( view.columnWidth( 0 ) == WidenedWidth );
            }
        }
    }

    PersistentInfo::getSettings( app_settings{} ).remove( settingsGroup );
}

namespace {

// A Text View, to hold a selection of its own next to the Table View.
class TextView : public AbstractLogView {
public:
    TextView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, quickFindPattern, false, nullptr )
    {
    }

    using AbstractLogView::createContextMenu;
};

const QStringList SaveLines = {
    "Jan  1 12:00:00 host1 first message", "Jan  1 12:00:01 host2 second message",
    "Jan  1 12:00:02 host3 third message", "Jan  1 12:00:03 host4 fourth message",
    "Jan  1 12:00:04 host5 fifth message",
};

void selectRows( LogTableView& view, std::initializer_list<int> rows )
{
    view.clearSelection();
    for ( const auto row : rows ) {
        view.selectionModel()->select( view.model()->index( row, 0 ),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows );
    }
}

QByteArray utf8File( std::initializer_list<QString> lines )
{
#if defined( Q_OS_WIN )
    const QByteArray lineEnding = "\n";
#else
    const QByteArray lineEnding = "\r\n";
#endif
    QByteArray bytes = "\xEF\xBB\xBF";
    for ( const auto& line : lines ) {
        bytes += line.toUtf8() + lineEnding;
    }
    return bytes;
}

QByteArray contentOf( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

} // namespace

SCENARIO( "Save selected to file in the Table View writes the Log Lines of the selected Rows",
          "[logtableview][save]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SaveLines );
    LogTableView view;
    open( view, format, logData );

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fileName = dir.filePath( "logtableview_save_test.log" );

    GIVEN( "Rows selected in the Table View, not next to each other and out of order" )
    {
        selectRows( view, { 3, 1 } );

        WHEN( "the selection is saved" )
        {
            view.saveSelectedTo( fileName );

            THEN( "the file holds exactly the Log Lines of those Rows, in Log Line order" )
            {
                REQUIRE( contentOf( fileName ) == utf8File( { SaveLines[ 1 ], SaveLines[ 3 ] } ) );
            }
        }
    }

    GIVEN( "a Text View of the same Log File with every Log Line selected" )
    {
        QuickFindPattern quickFindPattern;
        TextView textView( &logData, &quickFindPattern );
        textView.selectAll();
        REQUIRE( !textView.getSelectedText().isEmpty() );

        AND_GIVEN( "one Row selected in the Table View" )
        {
            selectRows( view, { 2 } );

            WHEN( "the Table View's selection is saved" )
            {
                view.saveSelectedTo( fileName );

                THEN( "the file holds only the Log Line of that Row" )
                {
                    REQUIRE( contentOf( fileName ) == utf8File( { SaveLines[ 2 ] } ) );
                }
            }
        }

        AND_GIVEN( "no Row selected in the Table View" )
        {
            view.clearSelection();

            WHEN( "the Table View's selection is saved" )
            {
                view.saveSelectedTo( fileName );

                THEN( "no file is written" )
                {
                    REQUIRE( !QFile::exists( fileName ) );
                }
            }
        }
    }
}

SCENARIO( "Save selected to file in the Table View's context menu needs a selected Row",
          "[logtableview][save]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SaveLines );
    LogTableView view;
    open( view, format, logData );
    view.show();
    QCoreApplication::processEvents();

    // Opens the context menu over the first Row, and tells whether its Save
    // selected to file entry is enabled; nullopt when there is no such entry.
    const auto openMenuForSaveSelectedEntry = [ &view ]() {
        std::optional<bool> entryEnabled;
        QTimer poll;
        QObject::connect( &poll, &QTimer::timeout, &poll, [ & ]() {
            auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() );
            if ( menu == nullptr ) {
                return;
            }
            poll.stop();
            for ( const auto* action : menu->actions() ) {
                if ( action->text() == "Save selected to file" ) {
                    entryEnabled = action->isEnabled();
                }
            }
            menu->close();
        } );
        poll.start( 10 );

        Q_EMIT view.customContextMenuRequested(
            view.visualRect( view.model()->index( 0, 0 ) ).center() );
        return entryEnabled;
    };

    GIVEN( "no Row selected" )
    {
        view.clearSelection();

        WHEN( "the context menu is opened" )
        {
            const auto entryEnabled = openMenuForSaveSelectedEntry();

            THEN( "Save selected to file is disabled" )
            {
                REQUIRE( entryEnabled == false );
            }
        }
    }

    GIVEN( "a Row selected" )
    {
        selectRows( view, { 1 } );

        WHEN( "the context menu is opened" )
        {
            const auto entryEnabled = openMenuForSaveSelectedEntry();

            THEN( "Save selected to file is enabled" )
            {
                REQUIRE( entryEnabled == true );
            }
        }
    }
}

namespace {

// Rows showing the Log File from Log Line 100 on: Row 0 is Log Line 100.
class RowsFromLogLine100 : public RowMapping {
public:
    static constexpr uint64_t FirstLogLine = 100;

    int rowCount( LinesCount logLines ) const override
    {
        return logLines.get() > FirstLogLine ? static_cast<int>( logLines.get() - FirstLogLine )
                                             : 0;
    }

    LineNumber logLineAt( int row ) const override
    {
        return LineNumber( FirstLogLine + static_cast<uint64_t>( row ) );
    }

    std::optional<int> rowOf( LineNumber logLine ) const override
    {
        if ( logLine.get() < FirstLogLine ) {
            return std::nullopt;
        }
        return static_cast<int>( logLine.get() - FirstLogLine );
    }
};

// 105 Log Lines; the ones from Log Line 100 on have the Log Format's shape.
QStringList offsetLines()
{
    QStringList lines;
    for ( int line = 0; line < 100; ++line ) {
        lines << QString( "noise %1" ).arg( line );
    }
    for ( int line = 100; line < 105; ++line ) {
        lines << QString( "Jan  1 12:00:%1 host%1 message %1" )
                     .arg( line - 100, 2, 10, QChar( '0' ) );
    }
    return lines;
}

// Opens the context menu over the first Row and triggers the entry with the
// given text.
void triggerContextMenuEntry( LogTableView& view, const QString& entry )
{
    QTimer poll;
    QObject::connect( &poll, &QTimer::timeout, &poll, [ & ]() {
        auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() );
        if ( menu == nullptr ) {
            return;
        }
        poll.stop();
        QAction* found = nullptr;
        for ( auto* action : menu->actions() ) {
            if ( action->text() == entry ) {
                found = action;
            }
        }
        menu->close();
        if ( found != nullptr ) {
            found->trigger();
        }
    } );
    poll.start( 10 );

    Q_EMIT view.customContextMenuRequested(
        view.visualRect( view.model()->index( 0, 0 ) ).center() );
}

} // namespace

SCENARIO( "A Table View whose Rows are not its Log Lines hands out Log Lines",
          "[logtableview][rowmapping]" )
{
    const auto format = makeFormat();
    const auto lines = offsetLines();
    FakeLogData logData( lines );
    LogTableView view( std::make_shared<RowsFromLogLine100>() );
    open( view, format, logData );
    view.setActive( true );
    view.show();
    QCoreApplication::processEvents();

    REQUIRE( view.model()->rowCount() == 5 );

    GIVEN( "the Rows 1 and 3 selected" )
    {
        QSignalSpy newSelection( &view, &LogTableView::newSelection );
        selectRows( view, { 1, 3 } );

        THEN( "the new selection is reported as the Log Line of the first Row" )
        {
            REQUIRE( !newSelection.isEmpty() );
            REQUIRE( newSelection.last().at( 0 ).value<LineNumber>() == 101_lnum );
        }

        WHEN( "they are marked" )
        {
            QSignalSpy markLines( &view, &LogTableView::markLines );
            QTest::keyClick( &view, Qt::Key_M );

            THEN( "their Log Lines are marked" )
            {
                REQUIRE( markLines.size() == 1 );
                REQUIRE( markLines.first().at( 0 ).value<logsquirl::vector<LineNumber>>()
                         == logsquirl::vector<LineNumber>{ 101_lnum, 103_lnum } );
            }
        }

        WHEN( "they are copied with line numbers" )
        {
            QApplication::clipboard()->clear();
            triggerContextMenuEntry( view, "Copy with line numbers" );

            THEN( "each is numbered with its Log Line, counted from 1" )
            {
                const auto copied = QApplication::clipboard()->text().split( '\n' );
                REQUIRE( copied.size() == 2 );
                REQUIRE( copied[ 0 ].startsWith( "102\t" ) );
                REQUIRE( copied[ 0 ].endsWith( "message 01" ) );
                REQUIRE( copied[ 1 ].startsWith( "104\t" ) );
                REQUIRE( copied[ 1 ].endsWith( "message 03" ) );
            }
        }

        WHEN( "they are saved" )
        {
            const QTemporaryDir dir;
            REQUIRE( dir.isValid() );
            const auto fileName = dir.filePath( "logtableview_rowmapping_test.log" );
            view.saveSelectedTo( fileName );

            THEN( "the file holds their Log Lines" )
            {
                REQUIRE( contentOf( fileName ) == utf8File( { lines[ 101 ], lines[ 103 ] } ) );
            }
        }
    }

    WHEN( "a Log Line is shown" )
    {
        view.showLogLine( 104_lnum );

        THEN( "the Row showing it is selected" )
        {
            REQUIRE( view.selectionModel()->isRowSelected( 4, {} ) );
            REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 104_lnum } );
        }
    }

    THEN( "the Log Line at a point is the one its Row shows" )
    {
        const auto point = view.visualRect( view.model()->index( 2, 0 ) ).center();
        REQUIRE( view.logLineAt( point ) == OptionalLineNumber{ 102_lnum } );
    }
}

namespace {

// A Table View whose context menu can be looked at without opening it.
class InspectedTableView : public LogTableView {
public:
    using LogTableView::createContextMenu;
    using LogTableView::LogTableView;
};

// The entries of a context menu, in order, without its separators.
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

// The shortcut shown with an entry of a context menu.
QKeySequence shortcutOf( const QMenu& menu, const QString& entry )
{
    for ( const auto* action : menu.actions() ) {
        if ( action->text() == entry ) {
            return action->shortcut();
        }
    }
    FAIL( "no entry " << entry.toStdString() );
    return {};
}

QPoint centerOfRow( const LogTableView& view, int row )
{
    return view.visualRect( view.model()->index( row, 0 ) ).center();
}

} // namespace

SCENARIO( "The Text View and the Table View offer one context menu", "[logtableview][contextmenu]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SaveLines );

    GIVEN( "one Log Line selected in the Text View and one Row in the Table View" )
    {
        QuickFindPattern quickFindPattern;
        TextView textView( &logData, &quickFindPattern );
        textView.resize( 800, 400 );
        textView.selectAndDisplayLine( 0_lnum );

        InspectedTableView tableView;
        open( tableView, format, logData );
        selectRows( tableView, { 0 } );

        WHEN( "each context menu is opened over the selected Log Line" )
        {
            const auto textMenu = textView.createContextMenu( QPoint( 100, 2 ) );
            const auto tableMenu = tableView.createContextMenu( centerOfRow( tableView, 0 ) );

            THEN( "both offer the same entries in the same order, and only the Text View lets "
                  "a selection start and end be set" )
            {
                const QStringList tableEntries = {
                    "Highlighters",
                    "Color labels",
                    "&Mark",
                    "&Copy this line",
                    "Copy this line with line number",
                    "Send to scratchpad",
                    "Replace scratchpad",
                    "Find &next",
                    "Find &previous",
                    "&Replace search",
                    "&Add to search",
                    "&Exclude from search",
                    "Set search start",
                    "Set search end",
                    "Clear search limits",
                    "Save splitter position",
                    "Save to file",
                    "Save selected to file",
                };
                auto textEntries = tableEntries;
                const auto beforeSplitter = textEntries.indexOf( "Save splitter position" );
                textEntries.insert( beforeSplitter, "Set selection end" );
                textEntries.insert( beforeSplitter, "Set selection start" );

                REQUIRE( entriesOf( *tableMenu ) == tableEntries );
                REQUIRE( entriesOf( *textMenu ) == textEntries );
            }

            THEN( "only the Text View, where * and / find, shows them as the shortcuts of Find "
                  "next and Find previous" )
            {
                REQUIRE( shortcutOf( *textMenu, "Find &next" ) == QKeySequence( "*" ) );
                REQUIRE( shortcutOf( *textMenu, "Find &previous" ) == QKeySequence( "/" ) );
                REQUIRE( shortcutOf( *tableMenu, "Find &next" ).isEmpty() );
                REQUIRE( shortcutOf( *tableMenu, "Find &previous" ).isEmpty() );
            }
        }
    }
}

namespace {

// Chooses the entry of a context menu, as if the user had.
void choose( const QMenu& menu, const QString& entry )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == entry ) {
            REQUIRE( action->isEnabled() );
            action->trigger();
            return;
        }
    }
    FAIL( "no entry " << entry.toStdString() );
}

// A Log Format whose body is the whole Log Line, so the characters of its
// cell are those of the Log Line.
LogFormatDefinition makeWholeLineFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_test_whole_line" );
    format.setTitle( "Table View whole line test" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setBodyField( "body" );

    return format;
}

// The column the Table View shows the body in; the two before it stay empty.
constexpr int BodyColumn = 2;

} // namespace

SCENARIO( "The Text View and the Table View copy a Log Line holding a null character alike",
          "[logtableview][copy]" )
{
    const auto format = makeWholeLineFormat();
    const QStringList lines = { QString( "before" ) + QChar( QChar::Null ) + QString( "after" ) };
    FakeLogData logData( lines );

    GIVEN( "the Log Line selected in the Text View, and in the Table View every character "
           "of the cell showing it" )
    {
        QuickFindPattern quickFindPattern;
        TextView textView( &logData, &quickFindPattern );
        textView.resize( 800, 400 );
        textView.selectAndDisplayLine( 0_lnum );

        InspectedTableView tableView;
        open( tableView, format, logData );
        tableView.setActive( true );
        tableView.show();
        QCoreApplication::processEvents();
        const auto cell = tableView.visualRect( tableView.model()->index( 0, BodyColumn ) );
        QTest::mousePress( tableView.viewport(), Qt::LeftButton, {},
                           QPoint( cell.left(), cell.center().y() ) );
        QTest::mouseMove( tableView.viewport(), QPoint( cell.right(), cell.center().y() ) );
        QTest::mouseRelease( tableView.viewport(), Qt::LeftButton, {},
                             QPoint( cell.right(), cell.center().y() ) );
        REQUIRE( tableView.selectedText().size() == lines[ 0 ].size() );

        WHEN( "each copies it from its context menu" )
        {
            QApplication::clipboard()->clear();
            choose( *textView.createContextMenu( QPoint( 100, 2 ) ), "&Copy this line" );
            const auto fromTextView = QApplication::clipboard()->text();

            QApplication::clipboard()->clear();
            choose( *tableView.createContextMenu( cell.center() ), "&Copy" );
            const auto fromTableView = QApplication::clipboard()->text();

            THEN( "both copy the same text, with a space for the null character" )
            {
                REQUIRE( fromTextView == "before after" );
                REQUIRE( fromTableView == fromTextView );
            }
        }
    }
}

SCENARIO( "Search Limits set from the Table View's context menu are those of the Log Line under "
          "the cursor",
          "[logtableview][contextmenu]" )
{
    const auto format = makeFormat();
    const auto lines = offsetLines();
    FakeLogData logData( lines );
    InspectedTableView view( std::make_shared<RowsFromLogLine100>() );
    open( view, format, logData );

    GIVEN( "the Rows showing Log Lines 101 and 103 selected, and the cursor over the Row showing "
           "Log Line 102" )
    {
        selectRows( view, { 1, 3 } );
        QSignalSpy changeSearchLimits( &view, &LogTableView::changeSearchLimits );
        const auto menu = view.createContextMenu( centerOfRow( view, 2 ) );

        WHEN( "Set search start is chosen" )
        {
            choose( *menu, "Set search start" );

            THEN( "the Search Limits start at Log Line 102 and end with the Log File" )
            {
                REQUIRE( changeSearchLimits.size() == 1 );
                REQUIRE( changeSearchLimits.first().at( 0 ).value<LineNumber>() == 102_lnum );
                REQUIRE( changeSearchLimits.first().at( 1 ).value<LineNumber>() == 105_lnum );
            }
        }

        WHEN( "Set search end is chosen" )
        {
            choose( *menu, "Set search end" );

            THEN( "the Search Limits start with the Log File and end after Log Line 102" )
            {
                REQUIRE( changeSearchLimits.size() == 1 );
                REQUIRE( changeSearchLimits.first().at( 0 ).value<LineNumber>() == 0_lnum );
                REQUIRE( changeSearchLimits.first().at( 1 ).value<LineNumber>() == 103_lnum );
            }
        }

        AND_GIVEN( "Search Limits from Log Line 101 to Log Line 104 already" )
        {
            view.setSearchLimits( 101_lnum, 104_lnum );

            WHEN( "Set search end is chosen" )
            {
                choose( *menu, "Set search end" );

                THEN( "the Search Limits keep their start" )
                {
                    REQUIRE( changeSearchLimits.size() == 1 );
                    REQUIRE( changeSearchLimits.first().at( 0 ).value<LineNumber>() == 101_lnum );
                    REQUIRE( changeSearchLimits.first().at( 1 ).value<LineNumber>() == 103_lnum );
                }
            }
        }
    }
}

namespace {

// 105 Log Lines; Log Line 99 mentions a needle, but only the ones from Log
// Line 100 on have the Log Format's shape, and there Log Lines 101 and 103 do.
QStringList needleLines()
{
    QStringList lines;
    for ( int line = 0; line < 99; ++line ) {
        lines << QString( "noise %1" ).arg( line );
    }
    lines << "noise needle";
    lines << "Jan  1 12:00:00 host0 alpha" << "Jan  1 12:00:01 host1 needle one"
          << "Jan  1 12:00:02 host2 beta" << "Jan  1 12:00:03 host3 needle two"
          << "Jan  1 12:00:04 host4 gamma";
    return lines;
}

// Double-clicks the first word of the cell of the Row whose text starts with it.
void doubleClickWord( LogTableView& view, int row, const QString& word )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        const auto index = view.model()->index( row, column );
        if ( index.data( Qt::DisplayRole ).toString().startsWith( word ) ) {
            const auto rect = view.visualRect( index );
            QTest::mouseDClick( view.viewport(), Qt::LeftButton, {},
                                QPoint( rect.left() + 6, rect.center().y() ) );
            return;
        }
    }
    FAIL( "no cell starting with " << word.toStdString() );
}

} // namespace

SCENARIO( "Find next and previous in the Table View's context menu select the Log Line matching "
          "the selected text",
          "[logtableview][contextmenu]" )
{
    const auto format = makeFormat();
    FakeLogData logData( needleLines() );
    InspectedTableView view( std::make_shared<RowsFromLogLine100>() );
    open( view, format, logData );
    view.setActive( true );
    view.show();
    QCoreApplication::processEvents();

    QSignalSpy newSelection( &view, &LogTableView::newSelection );
    const auto selectedLogLineAfterChoosing = [ & ]( const QString& entry ) {
        newSelection.clear();
        const auto menu = view.createContextMenu( centerOfRow( view, 0 ) );
        choose( *menu, entry );
        REQUIRE( ( !newSelection.isEmpty() || newSelection.wait( 10000 ) ) );
        return newSelection.last().at( 0 ).value<LineNumber>();
    };

    GIVEN( "the word needle selected in the Row showing Log Line 101" )
    {
        doubleClickWord( view, 1, "needle" );
        REQUIRE( view.selectedText() == "needle" );

        WHEN( "Find next is chosen" )
        {
            const auto selected = selectedLogLineAfterChoosing( "Find &next" );

            THEN( "the next Log Line mentioning a needle is selected" )
            {
                REQUIRE( selected == 103_lnum );
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 103_lnum } );
            }
        }
    }

    GIVEN( "the word needle selected in the Row showing Log Line 103" )
    {
        doubleClickWord( view, 3, "needle" );
        REQUIRE( view.selectedText() == "needle" );

        WHEN( "Find previous is chosen" )
        {
            const auto selected = selectedLogLineAfterChoosing( "Find &previous" );

            THEN( "the previous Log Line the Table View shows mentioning a needle is selected" )
            {
                REQUIRE( selected == 101_lnum );
                REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 101_lnum } );
            }
        }
    }
}

namespace {

// 103 Log Lines, the last three of the Log Format's shape. The body of the Row
// showing Log Line 101 is a lone regexp metacharacter: a word is letters,
// digits and underscores, so where there is no word to be found the character
// under the cursor is selected on its own -- whatever the font, and without
// the test having to place a selection by pixel. The Row after it holds that
// character too, so that a QuickFind for it finds a match and finishes.
QStringList metacharacterLines()
{
    QStringList lines;
    for ( int line = 0; line < 100; ++line ) {
        lines << QString( "noise %1" ).arg( line );
    }
    lines << "Jan  1 12:00:00 host0 alpha" << "Jan  1 12:00:01 host1 ."
          << "Jan  1 12:00:02 host2 beta.";
    return lines;
}

// Double-clicks the cell of the Row whose text is exactly text.
void doubleClickCell( LogTableView& view, int row, const QString& text )
{
    for ( int column = 0; column < view.model()->columnCount(); ++column ) {
        const auto index = view.model()->index( row, column );
        if ( index.data( Qt::DisplayRole ).toString() == text ) {
            QTest::mouseDClick( view.viewport(), Qt::LeftButton, {},
                                view.visualRect( index ).center() );
            return;
        }
    }
    FAIL( "no cell holding " << text.toStdString() );
}

} // namespace

SCENARIO(
    "The Table View reads the text a QuickFind searches for the way its QuickFind Policy says",
    "[logtableview][quickfindpolicy]" )
{
    // No settings store takes part: the Policy is a literal, and how the
    // selected text is read follows from it alone.
    const auto format = makeFormat();
    FakeLogData logData( metacharacterLines() );
    InspectedTableView view( std::make_shared<RowsFromLogLine100>() );
    auto quickFindPattern = std::make_shared<QuickFindPattern>();
    view.setQuickFindPattern( quickFindPattern );
    open( view, format, logData );
    view.setActive( true );
    view.show();
    QCoreApplication::processEvents();

    auto policy = testSettingsPolicies().quickFind;

    // Chooses an entry of the context menu over the Row holding the lone
    // metacharacter, and lets the QuickFind it starts finish: the search runs
    // off the UI thread, and none of it may outlive the view.
    const auto chooseAndLetTheQuickFindFinish = [ & ]( const QString& entry ) {
        const auto menu = view.createContextMenu( centerOfRow( view, 1 ) );
        QSignalSpy newSelection( &view, &LogTableView::newSelection );
        choose( *menu, entry );
        REQUIRE( ( !newSelection.isEmpty() || newSelection.wait( 10000 ) ) );
    };

    GIVEN( "the lone metacharacter of a Row selected" )
    {
        doubleClickCell( view, 1, "." );
        REQUIRE( view.selectedText() == "." );

        WHEN( "Find next is chosen under a Policy reading a pattern as an extended regexp" )
        {
            policy.quickFindRegexpType = SearchRegexpType::ExtendedRegexp;
            view.setQuickFindPolicy( policy );

            chooseAndLetTheQuickFindFinish( "Find &next" );

            THEN( "the QuickFind pattern is the selected text escaped, standing for itself "
                  "rather than for any character" )
            {
                REQUIRE( quickFindPattern->getPattern() == QRegularExpression::escape( "." ) );
            }
        }

        WHEN( "Find next is chosen under a Policy reading a pattern as a fixed string" )
        {
            policy.quickFindRegexpType = SearchRegexpType::FixedString;
            view.setQuickFindPolicy( policy );

            chooseAndLetTheQuickFindFinish( "Find &next" );

            THEN( "the QuickFind pattern is the selected text as it stands" )
            {
                REQUIRE( quickFindPattern->getPattern() == "." );
            }
        }
    }
}

namespace {

// Records the region of every paint event a widget receives.
class PaintedRegions : public QObject {
public:
    explicit PaintedRegions( QWidget* widget )
    {
        widget->installEventFilter( this );
    }

    QRegion painted;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( event->type() == QEvent::Paint ) {
            painted += static_cast<QPaintEvent*>( event )->region();
        }
        return QObject::eventFilter( watched, event );
    }
};

void moveMouseTo( LogTableView& view, const QPoint& position )
{
    QMouseEvent move( QEvent::MouseMove, position, view.viewport()->mapToGlobal( position ),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &move );
}

// Where a Row is drawn in the viewport, across its whole width.
QRect rowRect( const LogTableView& view, int row )
{
    return QRect( 0, view.rowViewportPosition( row ), view.viewport()->width(),
                  view.rowHeight( row ) );
}

} // namespace

SCENARIO( "Moving the mouse to another Row of the Table View repaints only the two Rows",
          "[logtableview][hover]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SaveLines );
    LogTableView view;
    open( view, format, logData );
    view.setActive( true );
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    REQUIRE( view.model()->rowCount() == 5 );

    GIVEN( "the mouse over Row 1" )
    {
        moveMouseTo( view, centerOfRow( view, 1 ) );
        QTest::qWait( 20 );
        PaintedRegions regions( view.viewport() );

        WHEN( "the mouse moves to Row 3" )
        {
            moveMouseTo( view, centerOfRow( view, 3 ) );
            QTest::qWait( 20 );

            THEN( "Rows 1 and 3 are repainted, and no other Row" )
            {
                REQUIRE( regions.painted.intersects( rowRect( view, 1 ) ) );
                REQUIRE( regions.painted.intersects( rowRect( view, 3 ) ) );
                REQUIRE( ( regions.painted - rowRect( view, 1 ) - rowRect( view, 3 ) ).isEmpty() );
            }
        }

        WHEN( "the mouse leaves the Table View" )
        {
            QEvent leave( QEvent::Leave );
            QCoreApplication::sendEvent( view.viewport(), &leave );
            QTest::qWait( 20 );

            THEN( "only Row 1 is repainted" )
            {
                REQUIRE( regions.painted.intersects( rowRect( view, 1 ) ) );
                REQUIRE( ( regions.painted - rowRect( view, 1 ) ).isEmpty() );
            }
        }
    }
}
