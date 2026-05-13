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
        regex[ "std" ]
            = R"(^(?<timestamp>\S+) (?<level>\w+) (?<request_id>\S+) (?<body>.*)$)";
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

SCENARIO( "LogFieldExtractor LRU cache works correctly", "[logformat][extractor]" )
{
    GIVEN( "An extractor with LRU caching" )
    {
        auto formats = LogFormatParser::parseJsonString( TestFormatJson );
        REQUIRE( formats.size() == 1 );

        LogFieldExtractor extractor( formats[ 0 ], 3 ); // Small cache for testing

        const QString line1
            = "2024-01-15 12:30:45.123 INFO [main] com.example.App - Message 1";
        const QString line2
            = "2024-01-15 12:30:46.123 DEBUG [worker] com.example.Worker - Message 2";
        const QString line3
            = "2024-01-15 12:30:47.123 ERROR [io] com.example.IO - Message 3";
        const QString line4
            = "2024-01-15 12:30:48.123 WARN [net] com.example.Net - Message 4";

        WHEN( "Lines are extracted and then re-extracted" )
        {
            // First extraction
            auto f1 = extractor.extractFields( line1, 0 );
            auto f2 = extractor.extractFields( line2, 1 );
            auto f3 = extractor.extractFields( line3, 2 );

            REQUIRE( f1.isValid() );
            REQUIRE( f2.isValid() );
            REQUIRE( f3.isValid() );

            // Re-extract from cache (should return same results)
            auto f1_cached = extractor.extractFields( line1, 0 );
            REQUIRE( f1_cached.isValid() );
            REQUIRE( f1_cached.value( "body" ) == "Message 1" );

            THEN( "Cache returns consistent results" )
            {
                REQUIRE( f1_cached.value( "timestamp" ) == f1.value( "timestamp" ) );
                REQUIRE( f1_cached.value( "level" ) == f1.value( "level" ) );
            }
        }

        WHEN( "Cache overflows" )
        {
            // Fill cache
            extractor.extractFields( line1, 0 );
            extractor.extractFields( line2, 1 );
            extractor.extractFields( line3, 2 );

            // Overflow: this should evict the oldest (line1)
            auto f4 = extractor.extractFields( line4, 3 );
            REQUIRE( f4.isValid() );

            // line1 was evicted; re-extracting still works (just re-parses)
            auto f1_reparsed = extractor.extractFields( line1, 0 );

            THEN( "Evicted entries are re-parsed correctly" )
            {
                REQUIRE( f1_reparsed.isValid() );
                REQUIRE( f1_reparsed.value( "body" ) == "Message 1" );
            }
        }
    }
}

SCENARIO( "LogFieldExtractor cache can be invalidated", "[logformat][extractor]" )
{
    GIVEN( "An extractor with cached data" )
    {
        auto formats = LogFormatParser::parseJsonString( TestFormatJson );
        REQUIRE( formats.size() == 1 );

        LogFieldExtractor extractor( formats[ 0 ], 100 );

        extractor.extractFields(
            "2024-01-15 12:30:45.123 INFO [main] com.example.App - Cached", 0 );

        WHEN( "Cache is invalidated" )
        {
            extractor.invalidateCache();

            THEN( "Re-extraction still works (just re-parses)" )
            {
                auto fields = extractor.extractFields(
                    "2024-01-15 12:30:45.123 INFO [main] com.example.App - Cached", 0 );
                REQUIRE( fields.isValid() );
                REQUIRE( fields.value( "body" ) == "Cached" );
            }
        }
    }
}
