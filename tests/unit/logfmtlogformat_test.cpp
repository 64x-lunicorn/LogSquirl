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
#include "logfmtlogline.h"
#include "logformatcatalog.h"
#include "logformatparser.h"
#include "logformattablemodel.h"
#include "timestampreader.h"

namespace {

constexpr RecognitionPolicy Enabled{ .enabled = true };

// A logfmt format as a user would bring it: no regex, the keys in the order the
// columns should have, not alphabetical.
const char* const AppLogfmt = R"({
    "app_logfmt": {
        "title": "App logfmt",
        "file-type": "logfmt",
        "timestamp-field": "time",
        "level-field": "level",
        "body-field": "msg",
        "value": {
            "time": { "kind": "string" },
            "level": { "kind": "string" },
            "port": { "kind": "integer" },
            "msg": { "kind": "string" }
        }
    }
})";

const char* const SyslogLogfmt = R"({
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

// A regex format that matches every line, logfmt or not
const char* const GenericLogfmt = R"({
    "generic_log": {
        "title": "Generic",
        "regex": { "basic": { "pattern": "^(?<body>.+)$" } },
        "body-field": "body"
    }
})";

const QStringList LogfmtLines = {
    R"(time=2026-09-23T18:00:00Z level=info msg="server started" port=8080)",
    R"(port=9090 msg="said \"hi\" to \\ you" level=warn time=2026-09-23T18:00:01Z extra=ignored)",
    R"(time=2026-09-23T18:00:02Z level=error flag)",
};

LogFormatDefinition loadAppFormat()
{
    auto formats = LogFormatParser::parseJsonString( AppLogfmt );
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

SCENARIO( "logfmt Log Lines are read as key/value pairs", "[logformat][logfmt]" )
{
    THEN( "bare and quoted values, escapes and bare keys are read" )
    {
        const auto pairs = LogfmtLogLine::parse( LogfmtLines[ 1 ] );
        REQUIRE( pairs.has_value() );
        REQUIRE( pairs->value( "msg" ) == R"(said "hi" to \ you)" );
        REQUIRE( pairs->value( "port" ) == "9090" );
        const auto bare = LogfmtLogLine::parse( LogfmtLines[ 2 ] );
        REQUIRE( bare.has_value() );
        REQUIRE( bare->contains( "flag" ) );
        REQUIRE( bare->value( "flag" ).isEmpty() );
    }

    THEN( "anything else is not logfmt" )
    {
        REQUIRE_FALSE( LogfmtLogLine::parse( "" ).has_value() );
        REQUIRE_FALSE( LogfmtLogLine::parse( R"({"a":1})" ).has_value() );
        REQUIRE_FALSE( LogfmtLogLine::parse( R"(msg="unterminated)" ).has_value() );
        REQUIRE_FALSE( LogfmtLogLine::parse( "=value" ).has_value() );
        REQUIRE_FALSE( LogfmtLogLine::parse( R"(a="b"c)" ).has_value() );
    }
}

SCENARIO( "A logfmt Log Format loads without a regex", "[logformat][logfmt]" )
{
    const auto format = loadAppFormat();

    THEN( "it is a logfmt format whose columns are the value keys in file order" )
    {
        REQUIRE( format.kind() == LogFormatKind::Logfmt );
        REQUIRE( format.regexPatterns().isEmpty() );
        const QStringList expected = { "time", "level", "port", "msg" };
        REQUIRE( format.valueFieldOrder() == expected );

        LogFormatCatalog catalog;
        addTo( catalog, AppLogfmt );
        REQUIRE( catalog.allFormats().contains( "app_logfmt" ) );
    }
}

SCENARIO( "Format Recognition recognizes logfmt Log Files", "[logformat][logfmt][recognition]" )
{
    GIVEN( "a Catalog with a logfmt format and a regex format" )
    {
        LogFormatCatalog catalog;
        addTo( catalog, AppLogfmt );
        addTo( catalog, SyslogLogfmt );

        THEN( "logfmt lines are recognized with the logfmt format" )
        {
            const auto result = FormatRecognition::recognize( LogfmtLines, Enabled, catalog );
            REQUIRE( result != nullptr );
            REQUIRE( result->name() == "app_logfmt" );
        }

        THEN( "syslog lines keep the regex format" )
        {
            const QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[1]: Accepted publickey",
                "Jun 15 10:21:05 myhost sshd[1]: session opened",
            };
            const auto result = FormatRecognition::recognize( lines, Enabled, catalog );
            REQUIRE( result != nullptr );
            REQUIRE( result->name() == "syslog_log" );
        }

        THEN( "lines without the timestamp key, and JSON objects, are not recognized" )
        {
            const QStringList lines = { "a=1 b=2", "c=3" };
            REQUIRE( FormatRecognition::recognize( lines, Enabled, catalog ) == nullptr );
            const QStringList json = { R"({"time":"x"})", R"({"time":"y"})" };
            REQUIRE( FormatRecognition::recognize( json, Enabled, catalog ) == nullptr );
        }
    }

    GIVEN( "a Catalog with a logfmt format and a regex format that matches every line" )
    {
        LogFormatCatalog catalog;
        addTo( catalog, AppLogfmt );
        addTo( catalog, GenericLogfmt );

        THEN( "the regex format keeps precedence" )
        {
            const auto result = FormatRecognition::recognize( LogfmtLines, Enabled, catalog );
            REQUIRE( result != nullptr );
            REQUIRE( result->name() == "generic_log" );
        }
    }
}

SCENARIO( "The Table View shows the fields of logfmt Log Lines", "[logformat][logfmt][tablemodel]" )
{
    GIVEN( "a table model over logfmt Log Lines and lines that are not logfmt" )
    {
        const auto format = loadAppFormat();
        FakeLogData logData;
        auto lines = LogfmtLines;
        lines << R"(msg="cut)" << R"({"time":"x"})";
        logData.setLines( lines );
        LogFormatTableModel model( format, &logData );
        model.setLineCount( static_cast<int>( lines.size() ) );

        const auto cell = [ & ]( int row, int column ) {
            return model.data( model.index( row, column ) ).toString();
        };

        THEN( "each declared key is a column, whatever the order of keys in the line" )
        {
            REQUIRE( cell( 0, 1 ) == "info" );
            REQUIRE( cell( 0, 2 ) == "8080" );
            REQUIRE( cell( 0, 3 ) == "server started" );
            REQUIRE( cell( 1, 1 ) == "warn" );
            REQUIRE( cell( 1, 2 ) == "9090" );
            REQUIRE( cell( 1, 3 ) == R"(said "hi" to \ you)" );
        }

        THEN( "a missing key is an empty cell" )
        {
            REQUIRE( cell( 2, 2 ).isEmpty() );
            REQUIRE( cell( 2, 3 ).isEmpty() );
        }

        THEN( "a line that is not logfmt is a Row with empty fields" )
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

SCENARIO( "A logfmt Log Format has Timestamps", "[logformat][logfmt][timestamp]" )
{
    const auto format = loadAppFormat();
    REQUIRE( TimestampReader::isAvailableFor( format ) );
    const TimestampReader reader( format );

    THEN( "the timestamp key is read whatever its position" )
    {
        const auto timestamp = reader.timestampOf( LogfmtLines[ 1 ] );
        REQUIRE( timestamp.has_value() );
        REQUIRE( timestamp->toMSecsSinceEpoch() == 1790186401000 );
    }

    THEN( "a line that is not logfmt has none" )
    {
        REQUIRE_FALSE( reader.timestampOf( R"({"time":"2026-09-23T18:00:00Z"})" ).has_value() );
    }
}
