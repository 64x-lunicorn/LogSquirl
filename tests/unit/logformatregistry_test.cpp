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

#include "logformatregistry.h"

#include <QDir>
#include <QTemporaryDir>

// Helper to write a format JSON file into a directory
static void writeFormatFile( const QDir& dir, const QString& filename, const char* content )
{
    QFile file( dir.filePath( filename ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
    file.write( content );
    file.close();
}

static const char* SyslogFormatJson = R"({
    "syslog_log": {
        "title": "Syslog",
        "description": "The system logger format",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[A-Z][a-z]{2}\\s+\\d+\\s+\\d{2}:\\d{2}:\\d{2})\\s+(?<hostname>[^ ]+)\\s+(?<service>[^\\[]+)\\[(?<pid>\\d+)\\]:\\s+(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "hostname": { "kind": "string", "identifier": true },
            "service": { "kind": "string", "identifier": true },
            "pid": { "kind": "integer" }
        },
        "sample": [
            { "line": "Jun 15 10:21:04 myhost sshd[12345]: Accepted publickey for user" }
        ]
    }
})";

static const char* ApacheFormatJson = R"({
    "access_log": {
        "title": "Common Access Log",
        "description": "Apache access log format",
        "regex": {
            "basic": {
                "pattern": "^(?<c_ip>[^ ]+)\\s+[^ ]+\\s+(?<cs_username>[^ ]+)\\s+\\[(?<timestamp>[^\\]]+)\\]\\s+\"(?<cs_method>\\w+)\\s+(?<cs_uri_stem>[^ ]+)\\s+[^\"]+\"\\s+(?<sc_status>\\d+)\\s+(?<sc_bytes>\\d+)\\s*(?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "body-field": "body",
        "value": {
            "c_ip": { "kind": "string", "identifier": true },
            "cs_method": { "kind": "string", "identifier": true },
            "cs_uri_stem": { "kind": "string" },
            "sc_status": { "kind": "integer" },
            "sc_bytes": { "kind": "integer" }
        },
        "sample": [
            { "line": "192.168.1.1 - admin [01/Jan/2024:12:00:00 +0000] \"GET /index.html HTTP/1.1\" 200 1234" }
        ]
    }
})";

SCENARIO( "LogFormatRegistry loads formats from a directory", "[logformat][registry]" )
{
    GIVEN( "A temporary directory with format JSON files" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        QDir dir( tempDir.path() );
        writeFormatFile( dir, "syslog.json", SyslogFormatJson );
        writeFormatFile( dir, "apache.json", ApacheFormatJson );

        LogFormatRegistry registry;
        registry.loadFromDirectory( dir.path() );

        THEN( "All formats from all files are loaded" )
        {
            REQUIRE( registry.formatCount() == 2 );
        }

        THEN( "Formats can be looked up by name" )
        {
            auto syslog = registry.formatByName( "syslog_log" );
            REQUIRE( syslog != nullptr );
            REQUIRE( syslog->title() == "Syslog" );

            auto apache = registry.formatByName( "access_log" );
            REQUIRE( apache != nullptr );
            REQUIRE( apache->title() == "Common Access Log" );
        }

        THEN( "Non-existent format returns nullptr" )
        {
            REQUIRE( registry.formatByName( "nonexistent" ) == nullptr );
        }

        THEN( "All format names can be listed" )
        {
            auto names = registry.formatNames();
            REQUIRE( names.size() == 2 );
            REQUIRE( names.contains( "syslog_log" ) );
            REQUIRE( names.contains( "access_log" ) );
        }
    }
}

SCENARIO( "LogFormatRegistry handles empty directory", "[logformat][registry]" )
{
    GIVEN( "An empty temporary directory" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        LogFormatRegistry registry;
        registry.loadFromDirectory( tempDir.path() );

        THEN( "No formats are loaded" )
        {
            REQUIRE( registry.formatCount() == 0 );
        }
    }
}

SCENARIO( "LogFormatRegistry handles non-existent directory", "[logformat][registry]" )
{
    GIVEN( "A path that does not exist" )
    {
        LogFormatRegistry registry;
        registry.loadFromDirectory( "/nonexistent/path/formats" );

        THEN( "No formats are loaded and no crash" )
        {
            REQUIRE( registry.formatCount() == 0 );
        }
    }
}

SCENARIO( "LogFormatRegistry user formats override built-in", "[logformat][registry]" )
{
    GIVEN( "Two directories with the same format name" )
    {
        QTemporaryDir builtinDir;
        QTemporaryDir userDir;
        REQUIRE( builtinDir.isValid() );
        REQUIRE( userDir.isValid() );

        // Built-in version
        writeFormatFile( QDir( builtinDir.path() ), "syslog.json", SyslogFormatJson );

        // User override with a different title
        writeFormatFile( QDir( userDir.path() ), "syslog.json", R"({
            "syslog_log": {
                "title": "My Custom Syslog",
                "regex": {
                    "basic": {
                        "pattern": "^(?<timestamp>[^ ]+) (?<body>.*)$"
                    }
                },
                "sample": [{ "line": "2024-01-01 hello" }]
            }
        })" );

        LogFormatRegistry registry;
        registry.loadFromDirectory( builtinDir.path() );
        registry.loadFromDirectory( userDir.path() );

        THEN( "The user format overrides the built-in" )
        {
            auto syslog = registry.formatByName( "syslog_log" );
            REQUIRE( syslog != nullptr );
            REQUIRE( syslog->title() == "My Custom Syslog" );
        }

        THEN( "Format count is still 1 (not duplicated)" )
        {
            REQUIRE( registry.formatCount() == 1 );
        }
    }
}

SCENARIO( "LogFormatRegistry ignores non-JSON files", "[logformat][registry]" )
{
    GIVEN( "A directory with JSON and non-JSON files" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        QDir dir( tempDir.path() );
        writeFormatFile( dir, "syslog.json", SyslogFormatJson );
        writeFormatFile( dir, "readme.txt", "This is not a format file" );
        writeFormatFile( dir, "notes.md", "# Notes" );

        LogFormatRegistry registry;
        registry.loadFromDirectory( dir.path() );

        THEN( "Only the JSON format is loaded" )
        {
            REQUIRE( registry.formatCount() == 1 );
        }
    }
}
