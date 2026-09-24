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

#include "logfieldextractor.h"
#include "logformatparser.h"

#include <QString>
#include <QStringList>

static const char* TestFormatJson = R"({
    "test_log": {
        "title": "Test Format",
        "regex": {
            "standard": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\s+(?<level>\\w+)\\s+\\[(?<thread>[^\\]]+)\\]\\s+(?<component>[^ ]+)\\s+-\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "thread": { "kind": "string", "identifier": true },
            "component": { "kind": "string", "identifier": true }
        },
        "sample": [
            { "line": "2024-01-15 12:30:45.123 INFO [main] com.example.App - Application started" }
        ]
    }
})";

static const char* MultiRegexFormatJson = R"({
    "multi_regex_log": {
        "title": "Multi Regex Format",
        "regex": {
            "with_pid": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\s+\\[(?<pid>\\d+)\\]\\s+(?<level>\\w+)\\s+(?<body>.*)$"
            },
            "without_pid": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\s+(?<level>\\w+)\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "pid": { "kind": "integer" }
        },
        "sample": [
            { "line": "2024-01-01 12:00:00 [1234] ERROR Something failed" },
            { "line": "2024-01-01 12:00:00 INFO Starting up" }
        ]
    }
})";

SCENARIO( "LogFieldExtractor extracts fields from a matching line", "[logformat][extractor]" )
{
    GIVEN( "An extractor configured with a test format" )
    {
        auto formats = LogFormatParser::parseJsonString( TestFormatJson );
        REQUIRE( formats.size() == 1 );

        LogFieldExtractor extractor( formats[ 0 ] );

        WHEN( "A matching line is extracted" )
        {
            auto fields = extractor.extractFields(
                "2024-01-15 12:30:45.123 INFO [main] com.example.App - Application started" );

            THEN( "All named fields are present" )
            {
                REQUIRE( fields.isValid() );
                REQUIRE( fields.value( "timestamp" ) == "2024-01-15 12:30:45.123" );
                REQUIRE( fields.value( "level" ) == "INFO" );
                REQUIRE( fields.value( "thread" ) == "main" );
                REQUIRE( fields.value( "component" ) == "com.example.App" );
                REQUIRE( fields.value( "body" ) == "Application started" );
            }
        }

        WHEN( "A non-matching line is extracted" )
        {
            auto fields = extractor.extractFields( "some random text without structure" );

            THEN( "The result is invalid" )
            {
                REQUIRE_FALSE( fields.isValid() );
            }
        }
    }
}

SCENARIO( "LogFieldExtractor tries multiple regex patterns", "[logformat][extractor]" )
{
    GIVEN( "An extractor with a format that has two regex patterns" )
    {
        auto formats = LogFormatParser::parseJsonString( MultiRegexFormatJson );
        REQUIRE( formats.size() == 1 );

        LogFieldExtractor extractor( formats[ 0 ] );

        WHEN( "A line matches the first pattern (with PID)" )
        {
            auto fields
                = extractor.extractFields( "2024-01-01 12:00:00 [1234] ERROR Something failed" );

            THEN( "Fields including PID are extracted" )
            {
                REQUIRE( fields.isValid() );
                REQUIRE( fields.value( "timestamp" ) == "2024-01-01 12:00:00" );
                REQUIRE( fields.value( "pid" ) == "1234" );
                REQUIRE( fields.value( "level" ) == "ERROR" );
                REQUIRE( fields.value( "body" ) == "Something failed" );
            }
        }

        WHEN( "A line matches the second pattern (without PID)" )
        {
            auto fields = extractor.extractFields( "2024-01-01 12:00:00 INFO Starting up" );

            THEN( "Fields without PID are extracted" )
            {
                REQUIRE( fields.isValid() );
                REQUIRE( fields.value( "timestamp" ) == "2024-01-01 12:00:00" );
                REQUIRE( fields.value( "level" ) == "INFO" );
                REQUIRE( fields.value( "body" ) == "Starting up" );
            }

            THEN( "PID field is empty for this pattern" )
            {
                REQUIRE( fields.value( "pid" ).isEmpty() );
            }
        }
    }
}

SCENARIO( "LogFieldExtractor column info reflects format definition", "[logformat][extractor]" )
{
    GIVEN( "An extractor configured with a test format" )
    {
        auto formats = LogFormatParser::parseJsonString( TestFormatJson );
        REQUIRE( formats.size() == 1 );

        LogFieldExtractor extractor( formats[ 0 ] );

        THEN( "Column names include special fields and value fields" )
        {
            auto columns = extractor.columnNames();
            // Should contain at minimum: timestamp, level, thread, component, body
            REQUIRE( columns.contains( "timestamp" ) );
            REQUIRE( columns.contains( "level" ) );
            REQUIRE( columns.contains( "thread" ) );
            REQUIRE( columns.contains( "component" ) );
            REQUIRE( columns.contains( "body" ) );
        }

        THEN( "Value definition columns follow regex capture group order" )
        {
            auto columns = extractor.columnNames();
            // Regex order: timestamp, level, thread, component, body
            REQUIRE( columns.first() == "timestamp" );
            REQUIRE( columns.last() == "body" );

            // "thread" appears before "component" in the regex capture groups
            const auto componentIdx = columns.indexOf( "component" );
            const auto threadIdx = columns.indexOf( "thread" );
            REQUIRE( threadIdx < componentIdx );
        }
    }
}

SCENARIO( "LogFieldExtractor hides fields marked as hidden", "[logformat][extractor]" )
{
    GIVEN( "A format with a hidden value definition" )
    {
        LogFormatDefinition def;
        def.setName( "hidden_test" );
        QHash<QString, QString> regex;
        regex[ "std" ] = R"(^(?<timestamp>\S+) (?<level>\w+) (?<secret>\S+) (?<body>.*)$)";
        def.setRegexPatterns( regex );
        def.setTimestampField( "timestamp" );
        def.setLevelField( "level" );
        def.setBodyField( "body" );

        QHash<QString, LogFormatValueDef> values;
        values[ "secret" ] = LogFormatValueDef{ "string", false, true }; // hidden = true
        def.setValueDefinitions( values );

        LogFieldExtractor extractor( def );

        THEN( "Hidden fields do not appear in columnNames" )
        {
            auto columns = extractor.columnNames();
            REQUIRE_FALSE( columns.contains( "secret" ) );
        }

        THEN( "Hidden fields are still extracted from lines" )
        {
            auto fields = extractor.extractFields( "2024-01-01 INFO s3cr3t Hello world" );
            REQUIRE( fields.isValid() );
            REQUIRE( fields.value( "secret" ) == "s3cr3t" );
        }
    }
}

SCENARIO( "LogFieldExtractor includes opid-field in columns", "[logformat][extractor]" )
{
    GIVEN( "A format with opid-field not in value definitions" )
    {
        LogFormatDefinition def;
        def.setName( "opid_test" );
        QHash<QString, QString> regex;
        regex[ "std" ] = R"(^(?<timestamp>\S+) (?<level>\w+) (?<request_id>\S+) (?<body>.*)$)";
        def.setRegexPatterns( regex );
        def.setTimestampField( "timestamp" );
        def.setLevelField( "level" );
        def.setBodyField( "body" );
        def.setOpidField( "request_id" );
        // NOTE: request_id is NOT in valueDefinitions

        LogFieldExtractor extractor( def );

        THEN( "opid-field appears in columnNames" )
        {
            auto columns = extractor.columnNames();
            REQUIRE( columns.contains( "request_id" ) );
        }
    }
}

SCENARIO( "LogFieldExtractor maps each named group of each pattern", "[logformat][extractor]" )
{
    GIVEN( "Two patterns with unnamed and optional groups between the named ones" )
    {
        LogFormatDefinition def;
        def.setName( "groups_test" );
        QHash<QString, QString> regex;
        regex[ "a_bracketed" ]
            = R"(^(\[)(?<timestamp>\S+)(\]) (?:(?<pid>\d+) )?(?<level>[A-Z]+) (?<body>.*)$)";
        regex[ "b_plain" ] = R"(^(?<when>\S+) (x)(?<body>.*)$)";
        def.setRegexPatterns( regex );
        def.setTimestampField( "timestamp" );
        def.setLevelField( "level" );
        def.setBodyField( "body" );

        LogFieldExtractor extractor( def );

        THEN( "Every line gets the values of its own named groups" )
        {
            for ( int round = 0; round < 3; ++round ) {
                auto withPid = extractor.extractFields( "[t1] 42 INFO first" );
                REQUIRE( withPid.isValid() );
                REQUIRE( withPid.value( "timestamp" ) == "t1" );
                REQUIRE( withPid.value( "pid" ) == "42" );
                REQUIRE( withPid.value( "level" ) == "INFO" );
                REQUIRE( withPid.value( "body" ) == "first" );

                auto withoutPid = extractor.extractFields( "[t2] WARN second" );
                REQUIRE( withoutPid.isValid() );
                REQUIRE( withoutPid.value( "timestamp" ) == "t2" );
                REQUIRE( withoutPid.value( "pid" ).isEmpty() );
                REQUIRE( withoutPid.value( "level" ) == "WARN" );
                REQUIRE( withoutPid.value( "body" ) == "second" );

                auto plain = extractor.extractFields( "t3 xthird" );
                REQUIRE( plain.isValid() );
                REQUIRE( plain.value( "when" ) == "t3" );
                REQUIRE( plain.value( "body" ) == "third" );
                REQUIRE_FALSE( plain.fieldNames().contains( "level" ) );
                REQUIRE_FALSE( plain.fieldNames().contains( "timestamp" ) );
            }
        }
    }
}

namespace {

const char* const SingleFieldJsonFormat = R"({
    "single_json": {
        "title": "Single JSON",
        "file-type": "json",
        "timestamp-field": "ts",
        "timestamp-divisor": 1000,
        "level-field": "level",
        "body-field": "msg",
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

const char* const SingleFieldLogfmtFormat = R"({
    "single_logfmt": {
        "title": "Single logfmt",
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

// Every field of every line, one at a time, must be what the extraction of all
// the fields gives, and a line that extraction calls invalid has no field.
void requireSingleFieldsEqualFullExtraction( const char* formatJson, const QStringList& lines,
                                             const QStringList& fieldNames )
{
    const auto formats = LogFormatParser::parseJsonString( formatJson );
    REQUIRE( formats.size() == 1 );
    const LogFieldExtractor extractor( formats[ 0 ] );

    for ( const auto& line : lines ) {
        const auto all = extractor.extractFields( line );
        for ( const auto& name : fieldNames ) {
            const auto single = extractor.extractField( line, name );
            INFO( line.toStdString() << " / " << name.toStdString() );
            if ( !all.isValid() ) {
                REQUIRE_FALSE( single.has_value() );
            }
            else {
                REQUIRE( single.has_value() );
                REQUIRE( *single == all.value( name ) );
            }
        }
    }
}

} // namespace

SCENARIO( "Extracting a single field gives what extracting every field gives",
          "[logformat][extractor]" )
{
    THEN( "it does for a Regex format" )
    {
        requireSingleFieldsEqualFullExtraction(
            TestFormatJson,
            { "2024-01-15 12:30:45.123 INFO [main] com.example.App - Application started",
              "2024-01-15 12:30:45.123 WARN [t] c - ",
              "some random text without structure", "" },
            { "timestamp", "level", "thread", "component", "body", "nosuchfield" } );
        requireSingleFieldsEqualFullExtraction(
            MultiRegexFormatJson,
            { "2024-01-01 12:00:00 [1234] ERROR Something failed",
              "2024-01-01 12:00:00 INFO Starting up", "garbage" },
            { "timestamp", "level", "pid", "body", "nosuchfield" } );
    }

    THEN( "it does for a JSON format" )
    {
        requireSingleFieldsEqualFullExtraction(
            SingleFieldJsonFormat,
            { R"({"ts":1700000000123,"level":"info","src":{"file":"a.cpp"},"pid":42,"ok":true,"msg":"hi"})",
              R"({"ts":"not a number","level":null,"pid":1.5,"extra":"ignored","msg":["a",1]})",
              R"({"src/file":"flat","msg":{"nested":1}})", R"({"a":1})", "not json", "", "[1,2]" },
            { "ts", "level", "src/file", "pid", "ok", "msg", "extra", "nosuchfield" } );
    }

    THEN( "it does for a logfmt format" )
    {
        requireSingleFieldsEqualFullExtraction(
            SingleFieldLogfmtFormat,
            { R"(time=2026-09-23T18:00:00Z level=info msg="server started" port=8080)",
              R"(port=9090 msg="said \"hi\" to \\ you" level=warn extra=ignored)",
              R"(time=2026-09-23T18:00:02Z level=error flag)", "=value", R"({"a":1})", "" },
            { "time", "level", "port", "msg", "extra", "flag", "nosuchfield" } );
    }
}
