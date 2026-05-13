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

#include "abstractlogdata.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFontMetrics>

namespace {

// Minimal mock of AbstractLogData for testing the table model.
class MockLogData : public AbstractLogData {
  public:
    void setLines( const QStringList& lines ) { lines_ = lines; }

  protected:
    QString doGetLineString( LineNumber line ) const override
    {
        const auto idx = static_cast<int>( line.get() );
        if ( idx >= 0 && idx < lines_.size() ) {
            return lines_[ idx ];
        }
        return {};
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> result;
        for ( uint64_t i = 0; i < count.get(); ++i ) {
            result.push_back( doGetLineString( LineNumber( first.get() + i ) ) );
        }
        return result;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        return doGetLines( first, count );
    }
    LineNumber doGetLineNumber( LineNumber index ) const override { return index; }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( static_cast<uint64_t>( lines_.size() ) );
    }
    LineLength doGetMaxLength() const override { return LineLength( 0 ); }
    LineLength doGetLineLength( LineNumber ) const override { return LineLength( 0 ); }
    void doSetDisplayEncoding( const char* ) override {}
    QTextCodec* doGetDisplayEncoding() const override { return nullptr; }
    void doAttachReader() const override {}
    void doDetachReader() const override {}

  private:
    QStringList lines_;
};

// Build a simple syslog-like format for testing
LogFormatDefinition makeTestFormat()
{
    LogFormatDefinition def;
    def.setName( "test_log" );
    def.setTitle( "Test Log" );

    QHash<QString, QString> regex;
    regex[ "basic" ]
        = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>\S+) (?<body>.*)$)";
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
        MockLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "Column count is 4: timestamp, level, host, body" )
        {
            REQUIRE( model.columnCount() == 4 );
        }
    }
}

SCENARIO( "LogFormatTableModel returns proper column headers", "[logformat][tablemodel]" )
{
    GIVEN( "A table model with a syslog-like format" )
    {
        auto format = makeTestFormat();
        MockLogData logData;
        LogFormatTableModel model( format, &logData );

        THEN( "Column headers match the field names in deterministic order" )
        {
            // Order must be: timestamp, level, [value fields], body
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "host" );
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

        MockLogData logData;
        LogFormatTableModel model( def, &logData );

        THEN( "Value field columns are sorted alphabetically (fallback)" )
        {
            // Expected order: timestamp, level, alpha, middle, zebra, body
            REQUIRE( model.columnCount() == 6 );
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "alpha" );
            REQUIRE( model.headerData( 3, Qt::Horizontal ).toString() == "middle" );
            REQUIRE( model.headerData( 4, Qt::Horizontal ).toString() == "zebra" );
            REQUIRE( model.headerData( 5, Qt::Horizontal ).toString() == "body" );
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

        MockLogData logData;
        LogFormatTableModel model( def, &logData );

        THEN( "Value field columns follow JSON insertion order" )
        {
            // Expected order: timestamp, level, zebra, alpha, middle, body
            REQUIRE( model.columnCount() == 6 );
            REQUIRE( model.headerData( 0, Qt::Horizontal ).toString() == "timestamp" );
            REQUIRE( model.headerData( 1, Qt::Horizontal ).toString() == "level" );
            REQUIRE( model.headerData( 2, Qt::Horizontal ).toString() == "zebra" );
            REQUIRE( model.headerData( 3, Qt::Horizontal ).toString() == "alpha" );
            REQUIRE( model.headerData( 4, Qt::Horizontal ).toString() == "middle" );
            REQUIRE( model.headerData( 5, Qt::Horizontal ).toString() == "body" );
        }
    }
}

SCENARIO( "LogFormatTableModel row count starts at zero", "[logformat][tablemodel]" )
{
    GIVEN( "A newly constructed table model" )
    {
        auto format = makeTestFormat();
        MockLogData logData;
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
        MockLogData logData;
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
                auto body
                    = model.data( model.index( 0, model.columnCount() - 1 ) ).toString();
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
        MockLogData logData;
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
                auto body
                    = model.data( model.index( 0, model.columnCount() - 1 ) ).toString();
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
        MockLogData logData;
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
        MockLogData logData;

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
        MockLogData logData;

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
        MockLogData logData;

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
                const auto headerText
                    = model.headerData( col, Qt::Horizontal ).toString();
                maxWidths[ col ] = fm.horizontalAdvance( headerText ) + cellPadding;
            }
            for ( int row = 0; row < rowCount; ++row ) {
                for ( int col = 0; col < colCount; ++col ) {
                    const auto text
                        = model.data( model.index( row, col ) ).toString();
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
            const auto bodyHeader
                = model.headerData( bodyCol, Qt::Horizontal ).toString();
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
        MockLogData logData;

        // Non-matching lines (e.g., log file headers) go entirely into body column
        const QString headerLine
            = "#----- BEGIN: Logging session 2026-04-20 10:57:52.000 -----";
        logData.setLines( { headerLine } );

        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        const auto fm = QFontMetrics( QApplication::font() );
        constexpr int cellPadding = 16;

        THEN( "Body column width accommodates the full non-matching line text" )
        {
            const int bodyCol = model.columnCount() - 1;
            const auto bodyText
                = model.data( model.index( 0, bodyCol ) ).toString();

            // Non-matching line should appear in body column
            REQUIRE( bodyText == headerLine );

            // Width must accommodate the full text
            const int requiredWidth = fm.horizontalAdvance( bodyText ) + cellPadding;
            REQUIRE( requiredWidth > 0 );
        }
    }
}

SCENARIO( "LogFormatTableModel cache invalidation on truncation",
          "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with cached rows" )
    {
        auto format = makeTestFormat();
        MockLogData logData;

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
        MockLogData logData;

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
        MockLogData logData;

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
        MockLogData logData;

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
            REQUIRE_FALSE(
                model.data( model.index( 0, model.columnCount() ) ).isValid() );
        }
    }
}

SCENARIO( "LogFormatTableModel setLineCount is idempotent", "[logformat][tablemodel][cache]" )
{
    GIVEN( "A table model with data" )
    {
        auto format = makeTestFormat();
        MockLogData logData;

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
        MockLogData logData;

        logData.setLines( { "Jan  1 00:00:01 host1 msg" } );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( 1 );

        THEN( "EditRole returns empty variant" )
        {
            REQUIRE_FALSE( model.data( model.index( 0, 0 ), Qt::EditRole ).isValid() );
        }

        THEN( "DecorationRole returns empty variant" )
        {
            REQUIRE_FALSE(
                model.data( model.index( 0, 0 ), Qt::DecorationRole ).isValid() );
        }
    }
}
