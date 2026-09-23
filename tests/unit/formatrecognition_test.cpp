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

#include <QDir>
#include <QTemporaryDir>

constexpr RecognitionPolicy Enabled{ .enabled = true };
constexpr RecognitionPolicy Disabled{ .enabled = false };

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

SCENARIO( "Format Recognition recognizes syslog format", "[logformat][recognition]" )
{
    GIVEN( "A Catalog with syslog and java log formats" )
    {
        LogFormatCatalog catalog;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            catalog.addFormat( std::move( f ) );
        }

        auto javaFormats = LogFormatParser::parseJsonString( JavaLogJson );
        for ( auto& f : javaFormats ) {
            catalog.addFormat( std::move( f ) );
        }

        WHEN( "Given syslog lines" )
        {
            QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user",
                "Jun 15 10:21:05 myhost sshd[12345]: pam_unix(sshd:session): session opened",
                "Jun 15 10:21:06 myhost cron[99]: (root) CMD (/usr/bin/some_job)",
            };

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "Syslog format is recognized" )
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

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "Java log format is recognized" )
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

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "No format is recognized" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}

SCENARIO( "Format Recognition prefers more specific format", "[logformat][recognition]" )
{
    GIVEN( "A Catalog with a specific and a generic format" )
    {
        LogFormatCatalog catalog;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            catalog.addFormat( std::move( f ) );
        }

        auto genericFormats = LogFormatParser::parseJsonString( GenericJson );
        for ( auto& f : genericFormats ) {
            catalog.addFormat( std::move( f ) );
        }

        WHEN( "Given syslog lines (match both formats)" )
        {
            QStringList lines = {
                "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user",
                "Jun 15 10:21:05 myhost sshd[12345]: pam_unix(sshd:session): session opened",
            };

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "The more specific format (syslog) wins over generic" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }
    }
}

SCENARIO( "Format Recognition requires minimum match threshold", "[logformat][recognition]" )
{
    GIVEN( "A Catalog with a format" )
    {
        LogFormatCatalog catalog;

        auto syslogFormats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : syslogFormats ) {
            catalog.addFormat( std::move( f ) );
        }

        WHEN( "Only 1 out of 10 lines match" )
        {
            QStringList lines;
            lines << "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user";
            for ( int i = 0; i < 9; ++i ) {
                lines << "random garbage line";
            }

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "No format is recognized (below threshold)" )
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

            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "Format is detected (above threshold)" )
            {
                REQUIRE( result != nullptr );
                REQUIRE( result->name() == "syslog_log" );
            }
        }
    }
}

SCENARIO( "Format Recognition handles empty input", "[logformat][recognition]" )
{
    GIVEN( "A Catalog with formats" )
    {
        LogFormatCatalog catalog;
        auto formats = LogFormatParser::parseJsonString( SyslogJson );
        for ( auto& f : formats ) {
            catalog.addFormat( std::move( f ) );
        }

        WHEN( "Given an empty line list" )
        {
            auto result = FormatRecognition::recognize( QStringList{}, Enabled, catalog );

            THEN( "No format is recognized" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}

SCENARIO( "Format Recognition handles empty Catalog", "[logformat][recognition]" )
{
    GIVEN( "An empty Catalog" )
    {
        LogFormatCatalog catalog;

        WHEN( "Given lines" )
        {
            QStringList lines = { "Jun 15 10:21:04 myhost sshd[12345]: test" };
            auto result = FormatRecognition::recognize( lines, Enabled, catalog );

            THEN( "No format is recognized" )
            {
                REQUIRE( result == nullptr );
            }
        }
    }
}

namespace {

// A Log File in memory that counts how many of its Log Lines are read.
// FakeLogData serves every read, a range of Log Lines included, through
// doGetLineString(), so counting there counts them all.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable int linesRead = 0;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        ++linesRead;
        return FakeLogData::doGetLineString( line );
    }
};

QStringList syslogLines( int count )
{
    QStringList lines;
    for ( int i = 0; i < count; ++i ) {
        lines << QString( "Jun 15 10:21:%1 myhost sshd[12345]: Line %2" )
                     .arg( i % 60, 2, 10, QChar( '0' ) )
                     .arg( i );
    }
    return lines;
}

LogFormatCatalog catalogOf( std::initializer_list<const char*> formatsJson )
{
    LogFormatCatalog catalog;
    for ( const auto* json : formatsJson ) {
        for ( auto& format : LogFormatParser::parseJsonString( json ) ) {
            catalog.addFormat( std::move( format ) );
        }
    }
    return catalog;
}

} // namespace

SCENARIO( "Format Recognition answers with the Catalog's own Log Format",
          "[logformat][recognition]" )
{
    GIVEN( "a Catalog with syslog and Java Log Formats and a Log File of syslog lines" )
    {
        const auto catalog = catalogOf( { SyslogJson, JavaLogJson } );
        CountingLogData logFile{ syslogLines( 10 ) };

        WHEN( "its Log Format is recognized under an enabled Recognition Policy" )
        {
            const auto recognized = FormatRecognition::recognize( logFile, Enabled, catalog );

            THEN( "the answer is the very Log Format the Catalog holds, not a copy" )
            {
                REQUIRE( recognized != nullptr );
                REQUIRE( recognized.get() == catalog.formatByName( "syslog_log" ).get() );
            }
        }

        WHEN( "its Log Format is recognized under a disabled Recognition Policy" )
        {
            const auto recognized = FormatRecognition::recognize( logFile, Disabled, catalog );

            THEN( "nothing is recognized" )
            {
                REQUIRE( recognized == nullptr );
            }

            THEN( "not a single Log Line was read to match against" )
            {
                REQUIRE( logFile.linesRead == 0 );
            }
        }

        WHEN( "sample lines are recognized under a disabled Recognition Policy" )
        {
            const auto recognized
                = FormatRecognition::recognize( syslogLines( 10 ), Disabled, catalog );

            THEN( "nothing is recognized" )
            {
                REQUIRE( recognized == nullptr );
            }
        }
    }

    GIVEN( "a Log File whose Log Lines match no Log Format" )
    {
        const auto catalog = catalogOf( { SyslogJson } );
        const FakeLogData logFile{ QStringList{ "just some random text", "1234567890" } };

        THEN( "nothing is recognized" )
        {
            REQUIRE( FormatRecognition::recognize( logFile, Enabled, catalog ) == nullptr );
        }
    }

    GIVEN( "an empty Log File" )
    {
        const auto catalog = catalogOf( { SyslogJson } );
        const FakeLogData logFile;

        THEN( "nothing is recognized" )
        {
            REQUIRE( FormatRecognition::recognize( logFile, Enabled, catalog ) == nullptr );
        }
    }
}

SCENARIO( "Format Recognition looks at the first 50 Log Lines only", "[logformat][recognition]" )
{
    GIVEN( "a Log File that is syslog for its first 50 Log Lines and something else after" )
    {
        const auto catalog = catalogOf( { SyslogJson } );

        auto lines = syslogLines( 50 );
        for ( int i = 0; i < 500; ++i ) {
            lines << "random garbage line";
        }
        CountingLogData logFile{ lines };

        WHEN( "its Log Format is recognized" )
        {
            const auto recognized = FormatRecognition::recognize( logFile, Enabled, catalog );

            THEN( "the Log Lines past the sample depth do not count against the match" )
            {
                REQUIRE( recognized == catalog.formatByName( "syslog_log" ) );
            }

            THEN( "no more than the sample depth was read" )
            {
                REQUIRE( logFile.linesRead == 50 );
            }
        }
    }
}
