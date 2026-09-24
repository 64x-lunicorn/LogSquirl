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
#include "formatrecognition.h"
#include "logformatcatalog.h"
#include "logformatparser.h"
#include "logformattablemodel.h"
#include "timestampreader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

constexpr RecognitionPolicy Enabled{ .enabled = true };

// A JSON format as a user would bring it: no regex, the fields by path, epoch
// milliseconds in "ts". The members of "value" are deliberately not in
// alphabetical order.
const char* const AppJson = R"({
    "app_json": {
        "title": "App JSON",
        "file-type": "json",
        "timestamp-field": "ts",
        "timestamp-divisor": 1000,
        "level-field": "level",
        "body-field": "msg",
        "line-format": [ { "field": "ts" }, " ", { "field": "msg" } ],
        "value": {
            "ts": { "kind": "integer" },
            "level": { "kind": "string" },
            "src/file": { "kind": "string" },
            "pid": { "kind": "integer" },
            "ok": { "kind": "boolean" },
            "msg": { "kind": "string" }
        }
    }
})";

const char* const SyslogJson = R"({
    "syslog_log": {
        "title": "Syslog",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[A-Z][a-z]{2}\\s+\\d+\\s+\\d{2}:\\d{2}:\\d{2})\\s+(?<hostname>[^ ]+)\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "body-field": "body",
        "value": { "hostname": { "kind": "string" } }
    }
})";

// A regex format that matches every line, JSON or not
const char* const GenericJson = R"({
    "generic_log": {
        "title": "Generic",
        "regex": { "basic": { "pattern": "^(?<body>.+)$" } },
        "body-field": "body"
    }
})";

const QStringList JsonLines = {
    R"({"ts":1700000000000,"level":"info","src":{"file":"a.cpp","line":3},"pid":42,"ok":true,"msg":"started"})",
    R"({"ts":1700000001500,"level":"error","src":{"file":"b.cpp"},"pid":43,"msg":"failed"})",
    R"({"ts":1700000002000,"level":"warn","msg":"no source"})",
};

LogFormatDefinition loadAppFormat()
{
    auto formats = LogFormatParser::parseJsonString( AppJson );
    REQUIRE( formats.size() == 1 );
    return formats.first();
}

void addTo( LogFormatCatalog& catalog, const char* json )
{
    for ( auto& format : LogFormatParser::parseJsonString( json ) ) {
        catalog.addFormat( std::move( format ) );
    }
}

} // namespace

SCENARIO( "A JSON Log Format loads without a regex", "[logformat][json]" )
{
    GIVEN( "a format with file-type json, a nested field path and a timestamp divisor" )
    {
        const auto format = loadAppFormat();

        THEN( "it is a JSON format without regex patterns" )
        {
            REQUIRE( format.kind() == LogFormatKind::Json );
            REQUIRE( format.regexPatterns().isEmpty() );
            REQUIRE( format.timestampField() == "ts" );
            REQUIRE( format.timestampDivisor() == 1000.0 );
        }

        THEN( "the columns are the value fields in the order of the file" )
        {
            const QStringList expected = { "ts", "level", "src/file", "pid", "ok", "msg" };
            REQUIRE( format.valueFieldOrder() == expected );

            FakeLogData logData;
            LogFormatTableModel model( format, &logData );
            REQUIRE( model.columnCount() == expected.size() );
            for ( int i = 0; i < expected.size(); ++i ) {
                REQUIRE( model.headerData( i, Qt::Horizontal ).toString() == expected[ i ] );
            }
        }
    }

    GIVEN( "a regex format" )
    {
        auto formats = LogFormatParser::parseJsonString( SyslogJson );
        THEN( "it stays a regex format" )
        {
            REQUIRE( formats.size() == 1 );
            REQUIRE( formats.first().kind() == LogFormatKind::Regex );
        }
    }

    GIVEN( "a file-type json format read from a file" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( "app.json" );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( AppJson );
        file.close();

        THEN( "it loads into a Catalog" )
        {
            LogFormatCatalog catalog;
            for ( auto& format : LogFormatParser::parseFile( path ) ) {
                catalog.addFormat( std::move( format ) );
            }
            REQUIRE( catalog.allFormats().contains( "app_json" ) );
        }
    }
}

SCENARIO( "Format Recognition recognizes JSON Log Files", "[logformat][json][recognition]" )
{
    GIVEN( "a Catalog with a JSON format and a regex format" )
    {
        LogFormatCatalog catalog;
        addTo( catalog, AppJson );
        addTo( catalog, SyslogJson );

        WHEN( "the sample lines are JSON objects" )
        {
            const auto result = FormatRecognition::recognize( JsonLines, Enabled, catalog );
            THEN( "the JSON format is recognized" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "app_json" );
            }
        }

        WHEN( "the sample lines are syslog lines" )
        {
            const QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[1]: Accepted publickey",
                "Jun 15 10:21:05 myhost sshd[1]: session opened",
            };
            const auto result = FormatRecognition::recognize( lines, Enabled, catalog );
            THEN( "the regex format is recognized" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }

        WHEN( "the sample lines are plain text" )
        {
            const QStringList lines = { "just some text", "and more text", "{not json" };
            THEN( "no JSON format is recognized" )
            {
                const auto result = FormatRecognition::recognize( lines, Enabled, catalog );
                REQUIRE( result == nullptr );
            }
        }

        WHEN( "the sample lines are JSON objects without the timestamp field" )
        {
            const QStringList lines = { R"({"a":1})", R"({"b":2})" };
            THEN( "no format is recognized" )
            {
                REQUIRE( FormatRecognition::recognize( lines, Enabled, catalog ) == nullptr );
            }
        }

        WHEN( "one line of the JSON file is truncated" )
        {
            auto lines = JsonLines;
            lines << R"({"ts":1700000003000,"level":"info","ms)";
            THEN( "the JSON format is still recognized" )
            {
                const auto result = FormatRecognition::recognize( lines, Enabled, catalog );
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "app_json" );
            }
        }
    }

    GIVEN( "a Catalog with a JSON format and a regex format that matches every line" )
    {
        LogFormatCatalog catalog;
        addTo( catalog, AppJson );
        addTo( catalog, GenericJson );

        THEN( "the regex format does not steal the JSON Log File" )
        {
            const auto result = FormatRecognition::recognize( JsonLines, Enabled, catalog );
            REQUIRE( result != nullptr );
            REQUIRE( result->name() == "app_json" );
        }

        THEN( "the JSON format does not steal the plain Log File" )
        {
            const QStringList lines = { "one", "two", "three" };
            const auto result = FormatRecognition::recognize( lines, Enabled, catalog );
            REQUIRE( result != nullptr );
            REQUIRE( result->name() == "generic_log" );
        }
    }
}

SCENARIO( "The Table View shows the fields of JSON Log Lines", "[logformat][json][tablemodel]" )
{
    GIVEN( "a table model over JSON Log Lines and a line that is not JSON" )
    {
        const auto format = loadAppFormat();
        FakeLogData logData;
        auto lines = JsonLines;
        lines << "a stray plain text line" << R"({"ts":1700000003000,"msg":"cut)";
        logData.setLines( lines );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( static_cast<int>( lines.size() ) );

        const auto cell = [ & ]( int row, int column ) {
            return model.data( model.index( row, column ) ).toString();
        };

        THEN( "each value field is a column, the nested one and the converted timestamp included" )
        {
            REQUIRE( cell( 0, 0 ) == "2023-11-14 22:13:20.000" );
            REQUIRE( cell( 0, 1 ) == "info" );
            REQUIRE( cell( 0, 2 ) == "a.cpp" );
            REQUIRE( cell( 0, 3 ) == "42" );
            REQUIRE( cell( 0, 4 ) == "true" );
            REQUIRE( cell( 0, 5 ) == "started" );
            REQUIRE( cell( 1, 0 ) == "2023-11-14 22:13:21.500" );
        }

        THEN( "a missing field is an empty cell" )
        {
            REQUIRE( cell( 1, 4 ).isEmpty() );
            REQUIRE( cell( 2, 2 ).isEmpty() );
            REQUIRE( cell( 2, 3 ).isEmpty() );
        }

        THEN( "a line that is not a JSON object is a Row with empty fields" )
        {
            REQUIRE( model.rowCount() == 5 );
            for ( int row = 3; row < 5; ++row ) {
                for ( int column = 0; column < model.columnCount(); ++column ) {
                    REQUIRE( cell( row, column ).isEmpty() );
                }
            }
        }
    }
}

SCENARIO( "A JSON Log Format has Timestamps", "[logformat][json][timestamp]" )
{
    GIVEN( "a timestamp reader for the format" )
    {
        const auto format = loadAppFormat();
        REQUIRE( TimestampReader::isAvailableFor( format ) );
        const TimestampReader reader( format );

        THEN( "an epoch value is divided by the timestamp divisor" )
        {
            const auto timestamp = reader.timestampOf( JsonLines[ 1 ] );
            REQUIRE( timestamp.has_value() );
            REQUIRE( timestamp->toMSecsSinceEpoch() == 1700000001500 );
        }

        THEN( "a line that is not a JSON object has none" )
        {
            REQUIRE_FALSE( reader.timestampOf( "plain text" ).has_value() );
            REQUIRE_FALSE( reader.timestampOf( R"({"ts":1700)" ).has_value() );
        }
    }
}
