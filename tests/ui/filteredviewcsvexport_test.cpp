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

// The Filtered View exports its Displayed Lines as CSV, split into the fields
// of the recognized Log Format, with the values the Table View shows for them
// (#577).

#include "configuration.h"
#include "csv.h"
#include "filteredview.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <catch2/catch_test_macros.hpp>

namespace {

// Timestamp, level and message, with an elapsed-time column after the
// timestamp.
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "filteredviewcsvexport_test" );
    format.setTitle( "Filtered View CSV export test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{2}:\d{2}:\d{2}\.\d{3}) (?<level>[A-Z]+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setTimestampFormats( { "%H:%M:%S.%L" } );
    format.setLevelField( "level" );
    format.setBodyField( "body" );
    return format;
}

// The ERROR Log Lines 1, 5 and 9 are the Matches; with one Context Line each
// side, 0, 2, 4, 6 and 8 are Context Lines.
const QStringList SampleLines = {
    "10:00:00.000 INFO start",    "10:00:00.004 ERROR disk \"sda\" full",
    "    at Foo.bar(Foo.java:1)", "10:00:01.000 INFO retrying",
    "10:00:02.500 INFO waiting",  "10:00:12.304 ERROR failed; again",
    "10:05:14.304 INFO done",     "10:05:15.000 INFO idle",
    "10:05:16.000 INFO idle",     "10:06:00.000 ERROR last",
};

// A loaded Log File of SampleLines, searched for ERROR with one Context Line
// either side, Log Line 5 and 7 marked.
struct ExportLogFile {
    ExportLogFile()
        : logData( policies.indexing, searchPolicy(), policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( const auto& line : SampleLines ) {
            file.write( line.toUtf8() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
        filteredData->request( RegularExpressionPattern( "ERROR" ) );
        REQUIRE( waitUiState( [ this ]() {
            const auto state = filteredData->searchState();
            return state.phase == SearchSession::Phase::Complete
                   && state.matchCount == LinesCount( 3 );
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

        filteredData->addMark( 5_lnum );
        filteredData->addMark( 7_lnum );
    }

    SearchPolicy searchPolicy() const
    {
        auto search = policies.search;
        search.contextLinesCount = 1;
        return search;
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "filtered_view_csv_export_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

class ExportingFilteredView : public FilteredView {
public:
    using FilteredView::createContextMenu;
    using FilteredView::FilteredView;
};

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

using Lines = std::vector<std::string>;

// The lines of a CSV file, without the Byte Order Mark and the CR LF ends.
Lines csvLinesOf( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    auto content = file.readAll();
    REQUIRE( content.startsWith( "\xEF\xBB\xBF" ) );
    REQUIRE( content.endsWith( "\r\n" ) );
    content = content.mid( 3 );
    content.chop( 2 );
    Lines lines;
    for ( const auto& line : QString::fromUtf8( content ).split( QStringLiteral( "\r\n" ) ) ) {
        lines.push_back( line.toStdString() );
    }
    return lines;
}

} // namespace

SCENARIO( "The Filtered View offers Export as CSV only for a recognized Log Format",
          "[filteredview][csvexport]" )
{
    ExportLogFile logFile;
    QuickFindPattern quickFindPattern;
    ExportingFilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    GIVEN( "no Log Format recognized" )
    {
        view.setRecognizedFormat( []() { return FilteredView::RecognizedFormat{}; } );

        THEN( "the context menu has no Export as CSV" )
        {
            const auto menu = view.createContextMenu( QPoint( 1, 1 ) );
            REQUIRE( !entriesOf( *menu ).contains( "Export as CSV..." ) );
        }
    }

    GIVEN( "a Log Format recognized" )
    {
        const auto format = std::make_shared<const LogFormatDefinition>( makeFormat() );
        view.setRecognizedFormat( [ format ]() {
            return FilteredView::RecognizedFormat{ format, {}, QStringLiteral( "/logs/app.log" ) };
        } );

        THEN( "the context menu ends with Export as CSV" )
        {
            const auto menu = view.createContextMenu( QPoint( 1, 1 ) );
            REQUIRE( entriesOf( *menu ).last() == "Export as CSV..." );
        }

        WHEN( "the dialog is set up while Context Lines are hidden" )
        {
            view.setVisibility( LogFilteredData::VisibilityFlags::Matches
                                | LogFilteredData::VisibilityFlags::Marks );
            const auto setup = view.csvExportSetup();

            THEN( "it offers the shown lines, Include Context Lines disabled and unchecked, the "
                  "Line and Type columns unchecked and every column of the Table View checked" )
            {
                REQUIRE( setup.allRowsText == "All shown lines" );
                REQUIRE( setup.selectedRowsText == "Selected lines" );
                REQUIRE( !setup.hasSelection );
                REQUIRE( setup.rowOptions.size() == 1 );
                REQUIRE( setup.rowOptions[ 0 ].text == "Include Context Lines" );
                REQUIRE( !setup.rowOptions[ 0 ].enabled );
                REQUIRE( !setup.rowOptions[ 0 ].checked );

                QStringList names;
                std::vector<bool> checked;
                for ( const auto& column : setup.columns ) {
                    names << column.name;
                    checked.push_back( column.checked );
                }
                REQUIRE( names
                         == QStringList{ "Line", "Type", "timestamp", "Δt", "level", "body" } );
                REQUIRE( checked == std::vector<bool>{ false, false, true, true, true, true } );
                REQUIRE( setup.proposedFileName == "/logs/app.log.csv" );
            }
        }

        WHEN( "the dialog is set up while Context Lines are shown and a line is selected" )
        {
            view.setVisibility( LogFilteredData::VisibilityFlags::Matches
                                | LogFilteredData::VisibilityFlags::Marks
                                | LogFilteredData::VisibilityFlags::Context );
            view.selectAndDisplayLine( 5_lnum );
            const auto setup = view.csvExportSetup();

            THEN( "Include Context Lines is enabled, still unchecked, and the selected lines "
                  "are offered" )
            {
                REQUIRE( setup.hasSelection );
                REQUIRE( setup.rowOptions[ 0 ].enabled );
                REQUIRE( !setup.rowOptions[ 0 ].checked );
            }
        }
    }
}

SCENARIO( "The Filtered View exports the Matches of a Search as the Table View shows them",
          "[filteredview][csvexport]" )
{
    ExportLogFile logFile;
    QuickFindPattern quickFindPattern;
    ExportingFilteredView view( logFile.filteredData.get(), &quickFindPattern, false );
    view.resize( 400, 200 );
    showUntilExposed( view );
    view.registerShortcuts();
    const auto format = std::make_shared<const LogFormatDefinition>( makeFormat() );
    view.setRecognizedFormat(
        [ format ]() { return FilteredView::RecognizedFormat{ format, {}, {} }; } );

    // What the Table View shows for every Log Line of the Log File.
    LogFormatTableModel model( *format, &logFile.logData );
    model.setLineCount( static_cast<int>( SampleLines.size() ) );
    REQUIRE( model.columnCount() == 4 );
    const auto shownByTable = [ & ]( int logLine ) {
        QStringList cells;
        for ( int column = 0; column < model.columnCount(); ++column ) {
            cells << model.data( model.index( logLine, column ) ).toString();
        }
        return cells;
    };

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    CsvExportDialog::Choices choices;
    choices.fileName = dir.filePath( "export.csv" );
    choices.rowOptions = { false };
    choices.header = false;

    GIVEN( "Matches and Marks shown, and every column of the Table View chosen" )
    {
        view.setVisibility( LogFilteredData::VisibilityFlags::Matches
                            | LogFilteredData::VisibilityFlags::Marks );
        choices.columns = { 2, 3, 4, 5 };

        WHEN( "the Filtered View exports all shown lines" )
        {
            view.exportCsvTo( choices );

            THEN( "each exported line holds what the Table View shows for its Log Line, Δt "
                  "measured against the Log File" )
            {
                const auto lines = csvLinesOf( choices.fileName );
                const std::vector<int> exported = { 1, 5, 7, 9 };
                REQUIRE( lines.size() == exported.size() );
                for ( size_t index = 0; index < exported.size(); ++index ) {
                    INFO( "Log Line " << exported[ index ] );
                    REQUIRE( lines[ index ]
                             == csvLine( shownByTable( exported[ index ] ), QLatin1Char( ',' ) )
                                    .toStdString() );
                }
                // Log Line 0 was not exported, yet Log Line 1's Δt counts from it.
                REQUIRE( lines[ 0 ] == R"(10:00:00.004,+0.004s,ERROR,"disk ""sda"" full")" );
            }
        }
    }

    GIVEN( "Context Lines shown, and the Line and Type columns chosen with the body" )
    {
        view.setVisibility( LogFilteredData::VisibilityFlags::Matches
                            | LogFilteredData::VisibilityFlags::Marks
                            | LogFilteredData::VisibilityFlags::Context );
        choices.columns = { 0, 1, 3, 5 };
        choices.header = true;

        WHEN( "the Filtered View exports without Context Lines" )
        {
            view.exportCsvTo( choices );

            THEN( "only the Matches and Marks are written, each with its type" )
            {
                REQUIRE( csvLinesOf( choices.fileName )
                         == Lines{ "Line,Type,Δt,body", R"(2,Match,+0.004s,"disk ""sda"" full")",
                                   "6,Match+Mark,+9.8s,failed; again", "8,Mark,+0.696s,idle",
                                   "10,Match,+44.0s,last" } );
            }
        }

        WHEN( "the Filtered View exports with Context Lines" )
        {
            choices.rowOptions = { true };
            view.exportCsvTo( choices );

            THEN( "every shown line is written in the Filtered View's order, a Context Line "
                  "the Log Format doesn't match whole in the last column" )
            {
                REQUIRE( csvLinesOf( choices.fileName )
                         == Lines{ "Line,Type,Δt,body", "1,Context,,start",
                                   R"(2,Match,+0.004s,"disk ""sda"" full")",
                                   "3,Context,,    at Foo.bar(Foo.java:1)",
                                   "5,Context,+1.5s,waiting", "6,Match+Mark,+9.8s,failed; again",
                                   "7,Context,+5m02s,done", "8,Mark,+0.696s,idle",
                                   "9,Context,+1.0s,idle", "10,Match,+44.0s,last" } );
                // Log Line 3 is not shown, so it is not exported.
            }
        }

        WHEN( "the selected lines are exported, with and without Context Lines" )
        {
            view.selectAndDisplayLine( 1_lnum );
            triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
            triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
            choices.selectedRowsOnly = true;
            choices.columns = { 0, 1 };

            view.exportCsvTo( choices );
            const auto withoutContext = csvLinesOf( choices.fileName );
            choices.rowOptions = { true };
            view.exportCsvTo( choices );
            const auto withContext = csvLinesOf( choices.fileName );

            THEN( "only the selected lines are written, the Context Lines among them only when "
                  "chosen" )
            {
                REQUIRE( withoutContext == Lines{ "Line,Type", "2,Match" } );
                REQUIRE( withContext == Lines{ "Line,Type", "2,Match", "3,Context", "5,Context" } );
            }
        }
    }
}
