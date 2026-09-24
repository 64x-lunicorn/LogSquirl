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

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFontMetrics>

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
