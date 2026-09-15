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

#include "logformatcatalog.h"

#include <QDir>
#include <QTemporaryDir>

// Helper to write a format JSON file into a directory
static void writeFormatFile( const QDir& dir, const QString& filename, const char* content )
{
    QFile file( dir.filePath( filename ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate ) );
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

// A user's replacement for the built-in "syslog_log", told apart by its title.
static const char* UserSyslogFormatJson = R"({
    "syslog_log": {
        "title": "My Custom Syslog",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[^ ]+) (?<body>.*)$"
            }
        },
        "sample": [{ "line": "2024-01-01 hello" }]
    }
})";

static const char* EditedUserSyslogFormatJson = R"({
    "syslog_log": {
        "title": "My Edited Syslog",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>[^ ]+) (?<body>.*)$"
            }
        },
        "sample": [{ "line": "2024-01-01 hello" }]
    }
})";

SCENARIO( "LogFormatCatalog loads formats from a directory", "[logformat][catalog]" )
{
    GIVEN( "A temporary directory with format JSON files" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        QDir dir( tempDir.path() );
        writeFormatFile( dir, "syslog.json", SyslogFormatJson );
        writeFormatFile( dir, "apache.json", ApacheFormatJson );

        LogFormatCatalog catalog;
        catalog.loadFromDirectory( dir.path() );

        THEN( "All formats from all files are loaded" )
        {
            REQUIRE( catalog.formatCount() == 2 );
        }

        THEN( "Formats can be looked up by name" )
        {
            auto syslog = catalog.formatByName( "syslog_log" );
            REQUIRE( syslog != nullptr );
            REQUIRE( syslog->title() == "Syslog" );

            auto apache = catalog.formatByName( "access_log" );
            REQUIRE( apache != nullptr );
            REQUIRE( apache->title() == "Common Access Log" );
        }

        THEN( "Looking a format up twice hands out the same Log Format, not a copy" )
        {
            REQUIRE( catalog.formatByName( "syslog_log" ) == catalog.formatByName( "syslog_log" ) );
        }

        THEN( "Non-existent format returns nullptr" )
        {
            REQUIRE( catalog.formatByName( "nonexistent" ) == nullptr );
        }

        THEN( "All format names can be listed" )
        {
            auto names = catalog.formatNames();
            REQUIRE( names.size() == 2 );
            REQUIRE( names.contains( "syslog_log" ) );
            REQUIRE( names.contains( "access_log" ) );
        }
    }
}

SCENARIO( "LogFormatCatalog handles empty directory", "[logformat][catalog]" )
{
    GIVEN( "An empty temporary directory" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        LogFormatCatalog catalog;
        catalog.loadFromDirectory( tempDir.path() );

        THEN( "No formats are loaded" )
        {
            REQUIRE( catalog.formatCount() == 0 );
        }
    }
}

SCENARIO( "LogFormatCatalog handles non-existent directory", "[logformat][catalog]" )
{
    GIVEN( "A path that does not exist" )
    {
        LogFormatCatalog catalog;
        catalog.loadFromDirectory( "/nonexistent/path/formats" );

        THEN( "No formats are loaded and no crash" )
        {
            REQUIRE( catalog.formatCount() == 0 );
        }
    }
}

SCENARIO( "LogFormatCatalog user formats override built-in", "[logformat][catalog]" )
{
    GIVEN( "Two directories with the same format name" )
    {
        QTemporaryDir builtinDir;
        QTemporaryDir userDir;
        REQUIRE( builtinDir.isValid() );
        REQUIRE( userDir.isValid() );

        writeFormatFile( QDir( builtinDir.path() ), "syslog.json", SyslogFormatJson );
        writeFormatFile( QDir( userDir.path() ), "syslog.json", UserSyslogFormatJson );

        LogFormatCatalog catalog;
        catalog.loadFromDirectory( builtinDir.path() );
        catalog.loadFromDirectory( userDir.path() );

        THEN( "The user format overrides the built-in" )
        {
            auto syslog = catalog.formatByName( "syslog_log" );
            REQUIRE( syslog != nullptr );
            REQUIRE( syslog->title() == "My Custom Syslog" );
        }

        THEN( "Format count is still 1 (not duplicated)" )
        {
            REQUIRE( catalog.formatCount() == 1 );
        }
    }
}

SCENARIO( "LogFormatCatalog ignores non-JSON files", "[logformat][catalog]" )
{
    GIVEN( "A directory with JSON and non-JSON files" )
    {
        QTemporaryDir tempDir;
        REQUIRE( tempDir.isValid() );

        QDir dir( tempDir.path() );
        writeFormatFile( dir, "syslog.json", SyslogFormatJson );
        writeFormatFile( dir, "readme.txt", "This is not a format file" );
        writeFormatFile( dir, "notes.md", "# Notes" );

        LogFormatCatalog catalog;
        catalog.loadFromDirectory( dir.path() );

        THEN( "Only the JSON format is loaded" )
        {
            REQUIRE( catalog.formatCount() == 1 );
        }
    }
}

SCENARIO( "The Log Format Catalog is built from the built-in and the user's Log Formats",
          "[logformat][catalog]" )
{
    GIVEN( "a user formats directory that replaces a built-in Log Format" )
    {
        QTemporaryDir userDir;
        REQUIRE( userDir.isValid() );
        writeFormatFile( QDir( userDir.path() ), "syslog.json", UserSyslogFormatJson );

        LogFormatCatalog catalog{ userDir.path() };

        WHEN( "the Catalog is built" )
        {
            catalog.rebuild();

            THEN( "it holds the built-in Log Formats" )
            {
                REQUIRE( catalog.formatByName( "access_log" ) != nullptr );
                REQUIRE( catalog.formatCount() > 1 );
            }

            THEN( "the user's Log Format replaces the built-in one of the same name" )
            {
                const auto syslog = catalog.formatByName( "syslog_log" );
                REQUIRE( syslog != nullptr );
                REQUIRE( syslog->title() == "My Custom Syslog" );
            }

            AND_WHEN( "a Log Format is held, the user edits it, and the Catalog is rebuilt" )
            {
                const auto held = catalog.formatByName( "syslog_log" );
                const auto formatCount = catalog.formatCount();

                writeFormatFile( QDir( userDir.path() ), "syslog.json",
                                 EditedUserSyslogFormatJson );
                catalog.rebuild();

                THEN( "the held Log Format is still valid and unchanged" )
                {
                    REQUIRE( held != nullptr );
                    REQUIRE( held->title() == "My Custom Syslog" );
                    REQUIRE( held->name() == "syslog_log" );
                }

                THEN( "the Catalog now hands out the edited Log Format, a new object" )
                {
                    const auto rebuilt = catalog.formatByName( "syslog_log" );
                    REQUIRE( rebuilt != held );
                    REQUIRE( rebuilt->title() == "My Edited Syslog" );
                }

                THEN( "nothing was loaded twice" )
                {
                    REQUIRE( catalog.formatCount() == formatCount );
                }
            }
        }
    }

    GIVEN( "a Catalog with no user formats directory" )
    {
        LogFormatCatalog catalog;

        WHEN( "it is built" )
        {
            catalog.rebuild();

            THEN( "it holds the built-in Log Formats only" )
            {
                const auto syslog = catalog.formatByName( "syslog_log" );
                REQUIRE( syslog != nullptr );
                REQUIRE( syslog->title() != "My Custom Syslog" );
            }
        }
    }
}
