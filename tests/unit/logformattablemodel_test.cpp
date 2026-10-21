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

#include <catch2/catch_test_macros.hpp>

#include "fake_log_data.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"
#include "rowmapping.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QSignalSpy>

#include <memory>
#include <vector>

namespace {

// Build a simple syslog-like format for testing
LogFormatDefinition makeTestFormat()
{
    LogFormatDefinition def;
    def.setName( "test_log" );
    def.setTitle( "Test Log" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>\S+) (?<body>.*)$)";
    def.setRegexPatterns( regex );

    def.setTimestampField( "timestamp" );
    def.setLevelField( "level" );
    def.setBodyField( "body" );

    QHash<QString, LogFormatValueDef> values;
    values[ "host" ] = LogFormatValueDef{ "string", true, false };
    def.setValueDefinitions( values );

    return def;
}

} // namespace

SCENARIO( "LogFormatTableModel provides correct column count", "[logformat][tablemodel]" )
{
    GIVEN( "A table model with a format defining timestamp, body and one value" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "Column count is 5: timestamp, elapsed time, level, host, body" )
        {
            REQUIRE( model.columnCount() == 5 );
        }
    }
}

SCENARIO( "LogFormatTableModel returns proper column headers", "[logformat][tablemodel]" )
{
    GIVEN( "A table model with a syslog-like format" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "Column headers match the field names in deterministic order" )
        {
            // Order must be: timestamp, level, [value fields], body
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "\u0394t" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 3, Qt::Horizontal ).toString() == "host" );
            REQUIRE( model.headerData( model.columnCount() - 1, Qt::Horizontal ).toString()
                     == "body" );
        }
    }
}

SCENARIO( "LogFormatTableModel column order is stable with multiple value fields",
          "[logformat][tablemodel]" )
{
    GIVEN( "A format with multiple value definitions and no explicit order" )
    {
        LogFormatDefinition def;
        def.setName( "multi_val" );
        QHash<QString, QString> regex;
        regex[ "std" ]
            = R"(^(?<timestamp>\S+) (?<level>\w+) (?<zebra>\S+) (?<alpha>\S+) (?<middle>\S+) (?<body>.*)$)";
        def.setRegexPatterns( regex );
        def.setTimestampField( "timestamp" );
        def.setLevelField( "level" );
        def.setBodyField( "body" );

        QHash<QString, LogFormatValueDef> values;
        values[ "zebra" ] = LogFormatValueDef{ "string", false, false };
        values[ "alpha" ] = LogFormatValueDef{ "string", false, false };
        values[ "middle" ] = LogFormatValueDef{ "string", false, false };
        def.setValueDefinitions( values );
        // No valueFieldOrder set → falls back to alphabetical

        FakeLogData logData;
        LogFormatTableModel model( def, &logData );

        THEN( "Value field columns are sorted alphabetically (fallback)" )
        {
            // Expected order: timestamp, level, alpha, middle, zebra, body
            REQUIRE( model.columnCount() == 7 );
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 3, Qt::Horizontal ).toString() == "alpha" );
            REQUIRE( model.headerData( 4, Qt::Horizontal ).toString() == "middle" );
            REQUIRE( model.headerData( 5, Qt::Horizontal ).toString() == "zebra" );
            REQUIRE( model.headerData( 6, Qt::Horizontal ).toString() == "body" );
        }
    }
}

SCENARIO( "LogFormatTableModel preserves JSON field order when valueFieldOrder is set",
          "[logformat][tablemodel]" )
{
    GIVEN( "A format with explicit field order" )
    {
        LogFormatDefinition def;
        def.setName( "ordered_val" );
        QHash<QString, QString> regex;
        regex[ "std" ]
            = R"(^(?<timestamp>\S+) (?<level>\w+) (?<zebra>\S+) (?<alpha>\S+) (?<middle>\S+) (?<body>.*)$)";
        def.setRegexPatterns( regex );
        def.setTimestampField( "timestamp" );
        def.setLevelField( "level" );
        def.setBodyField( "body" );

        QHash<QString, LogFormatValueDef> values;
        values[ "zebra" ] = LogFormatValueDef{ "string", false, false };
        values[ "alpha" ] = LogFormatValueDef{ "string", false, false };
        values[ "middle" ] = LogFormatValueDef{ "string", false, false };
        def.setValueDefinitions( values );
        // valueFieldOrder now contains ALL capture group names in regex order
        def.setValueFieldOrder(
            QStringList{ "timestamp", "level", "zebra", "alpha", "middle", "body" } );

        FakeLogData logData;
        LogFormatTableModel model( def, &logData );

        THEN( "Value field columns follow JSON insertion order" )
        {
            // Expected order: timestamp, level, zebra, alpha, middle, body
            REQUIRE( model.columnCount() == 7 );
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 3, Qt::Horizontal ).toString() == "zebra" );
            REQUIRE( model.headerData( 4, Qt::Horizontal ).toString() == "alpha" );
            REQUIRE( model.headerData( 5, Qt::Horizontal ).toString() == "middle" );
            REQUIRE( model.headerData( 6, Qt::Horizontal ).toString() == "body" );
        }
    }
}

SCENARIO( "LogFormatTableModel row count starts at zero", "[logformat][tablemodel]" )
{
    GIVEN( "A newly constructed table model" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "Row count is 0" )
        {
            REQUIRE( model.rowCount() == 0 );
        }
    }
}

SCENARIO( "LogFormatTableModel data is extracted lazily via setLineCount",
          "[logformat][tablemodel]" )
{
    GIVEN( "A table model with a syslog-like format" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        WHEN( "Lines are set in the log data and line count is updated" )
        {
            logData.setLines( {
                "Jan  1 00:00:01 myhost first message",
                "Jan  1 00:00:02 myhost second message",
            } );
            model.setLineCount( 2 );

            THEN( "Row count matches" )
            {
                REQUIRE( model.rowCount() == 2 );
            }

            THEN( "Data is correctly extracted for first row" )
            {
                auto ts = model.data( model.index( 0, 0 ) ).toString();
                REQUIRE( ts == "Jan  1 00:00:01" );

                // body is last column
                auto body = model.data( model.index( 0, model.columnCount() - 1 ) ).toString();
                REQUIRE( body == "first message" );
            }

            THEN( "Data is correctly extracted for second row" )
            {
                auto ts = model.data( model.index( 1, 0 ) ).toString();
                REQUIRE( ts == "Jan  1 00:00:02" );
            }
        }
    }
}

SCENARIO( "LogFormatTableModel handles non-matching lines", "[logformat][tablemodel]" )
{
    GIVEN( "A table model with a syslog-like format" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        WHEN( "A non-matching line is set" )
        {
            logData.setLines( { "this does not match the regex at all" } );
            model.setLineCount( 1 );

            THEN( "Row count is still 1" )
            {
                REQUIRE( model.rowCount() == 1 );
            }

            THEN( "Body column contains the raw line" )
            {
                auto body = model.data( model.index( 0, model.columnCount() - 1 ) ).toString();
                REQUIRE( body == "this does not match the regex at all" );
            }
        }
    }
}

SCENARIO( "The table model says where the text of each column lies in the Log Line",
          "[logformat][tablemodel][columnspans]" )
{
    const auto spansOf = []( const LogFormatTableModel& model, int row ) {
        const auto spans = model.columnSpans( row );
        REQUIRE( spans.has_value() );
        std::vector<std::optional<std::pair<int, int>>> ends;
        for ( const auto& span : *spans ) {
            ends.push_back( span ? std::optional{ std::pair{ span->start, span->end } }
                                 : std::nullopt );
        }
        return ends;
    };
    using Ends = std::vector<std::optional<std::pair<int, int>>>;

    GIVEN( "a table model of a regex Log Format: timestamp, elapsed time, level, host, body" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );
        logData.setLines( { "Jan  1 12:00:00 host1 some message", "no match here" } );
        model.setLineCount( 2 );

        THEN( "a Log Line it matches has the span of each field's capture in its column, and "
              "none for the elapsed time and the level, which it did not capture" )
        {
            REQUIRE( spansOf( model, 0 )
                     == Ends{ std::pair{ 0, 15 }, std::nullopt, std::nullopt, std::pair{ 16, 21 },
                              std::pair{ 22, 34 } } );
        }

        THEN( "a Log Line it does not match lies whole in the last column" )
        {
            REQUIRE( spansOf( model, 1 )
                     == Ends{ std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                              std::pair{ 0, 13 } } );
        }

        THEN( "there is nothing for no Row" )
        {
            REQUIRE_FALSE( model.columnSpans( 2 ).has_value() );
        }
    }

    GIVEN( "a table model of a JSON Log Format" )
    {
        LogFormatDefinition format;
        format.setName( "test_json" );
        format.setKind( LogFormatKind::Json );
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );
        logData.setLines( { R"({"timestamp":"2026-01-01","body":"x"})" } );
        model.setLineCount( 1 );

        THEN( "there is nothing: its fields have no place in the Log Line" )
        {
            REQUIRE_FALSE( model.columnSpans( 0 ).has_value() );
        }
    }
}

SCENARIO( "LogFormatTableModel passes Qt model tester", "[logformat][tablemodel]" )
{
    GIVEN( "A table model with lines" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;
        logData.setLines( {
            "Jan  1 00:00:01 myhost first message",
            "Jan  1 00:00:02 myhost second message",
        } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 2 );

        THEN( "QAbstractItemModelTester does not crash" )
        {
            // This will assert internally if the model behaves incorrectly
            QAbstractItemModelTester tester(
                &model, QAbstractItemModelTester::FailureReportingMode::Fatal );
            REQUIRE( true );
        }
    }
}

SCENARIO( "The table model takes a Sort Order's Row Mapping and merges Rows into it",
          "[logformat][tablemodel][sortorder]" )
{
    auto format = makeTestFormat();
    FakeLogData logData;
    logData.setLines( {
        "Jan  1 00:00:01 hostC first message",
        "Jan  1 00:00:02 hostA second message",
        "Jan  1 00:00:03 hostB third message",
    } );
    LogFormatTableModel model( format, &logData );
    model.setLineCount( 3 );
    QAbstractItemModelTester tester( &model,
                                     QAbstractItemModelTester::FailureReportingMode::Fatal );
    const int host = 3;
    REQUIRE( model.headerData( host, Qt::Horizontal ).toString() == "host" );
    REQUIRE( model.index( 0, host ).data().toString() == "hostC" );

    GIVEN( "the Rows mapped onto Log Lines 1, 2, 0" )
    {
        QSignalSpy reset( &model, &QAbstractItemModel::modelReset );
        model.setRowMapping( std::make_shared<SortedRows>( std::vector<uint32_t>{ 1, 2, 0 } ) );

        THEN( "the views were reset, and each Row shows its Log Line" )
        {
            REQUIRE( reset.count() == 1 );
            REQUIRE( model.rowCount() == 3 );
            REQUIRE( model.index( 0, host ).data().toString() == "hostA" );
            REQUIRE( model.index( 1, host ).data().toString() == "hostB" );
            REQUIRE( model.index( 2, host ).data().toString() == "hostC" );
            REQUIRE( model.rowMapping().rowOf( 0_lnum ) == 2 );
        }

        WHEN( "two appended Log Lines are merged in, one at the top and one at the end" )
        {
            logData.setLines( {
                "Jan  1 00:00:01 hostC first message",
                "Jan  1 00:00:02 hostA second message",
                "Jan  1 00:00:03 hostB third message",
                "Jan  1 00:00:04 host0 fourth message",
                "Jan  1 00:00:05 hostZ fifth message",
            } );
            model.setLineCount( 5 );
            REQUIRE( model.rowCount() == 3 );
            QSignalSpy inserted( &model, &QAbstractItemModel::rowsInserted );
            model.mergeRows( std::make_shared<SortedRows>( std::vector<uint32_t>{ 3, 1, 2, 0, 4 } ),
                             { RowRun{ 0, 1 }, RowRun{ 4, 1 } } );

            THEN( "the views were told of each run inserted, not reset, and the Rows show the "
                  "Log Lines in the new order" )
            {
                REQUIRE( reset.count() == 1 );
                REQUIRE( inserted.count() == 2 );
                REQUIRE( inserted[ 0 ][ 1 ].toInt() == 0 );
                REQUIRE( inserted[ 1 ][ 1 ].toInt() == 4 );
                REQUIRE( model.rowCount() == 5 );
                REQUIRE( model.index( 0, host ).data().toString() == "host0" );
                REQUIRE( model.index( 3, host ).data().toString() == "hostC" );
                REQUIRE( model.index( 4, host ).data().toString() == "hostZ" );
            }
        }

        WHEN( "the Rows are mapped back onto their Log Lines" )
        {
            model.setRowMapping( std::make_shared<OneRowPerLogLine>() );

            THEN( "they are in Log Line order again" )
            {
                REQUIRE( reset.count() == 2 );
                REQUIRE( model.index( 0, host ).data().toString() == "hostC" );
            }
        }
    }

    GIVEN( "a note on a header" )
    {
        QSignalSpy headerChanged( &model, &QAbstractItemModel::headerDataChanged );
        model.setHeaderNote( host, QStringLiteral( "host (sorting 40%)" ) );

        THEN( "the header reads the note, the name stays known, and dropping the note shows "
              "the name again" )
        {
            REQUIRE( headerChanged.count() == 1 );
            REQUIRE( model.headerData( host, Qt::Horizontal ).toString() == "host (sorting 40%)" );
            REQUIRE( model.columnName( host ) == "host" );
            model.setHeaderNote( -1, std::nullopt );
            REQUIRE( headerChanged.count() == 2 );
            REQUIRE( model.headerData( host, Qt::Horizontal ).toString() == "host" );
        }
    }
}

SCENARIO( "LogFormatTableModel body column returns full untruncated text",
          "[logformat][tablemodel][columnwidth]" )
{
    GIVEN( "A model with lines containing very long body text" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        // Create a line with a very long body (200+ characters)
        const QString longBody
            = "[4782:24004:1310123] Synchronizing local state: registrationComplete "
              "-> registrationDataAvailable via ConnectionManager::handleStateTransition"
              " with full diagnostic context enabled for debugging purposes";
        const auto line = QString( "Jan  1 12:00:00 myhost %1" ).arg( longBody );
        logData.setLines( { line } );

        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        const int bodyCol = model.columnCount() - 1;

        THEN( "Body column returns the full text without truncation" )
        {
            const auto bodyText = model.data( model.index( 0, bodyCol ) ).toString();
            REQUIRE( bodyText == longBody );
            REQUIRE( bodyText.length() == longBody.length() );
        }
    }
}

SCENARIO( "LogFormatTableModel RawLineRole returns full original line",
          "[logformat][tablemodel][columnwidth]" )
{
    GIVEN( "A model with lines" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        const QString line = "Jan  1 12:00:00 myhost some body text here";
        logData.setLines( { line } );

        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        THEN( "RawLineRole returns the full original line text" )
        {
            const auto rawLine
                = model.data( model.index( 0, 0 ), LogFormatTableModel::RawLineRole ).toString();
            REQUIRE( rawLine == line );
        }
    }
}

SCENARIO( "Column width computation produces widths that fit all sampled text",
          "[logformat][tablemodel][columnwidth]" )
{
    GIVEN( "A model with lines of varying lengths including a very long body" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        // Short body
        const QString shortLine = "Jan  1 00:00:01 host1 short";
        // Very long body (should drive body column width)
        const QString longBody
            = "[4782:24004:1310123] Synchronizing local state: registrationComplete "
              "-> registrationDataAvailable via ConnectionManager::handleStateTransition"
              " with full diagnostic context enabled for debugging purposes and more text";
        const QString longLine = QString( "Jan  1 00:00:02 host2 %1" ).arg( longBody );

        logData.setLines( { shortLine, longLine } );

        LogFormatTableModel model( format, &logData );
        model.setLineCount( 2 );

        const auto fm = QFontMetrics( QApplication::font() );
        constexpr int cellPadding = 16;

        THEN( "Computed column widths accommodate the widest text in each column" )
        {
            const int colCount = model.columnCount();
            const int rowCount = model.rowCount();

            QVector<int> maxWidths( colCount, 0 );

            // Compute widths the same way autoSizeTableColumns does
            for ( int col = 0; col < colCount; ++col ) {
                const auto headerText = model.headerData( col, Qt::Horizontal ).toString();
                maxWidths[ col ] = fm.horizontalAdvance( headerText ) + cellPadding;
            }
            for ( int row = 0; row < rowCount; ++row ) {
                for ( int col = 0; col < colCount; ++col ) {
                    const auto text = model.data( model.index( row, col ) ).toString();
                    if ( !text.isEmpty() ) {
                        const int w = fm.horizontalAdvance( text ) + cellPadding;
                        if ( w > maxWidths[ col ] ) {
                            maxWidths[ col ] = w;
                        }
                    }
                }
            }

            // Body column width must accommodate the long body text
            const int bodyCol = colCount - 1;
            const int bodyTextWidth = fm.horizontalAdvance( longBody ) + cellPadding;
            REQUIRE( maxWidths[ bodyCol ] >= bodyTextWidth );

            // All columns must have a positive width
            for ( int col = 0; col < colCount; ++col ) {
                REQUIRE( maxWidths[ col ] > 0 );
            }

            // Body column must be wider than the header alone
            const auto bodyHeader = model.headerData( bodyCol, Qt::Horizontal ).toString();
            const int bodyHeaderWidth = fm.horizontalAdvance( bodyHeader ) + cellPadding;
            REQUIRE( maxWidths[ bodyCol ] > bodyHeaderWidth );
        }
    }
}

SCENARIO( "Column width computation handles non-matching lines in body column",
          "[logformat][tablemodel][columnwidth]" )
{
    GIVEN( "A model with lines that do not match the regex" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        // Non-matching lines (e.g., log file headers) go entirely into body column
        const QString headerLine = "#----- BEGIN: Logging session 2026-04-20 10:57:52.000 -----";
        logData.setLines( { headerLine } );

        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        const auto fm = QFontMetrics( QApplication::font() );
        constexpr int cellPadding = 16;

        THEN( "Body column width accommodates the full non-matching line text" )
        {
            const int bodyCol = model.columnCount() - 1;
            const auto bodyText = model.data( model.index( 0, bodyCol ) ).toString();

            // Non-matching line should appear in body column
            REQUIRE( bodyText == headerLine );

            // Width must accommodate the full text
            const int requiredWidth = fm.horizontalAdvance( bodyText ) + cellPadding;
            REQUIRE( requiredWidth > 0 );
        }
    }
}

SCENARIO( "LogFormatTableModel cache invalidation on truncation", "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with cached rows" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        logData.setLines( {
            "Jan  1 00:00:01 host1 first message",
            "Jan  1 00:00:02 host2 second message",
            "Jan  1 00:00:03 host3 third message",
        } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 3 );

        // Warm the cache by reading all rows
        for ( int row = 0; row < 3; ++row ) {
            model.data( model.index( row, 0 ) );
        }

        WHEN( "Line count is reduced (file truncation)" )
        {
            logData.setLines( { "Jan  1 00:00:01 host1 first message" } );
            model.setLineCount( 1 );

            THEN( "Row count reflects the new line count" )
            {
                REQUIRE( model.rowCount() == 1 );
            }

            THEN( "Old rows beyond new count are not accessible" )
            {
                // Out-of-bounds row should return empty data
                REQUIRE_FALSE( model.data( model.index( 2, 0 ) ).isValid() );
            }
        }
    }
}

SCENARIO( "LogFormatTableModel handles setLineCount(0)", "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with data" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        logData.setLines( { "Jan  1 00:00:01 host1 msg" } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        WHEN( "Line count is set to 0" )
        {
            logData.setLines( {} );
            model.setLineCount( 0 );

            THEN( "Row count is 0" )
            {
                REQUIRE( model.rowCount() == 0 );
            }

            THEN( "No data is accessible" )
            {
                REQUIRE_FALSE( model.data( model.index( 0, 0 ) ).isValid() );
            }
        }
    }
}

SCENARIO( "LogFormatTableModel RawLineRole uses cache consistently",
          "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with lines" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        const QString line = "Jan  1 12:00:00 myhost some body text here";
        logData.setLines( { line } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        WHEN( "DisplayRole and RawLineRole are accessed for the same row" )
        {
            // Access DisplayRole first to warm cache
            const auto display = model.data( model.index( 0, 0 ) ).toString();
            // Then access RawLineRole
            const auto rawLine
                = model.data( model.index( 0, 0 ), LogFormatTableModel::RawLineRole ).toString();

            THEN( "Both return correct values without extra disk I/O" )
            {
                REQUIRE( display == "Jan  1 12:00:00" );
                REQUIRE( rawLine == line );
            }
        }

        WHEN( "RawLineRole is accessed first" )
        {
            const auto rawLine
                = model.data( model.index( 0, 0 ), LogFormatTableModel::RawLineRole ).toString();
            // Then access DisplayRole from cache
            const auto display = model.data( model.index( 0, 0 ) ).toString();

            THEN( "Both return correct values" )
            {
                REQUIRE( rawLine == line );
                REQUIRE( display == "Jan  1 12:00:00" );
            }
        }
    }
}

SCENARIO( "LogFormatTableModel handles negative row index", "[logformat][tablemodel][edge]" )
{
    GIVEN( "A table model with one line" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        logData.setLines( { "Jan  1 00:00:01 host1 msg" } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        THEN( "Negative row returns invalid data" )
        {
            REQUIRE_FALSE( model.data( model.index( -1, 0 ) ).isValid() );
        }

        THEN( "Negative column returns invalid data" )
        {
            REQUIRE_FALSE( model.data( model.index( 0, -1 ) ).isValid() );
        }

        THEN( "Column beyond count returns invalid data" )
        {
            REQUIRE_FALSE( model.data( model.index( 0, model.columnCount() ) ).isValid() );
        }
    }
}

SCENARIO( "LogFormatTableModel setLineCount is idempotent", "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with data" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        logData.setLines( { "Jan  1 00:00:01 host1 msg" } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        WHEN( "setLineCount is called again with the same value" )
        {
            model.setLineCount( 1 );

            THEN( "Row count stays the same and data is still valid" )
            {
                REQUIRE( model.rowCount() == 1 );
                REQUIRE( model.data( model.index( 0, 0 ) ).isValid() );
            }
        }
    }
}

SCENARIO( "LogFormatTableModel unsupported role returns empty variant",
          "[logformat][tablemodel][edge]" )
{
    GIVEN( "A table model with lines" )
    {
        auto format = makeTestFormat();
        FakeLogData logData;

        logData.setLines( { "Jan  1 00:00:01 host1 msg" } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        THEN( "EditRole returns empty variant" )
        {
            REQUIRE_FALSE( model.data( model.index( 0, 0 ), Qt::EditRole ).isValid() );
        }

        THEN( "DecorationRole returns empty variant" )
        {
            REQUIRE_FALSE( model.data( model.index( 0, 0 ), Qt::DecorationRole ).isValid() );
        }
    }
}

namespace {

LogFormatDefinition elapsedFormat( bool withTimestamp = true )
{
    LogFormatDefinition def;
    def.setName( "elapsed_log" );
    QHash<QString, QString> regex;
    regex[ "std" ] = R"(^(?<timestamp>\d{2}:\d{2}:\d{2}\.\d{3}) (?<body>.*)$)";
    def.setRegexPatterns( regex );
    def.setTimestampField( withTimestamp ? "timestamp" : "" );
    def.setBodyField( "body" );
    return def;
}

QString elapsedCell( const LogFormatTableModel& model, int row )
{
    return model.data( model.index( row, 1 ) ).toString();
}

} // namespace

TEST_CASE( "The elapsed time is shown compactly", "[logformat][tablemodel][elapsed]" )
{
    CHECK( LogFormatTableModel::formatElapsed( 0 ) == "+0.000s" );
    CHECK( LogFormatTableModel::formatElapsed( 4 ) == "+0.004s" );
    CHECK( LogFormatTableModel::formatElapsed( 999 ) == "+0.999s" );
    CHECK( LogFormatTableModel::formatElapsed( 1000 ) == "+1.0s" );
    CHECK( LogFormatTableModel::formatElapsed( 12'300 ) == "+12.3s" );
    CHECK( LogFormatTableModel::formatElapsed( 59'949 ) == "+59.9s" );
    CHECK( LogFormatTableModel::formatElapsed( 59'950 ) == "+1m00s" );
    CHECK( LogFormatTableModel::formatElapsed( 302'000 ) == "+5m02s" );
    CHECK( LogFormatTableModel::formatElapsed( 3'900'000 ) == "+1h05m" );
    CHECK( LogFormatTableModel::formatElapsed( 90'000'000 ) == "+1d01h" );
    CHECK( LogFormatTableModel::formatElapsed( -1000 ) == "-1.0s" );
    CHECK( LogFormatTableModel::formatElapsed( -4 ) == "-0.004s" );
}

SCENARIO( "The Table View shows the time elapsed since the previous Log Line with a Timestamp",
          "[logformat][tablemodel][elapsed]" )
{
    GIVEN( "a Log File with a stack trace between timestamped Log Lines" )
    {
        const auto format = elapsedFormat();
        FakeLogData logData;
        const QStringList lines = {
            "10:00:00.000 start",
            "10:00:00.004 a",
            "    at Foo.bar(Foo.java:1)",
            "    at Foo.baz(Foo.java:2)",
            "10:00:12.304 b",
            "10:05:14.304 c",
            "10:05:13.304 d",
        };
        logData.setLines( lines );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( static_cast<int>( lines.size() ) );

        THEN( "the column follows the timestamp column" )
        {
            REQUIRE( model.columnCount() == 4 );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "Δt" );
            REQUIRE( model.isElapsedColumn( 1 ) );
            REQUIRE( !model.isElapsedColumn( 0 ) );
            REQUIRE( model.data( model.index( 1, 0 ) ).toString() == "10:00:00.004" );
            REQUIRE( model.data( model.index( 1, 3 ) ).toString() == "a" );
        }

        THEN( "the first Log Line with a Timestamp has no elapsed time" )
        {
            REQUIRE( elapsedCell( model, 0 ).isEmpty() );
        }

        THEN( "a Log Line is compared with the one before it" )
        {
            REQUIRE( elapsedCell( model, 1 ) == "+0.004s" );
        }

        THEN( "a continuation line has none, and the line after it compares with the one before" )
        {
            REQUIRE( elapsedCell( model, 2 ).isEmpty() );
            REQUIRE( elapsedCell( model, 3 ).isEmpty() );
            REQUIRE( elapsedCell( model, 4 ) == "+12.3s" );
        }

        THEN( "long and negative differences keep their unit and sign" )
        {
            REQUIRE( elapsedCell( model, 5 ) == "+5m02s" );
            REQUIRE( elapsedCell( model, 6 ) == "-1.0s" );
        }

        THEN( "reading the Rows in another order gives the same cells" )
        {
            REQUIRE( elapsedCell( model, 6 ) == "-1.0s" );
            REQUIRE( elapsedCell( model, 4 ) == "+12.3s" );
            REQUIRE( elapsedCell( model, 1 ) == "+0.004s" );
        }
    }

    GIVEN( "a stack trace longer than the look-back" )
    {
        const auto format = elapsedFormat();
        FakeLogData logData;
        QStringList lines = { "10:00:00.000 start" };
        for ( uint64_t i = 1; i < LogFormatTableModel::ElapsedLookBack; ++i ) {
            lines << "    at frame";
        }
        lines << "10:00:01.000 within";
        lines << "    at frame";
        for ( uint64_t i = 0; i < LogFormatTableModel::ElapsedLookBack; ++i ) {
            lines << "    at frame";
        }
        lines << "10:00:02.000 beyond";
        logData.setLines( lines );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( static_cast<int>( lines.size() ) );

        THEN( "the bound is inclusive, and beyond it the cell stays empty" )
        {
            REQUIRE( elapsedCell( model, static_cast<int>( LogFormatTableModel::ElapsedLookBack ) )
                     == "+1.0s" );
            REQUIRE( elapsedCell( model, static_cast<int>( lines.size() ) - 1 ).isEmpty() );
        }
    }

    GIVEN( "a Log Format without a timestamp field" )
    {
        const auto format = elapsedFormat( false );
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "there is no elapsed column" )
        {
            REQUIRE( model.columnCount() == 2 );
            REQUIRE( !model.isElapsedColumn( 1 ) );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "body" );
        }
    }
}

// #826: a Row the regex engine gave up on shows that it is undecided, as the
// Search says of its Log Lines, and a Log Line of many megabytes is shown
// without a stall: past the UI thread's line length cap it is not matched.
namespace {

LogFormatDefinition backtrackingFormat()
{
    LogFormatDefinition def;
    def.setName( "backtracking_log" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<body>(a|aa)+)$)";
    def.setRegexPatterns( regex );
    def.setBodyField( "body" );
    return def;
}

// Several ".*" segments, as the built-in haproxy Log Format has: one crafted
// Log Line makes each match run into PCRE2's step limit.
LogFormatDefinition greedyFormat()
{
    LogFormatDefinition def;
    def.setName( "greedy_log" );
    QHash<QString, QString> regex;
    regex[ "basic" ]
        = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>.*) (?<proc>.*): (?<body>.*)$)";
    def.setRegexPatterns( regex );
    def.setTimestampField( "timestamp" );
    def.setBodyField( "body" );
    QHash<QString, LogFormatValueDef> values;
    values[ "host" ] = LogFormatValueDef{ "string", true, false };
    values[ "proc" ] = LogFormatValueDef{ "string", true, false };
    def.setValueDefinitions( values );
    return def;
}

} // namespace

SCENARIO( "The Table View shows a Row the regex engine gave up on as undecided",
          "[logformat][tablemodel][undecided]" )
{
    GIVEN( "a table model of a Log Format whose pattern backtracks catastrophically" )
    {
        const auto format = backtrackingFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );
        const auto backtracking = QString( 5000, QLatin1Char( 'a' ) ) + QLatin1Char( '!' );
        logData.setLines( { backtracking, "b", "aaaa" } );
        model.setLineCount( 3 );
        const auto lastColumn = model.columnCount() - 1;

        THEN( "the Row the engine gave up on is undecided: it shows its Log Line whole in the "
              "last field, and every cell says the engine gave up, as the Search does" )
        {
            REQUIRE( model.rowIsUndecided( 0 ) );
            REQUIRE_FALSE( model.rowMatchesFormat( 0 ) );
            REQUIRE( model.data( model.index( 0, lastColumn ) ).toString() == backtracking );
            for ( int column = 0; column < model.columnCount(); ++column ) {
                const auto note = model.data( model.index( 0, column ), Qt::ToolTipRole );
                REQUIRE( note.isValid() );
                REQUIRE( note.toString().contains( "regex engine gave up" ) );
            }
        }

        THEN( "a Row the Log Format does not match is decided: it says nothing" )
        {
            REQUIRE_FALSE( model.rowIsUndecided( 1 ) );
            REQUIRE_FALSE( model.rowMatchesFormat( 1 ) );
            REQUIRE_FALSE( model.data( model.index( 1, lastColumn ), Qt::ToolTipRole ).isValid() );
        }

        THEN( "a Row the Log Format matches is decided" )
        {
            REQUIRE_FALSE( model.rowIsUndecided( 2 ) );
            REQUIRE( model.rowMatchesFormat( 2 ) );
            REQUIRE( model.data( model.index( 2, lastColumn ) ).toString() == "aaaa" );
        }

        THEN( "no Row is undecided" )
        {
            REQUIRE_FALSE( model.rowIsUndecided( 3 ) );
            REQUIRE_FALSE( model.rowIsUndecided( -1 ) );
        }
    }
}

SCENARIO( "A Log File with a 50 MB Log Line opens in the Table View without a stall",
          "[logformat][tablemodel][linecap]" )
{
    GIVEN( "a table model of a Log Format with several greedy segments, and a 50 MB Log Line "
           "that every segment would backtrack through" )
    {
        const auto format = greedyFormat();
        FakeLogData logData;
        LogFormatTableModel model( format, &logData );
        const auto huge = "Jan  1 12:00:00 " + QString( 50'000'000, QLatin1Char( 'a' ) );
        logData.setLines(
            { "Jan  1 12:00:00 host1 proc: first", huge, "Jan  1 12:00:01 host2 proc: third" } );

        WHEN( "every cell of every Row is read, and the spans of its columns, fifty times "
              "over as a scroll back and forth reads them" )
        {
            QElapsedTimer clock;
            clock.start();
            model.setLineCount( 3 );
            for ( int pass = 0; pass < 50; ++pass ) {
                model.rereadRows();
                for ( int row = 0; row < 3; ++row ) {
                    for ( int column = 0; column < model.columnCount(); ++column ) {
                        model.data( model.index( row, column ) );
                        model.data( model.index( row, column ), Qt::ToolTipRole );
                    }
                    REQUIRE( model.columnSpans( row ).has_value() );
                }
            }
            const auto elapsed = clock.elapsed();

            THEN( "it took well under a second: the huge Log Line was not matched" )
            {
                // Generous: one match of it that gives up takes a dozen
                // milliseconds, and each pass would take three -- the Row's
                // cells, its spans, its Timestamp for the next Row's elapsed
                // time -- for seconds in all.
                // Well under a second here; the uncapped extraction took
                // seconds, and tens of them under the sanitizers (#826).
                REQUIRE( elapsed < 8000 );
            }

            THEN( "the huge Log Line shows whole in the last field, as one the Log Format does "
                  "not match, and is decided" )
            {
                REQUIRE_FALSE( model.rowMatchesFormat( 1 ) );
                REQUIRE_FALSE( model.rowIsUndecided( 1 ) );
                REQUIRE( model.data( model.index( 1, model.columnCount() - 1 ) ).toString()
                         == huge );
            }

            THEN( "the Rows around it are read as ever" )
            {
                REQUIRE( model.rowMatchesFormat( 0 ) );
                REQUIRE( model.rowMatchesFormat( 2 ) );
                REQUIRE( model.data( model.index( 2, model.columnCount() - 1 ) ).toString()
                         == "third" );
            }
        }
    }
}
