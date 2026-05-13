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

#include "logformatparser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Minimal lnav-style format JSON for testing
static const char* MinimalFormatJson = R"({
    "example_log": {
        "title": "Example Log Format",
        "description": "A test format",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z)>>(?<level>\\w+)>>(?<component>\\w+)>>(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "level": {
            "error": "ERROR",
            "warning": "WARNING"
        },
        "value": {
            "component": {
                "kind": "string",
                "identifier": true
            }
        },
        "sample": [
            {
                "line": "2011-04-01T15:14:34.203Z>>ERROR>>core>>Something went wrong"
            }
        ]
    }
})";

// Format with multiple regex patterns
static const char* MultiRegexFormatJson = R"({
    "multi_log": {
        "title": "Multi-Regex Format",
        "description": "Format with multiple regex patterns",
        "regex": {
            "standard": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\s+(?<level>\\w+)\\s+(?<body>.*)$"
            },
            "with_pid": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\s+\\[(?<pid>\\d+)\\]\\s+(?<level>\\w+)\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "pid": {
                "kind": "integer",
                "identifier": true
            }
        },
        "sample": [
            {
                "line": "2024-01-01 12:00:00 INFO Starting up"
            },
            {
                "line": "2024-01-01 12:00:00 [1234] ERROR Something failed"
            }
        ]
    }
})";

// Format with hidden fields
static const char* HiddenFieldFormatJson = R"({
    "hidden_field_log": {
        "title": "Hidden Field Format",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[^ ]+) (?<level>\\w+) (?<internal_id>\\d+) (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "internal_id": {
                "kind": "integer",
                "hidden": true
            }
        },
        "sample": [
            {
                "line": "2024-01-01 INFO 42 Hello world"
            }
        ]
    }
})";

// Multiple formats in one file (like lnav does)
static const char* MultiFormatFileJson = R"({
    "format_a": {
        "title": "Format A",
        "regex": {
            "basic": {
                "pattern": "^A:(?<timestamp>[^ ]+) (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "body-field": "body",
        "sample": [
            { "line": "A:2024-01-01 Hello" }
        ]
    },
    "format_b": {
        "title": "Format B",
        "regex": {
            "basic": {
                "pattern": "^B:(?<timestamp>[^ ]+) (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "body-field": "body",
        "sample": [
            { "line": "B:2024-01-01 World" }
        ]
    }
})";

SCENARIO( "LogFormatParser parses minimal lnav format JSON", "[logformat][parser]" )
{
    GIVEN( "A valid minimal lnav format JSON string" )
    {
        auto formats = LogFormatParser::parseJsonString( MinimalFormatJson );

        THEN( "Exactly one format is parsed" )
        {
            REQUIRE( formats.size() == 1 );
        }

        THEN( "The format has the correct name" )
        {
            REQUIRE( formats[ 0 ].name() == "example_log" );
        }

        THEN( "The format has the correct title" )
        {
            REQUIRE( formats[ 0 ].title() == "Example Log Format" );
        }

        THEN( "The format has one regex pattern" )
        {
            REQUIRE( formats[ 0 ].regexPatterns().size() == 1 );
            REQUIRE( formats[ 0 ].regexPatterns().contains( "basic" ) );
        }

        THEN( "The special fields are set correctly" )
        {
            REQUIRE( formats[ 0 ].timestampField() == "timestamp" );
            REQUIRE( formats[ 0 ].levelField() == "level" );
            REQUIRE( formats[ 0 ].bodyField() == "body" );
        }

        THEN( "Level mappings are parsed" )
        {
            const auto& levels = formats[ 0 ].levelMappings();
            REQUIRE( levels.size() == 2 );
            REQUIRE( levels.contains( "error" ) );
            REQUIRE( levels.value( "error" ) == "ERROR" );
            REQUIRE( levels.contains( "warning" ) );
            REQUIRE( levels.value( "warning" ) == "WARNING" );
        }

        THEN( "Value definitions are parsed" )
        {
            const auto& values = formats[ 0 ].valueDefinitions();
            REQUIRE( values.size() == 1 );
            REQUIRE( values.contains( "component" ) );
            REQUIRE( values.value( "component" ).kind == "string" );
            REQUIRE( values.value( "component" ).identifier == true );
            REQUIRE( values.value( "component" ).hidden == false );
        }

        THEN( "Sample lines are parsed" )
        {
            REQUIRE( formats[ 0 ].sampleLines().size() == 1 );
            REQUIRE( formats[ 0 ].sampleLines()[ 0 ].line
                     == "2011-04-01T15:14:34.203Z>>ERROR>>core>>Something went wrong" );
        }
    }
}

SCENARIO( "LogFormatParser handles multiple regex patterns", "[logformat][parser]" )
{
    GIVEN( "A format with two regex patterns" )
    {
        auto formats = LogFormatParser::parseJsonString( MultiRegexFormatJson );

        THEN( "The format has two regex patterns" )
        {
            REQUIRE( formats.size() == 1 );
            REQUIRE( formats[ 0 ].regexPatterns().size() == 2 );
            REQUIRE( formats[ 0 ].regexPatterns().contains( "standard" ) );
            REQUIRE( formats[ 0 ].regexPatterns().contains( "with_pid" ) );
        }

        THEN( "Value definitions include pid" )
        {
            REQUIRE( formats[ 0 ].valueDefinitions().contains( "pid" ) );
            REQUIRE( formats[ 0 ].valueDefinitions().value( "pid" ).kind == "integer" );
        }

        THEN( "Two sample lines are parsed" )
        {
            REQUIRE( formats[ 0 ].sampleLines().size() == 2 );
        }
    }
}

SCENARIO( "LogFormatParser handles hidden fields", "[logformat][parser]" )
{
    GIVEN( "A format with a hidden field" )
    {
        auto formats = LogFormatParser::parseJsonString( HiddenFieldFormatJson );

        THEN( "The hidden flag is set on the value" )
        {
            REQUIRE( formats.size() == 1 );
            const auto& values = formats[ 0 ].valueDefinitions();
            REQUIRE( values.contains( "internal_id" ) );
            REQUIRE( values.value( "internal_id" ).hidden == true );
        }
    }
}

SCENARIO( "LogFormatParser handles multiple formats per file", "[logformat][parser]" )
{
    GIVEN( "A JSON file with two format definitions" )
    {
        auto formats = LogFormatParser::parseJsonString( MultiFormatFileJson );

        THEN( "Both formats are parsed" )
        {
            REQUIRE( formats.size() == 2 );

            bool hasA = false;
            bool hasB = false;
            for ( const auto& f : formats ) {
                if ( f.name() == "format_a" )
                    hasA = true;
                if ( f.name() == "format_b" )
                    hasB = true;
            }
            REQUIRE( hasA );
            REQUIRE( hasB );
        }
    }
}

SCENARIO( "LogFormatParser handles invalid input", "[logformat][parser]" )
{
    GIVEN( "An empty JSON string" )
    {
        auto formats = LogFormatParser::parseJsonString( "" );

        THEN( "No formats are returned" )
        {
            REQUIRE( formats.empty() );
        }
    }

    GIVEN( "Invalid JSON" )
    {
        auto formats = LogFormatParser::parseJsonString( "not json at all" );

        THEN( "No formats are returned" )
        {
            REQUIRE( formats.empty() );
        }
    }

    GIVEN( "Valid JSON but no regex field" )
    {
        auto formats = LogFormatParser::parseJsonString( R"({
            "bad_format": {
                "title": "No regex"
            }
        })" );

        THEN( "No formats are returned (regex is required)" )
        {
            REQUIRE( formats.empty() );
        }
    }

    GIVEN( "JSON with $schema key (should be skipped)" )
    {
        auto formats = LogFormatParser::parseJsonString( R"({
            "$schema": "https://lnav.org/schemas/format-v1.schema.json",
            "example_log": {
                "title": "With Schema",
                "regex": {
                    "basic": {
                        "pattern": "^(?<body>.*)$"
                    }
                },
                "body-field": "body",
                "sample": [
                    { "line": "hello" }
                ]
            }
        })" );

        THEN( "Only the format is parsed, $schema is ignored" )
        {
            REQUIRE( formats.size() == 1 );
            REQUIRE( formats[ 0 ].name() == "example_log" );
        }
    }
}

SCENARIO( "LogFormatParser default field values", "[logformat][parser]" )
{
    GIVEN( "A format without explicit timestamp/level/body fields" )
    {
        auto formats = LogFormatParser::parseJsonString( R"({
            "default_fields_log": {
                "title": "Defaults",
                "regex": {
                    "basic": {
                        "pattern": "^(?<timestamp>[^ ]+) (?<level>\\w+) (?<body>.*)$"
                    }
                },
                "sample": [
                    { "line": "2024-01-01 INFO hello" }
                ]
            }
        })" );

        THEN( "Default field names are used (timestamp, level, body)" )
        {
            REQUIRE( formats.size() == 1 );
            REQUIRE( formats[ 0 ].timestampField() == "timestamp" );
            REQUIRE( formats[ 0 ].levelField() == "level" );
            REQUIRE( formats[ 0 ].bodyField() == "body" );
        }
    }
}
