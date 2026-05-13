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

#include "logformatmatcher.h"
#include "logformatparser.h"
#include "logformatregistry.h"

#include <QDir>
#include <QTemporaryDir>

// Syslog format
static const char* SyslogJson = R"({
    "syslog_log": {
        "title": "Syslog",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[A-Z][a-z]{2}\\s+\\d+\\s+\\d{2}:\\d{2}:\\d{2})\\s+(?<hostname>[^ ]+)\\s+(?<service>[^\\[]+)\\[(?<pid>\\d+)\\]:\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "body-field": "body",
        "value": {
            "hostname": { "kind": "string" },
            "service": { "kind": "string" },
            "pid": { "kind": "integer" }
        },
        "sample": [
            { "line": "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user" }
        ]
    }
})";

// Java log format
static const char* JavaLogJson = R"({
    "java_log": {
        "title": "Java Log",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\s+(?<level>\\w+)\\s+\\[(?<thread>[^\\]]+)\\]\\s+(?<class>[^ ]+)\\s+-\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "thread": { "kind": "string", "identifier": true },
            "class": { "kind": "string", "identifier": true }
        },
        "sample": [
            { "line": "2024-01-15 12:30:45.123 INFO [main] com.example.App - Application started" }
        ]
    }
})";

// Generic format that matches almost anything (low specificity)
static const char* GenericJson = R"({
    "generic_log": {
        "title": "Generic",
        "regex": {
            "basic": {
                "pattern": "^(?<body>.+)$"
            }
        },
        "body-field": "body",
        "sample": [
            { "line": "anything goes here" }
        ]
    }
})";

SCENARIO( "LogFormatMatcher detects syslog format", "[logformat][matcher]" )
{
    GIVEN( "A registry with syslog and java log formats" )
    {
        LogFormatRegistry registry;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            registry.addFormat( std::move( f ) );
        }

        auto javaFormats = LogFormatParser::parseJsonString( JavaLogJson );
        for ( auto& f : javaFormats ) {
            registry.addFormat( std::move( f ) );
        }

        LogFormatMatcher matcher( registry );

        WHEN( "Given syslog lines" )
        {
            QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user",
                "Jun 15 10:21:05 myhost sshd[12345]: pam_unix(sshd:session): session opened",
                "Jun 15 10:21:06 myhost cron[99]: (root) CMD (/usr/bin/some_job)",
            };

            auto result = matcher.detectFormat( lines );

            THEN( "Syslog format is detected" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }

        WHEN( "Given Java log lines" )
        {
            QStringList lines = {
                "2024-01-15 12:30:45.123 INFO [main] com.example.App - Application started",
                "2024-01-15 12:30:45.456 DEBUG [main] com.example.Config - Loading config",
                "2024-01-15 12:30:45.789 INFO [worker-1] com.example.Worker - Processing task",
            };

            auto result = matcher.detectFormat( lines );

            THEN( "Java log format is detected" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "java_log" );
            }
        }

        WHEN( "Given lines that match no format" )
        {
            QStringList lines = {
                "just some random text",
                "without any recognizable structure",
                "1234567890",
            };

            auto result = matcher.detectFormat( lines );

            THEN( "No format is detected" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}

SCENARIO( "LogFormatMatcher prefers more specific format", "[logformat][matcher]" )
{
    GIVEN( "A registry with a specific and a generic format" )
    {
        LogFormatRegistry registry;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            registry.addFormat( std::move( f ) );
        }

        auto genericFormats = LogFormatParser::parseJsonString( GenericJson );
        for ( auto& f : genericFormats ) {
            registry.addFormat( std::move( f ) );
        }

        LogFormatMatcher matcher( registry );

        WHEN( "Given syslog lines (match both formats)" )
        {
            QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user",
                "Jun 15 10:21:05 myhost sshd[12345]: pam_unix(sshd:session): session opened",
            };

            auto result = matcher.detectFormat( lines );

            THEN( "The more specific format (syslog) wins over generic" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }
    }
}

SCENARIO( "LogFormatMatcher requires minimum match threshold", "[logformat][matcher]" )
{
    GIVEN( "A registry with a format" )
    {
        LogFormatRegistry registry;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            registry.addFormat( std::move( f ) );
        }

        LogFormatMatcher matcher( registry );

        WHEN( "Only 1 out of 10 lines match" )
        {
            QStringList lines;
            lines << "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user";
            for ( int i = 0; i < 9; ++i ) {
                lines << "random garbage line";
            }

            auto result = matcher.detectFormat( lines );

            THEN( "No format is detected (below threshold)" )
            {
                REQUIRE( result == nullptr );
            }
        }

        WHEN( "Most lines match the format" )
        {
            QStringList lines;
            for ( int i = 0; i < 8; ++i ) {
                lines << QString( "Jun 15 10:21:%1 myhost sshd[12345]: Line %2" )
                              .arg( i, 2, 10, QChar( '0' ) )
                              .arg( i );
            }
            lines << "some non-matching continuation line";
            lines << "another continuation";

            auto result = matcher.detectFormat( lines );

            THEN( "Format is detected (above threshold)" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }
    }
}

SCENARIO( "LogFormatMatcher handles empty input", "[logformat][matcher]" )
{
    GIVEN( "A registry with formats" )
    {
        LogFormatRegistry registry;
        auto formats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : formats ) {
            registry.addFormat( std::move( f ) );
        }

        LogFormatMatcher matcher( registry );

        WHEN( "Given an empty line list" )
        {
            auto result = matcher.detectFormat( QStringList{} );

            THEN( "No format is detected" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}

SCENARIO( "LogFormatMatcher handles empty registry", "[logformat][matcher]" )
{
    GIVEN( "An empty registry" )
    {
        LogFormatRegistry registry;
        LogFormatMatcher matcher( registry );

        WHEN( "Given lines" )
        {
            QStringList lines = { "Jun 15 10:21:04 myhost sshd[12345]: test" };
            auto result = matcher.detectFormat( lines );

            THEN( "No format is detected" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}
