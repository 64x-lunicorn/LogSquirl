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
#include "quickfindpattern.h"
#include "rowmapping.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
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
    QSettings{}.remove( settingsGroup );

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

    QSettings{}.remove( settingsGroup );
}

namespace {

// A Text View, to hold a selection of its own next to the Table View.
class TextView : public AbstractLogView {
public:
    TextView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, quickFindPattern, false, nullptr )
    {
    }

protected:
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
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
