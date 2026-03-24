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

#include "pluginmetadata.h"

#include <QByteArray>

using logsquirl::plugins::PluginMetadata;

namespace {

// Minimal valid plugin.json as a byte array
QByteArray validManifest()
{
    return R"({
        "id": "com.example.test",
        "name": "Test Plugin",
        "version": "1.0.0",
        "type": "datasource",
        "library": "libtest.dylib",
        "api_version": 1,
        "description": "A test plugin",
        "author": "Test Author",
        "license": "MIT"
    })";
}

} // namespace

SCENARIO( "PluginMetadata parses valid manifests", "[pluginmetadata]" )
{
    GIVEN( "A valid datasource plugin manifest" )
    {
        const auto result = PluginMetadata::fromJson( validManifest(), "test" );

        THEN( "Parsing succeeds" )
        {
            REQUIRE( result.has_value() );
        }

        THEN( "All fields are populated" )
        {
            const auto& meta = result.value();
            REQUIRE( meta.id() == "com.example.test" );
            REQUIRE( meta.name() == "Test Plugin" );
            REQUIRE( meta.version() == "1.0.0" );
            REQUIRE( meta.type() == LOGSQUIRL_PLUGIN_DATASOURCE );
            REQUIRE( meta.library() == "libtest.dylib" );
            REQUIRE( meta.apiVersion() == LOGSQUIRL_PLUGIN_API_VERSION );
            REQUIRE( meta.description() == "A test plugin" );
            REQUIRE( meta.author() == "Test Author" );
            REQUIRE( meta.license() == "MIT" );
        }
    }

    GIVEN( "A valid converter plugin manifest" )
    {
        auto json = R"({
            "id": "com.example.converter",
            "name": "CSV Converter",
            "version": "0.1.0",
            "type": "converter",
            "library": "libcsv.dylib",
            "api_version": 1
        })";

        WHEN( "Parsed" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );

            THEN( "Type is converter" )
            {
                REQUIRE( result.has_value() );
                REQUIRE( result->type() == LOGSQUIRL_PLUGIN_CONVERTER );
            }
        }
    }

    GIVEN( "A valid UI extension plugin manifest" )
    {
        auto json = R"({
            "id": "com.example.ui",
            "name": "Theme Plugin",
            "version": "2.0.0",
            "type": "ui",
            "library": "libtheme.dylib",
            "api_version": 1
        })";

        WHEN( "Parsed" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );

            THEN( "Type is UI" )
            {
                REQUIRE( result.has_value() );
                REQUIRE( result->type() == LOGSQUIRL_PLUGIN_UI );
            }
        }
    }

    GIVEN( "A manifest with optional fields omitted" )
    {
        auto json = R"({
            "id": "com.example.minimal",
            "name": "Minimal",
            "version": "0.0.1",
            "type": "datasource",
            "library": "libmin.dylib",
            "api_version": 1
        })";

        WHEN( "Parsed" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );

            THEN( "Succeeds with empty optional fields" )
            {
                REQUIRE( result.has_value() );
                REQUIRE( result->description().isEmpty() );
                REQUIRE( result->author().isEmpty() );
                REQUIRE( result->license().isEmpty() );
            }
        }
    }
}

SCENARIO( "PluginMetadata rejects invalid manifests", "[pluginmetadata]" )
{
    GIVEN( "Invalid JSON syntax" )
    {
        const auto result = PluginMetadata::fromJson( "{ not json }", "test" );

        THEN( "Parsing fails with a JSON error" )
        {
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "JSON parse error" ) );
        }
    }

    GIVEN( "JSON that is an array instead of an object" )
    {
        const auto result = PluginMetadata::fromJson( "[]", "test" );

        THEN( "Parsing fails" )
        {
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "not a JSON object" ) );
        }
    }

    GIVEN( "A manifest missing the 'id' field" )
    {
        auto json = R"({
            "name": "No ID",
            "version": "1.0.0",
            "type": "datasource",
            "library": "libno.dylib",
            "api_version": 1
        })";

        THEN( "Parsing fails with missing field error" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "'id'" ) );
        }
    }

    GIVEN( "A manifest missing the 'library' field" )
    {
        auto json = R"({
            "id": "com.example.nolib",
            "name": "No Library",
            "version": "1.0.0",
            "type": "datasource",
            "api_version": 1
        })";

        THEN( "Parsing fails" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "'library'" ) );
        }
    }

    GIVEN( "A manifest missing 'api_version'" )
    {
        auto json = R"({
            "id": "com.example.nover",
            "name": "No API Version",
            "version": "1.0.0",
            "type": "datasource",
            "library": "libnover.dylib"
        })";

        THEN( "Parsing fails" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "api_version" ) );
        }
    }

    GIVEN( "A manifest with incompatible api_version" )
    {
        auto json = R"({
            "id": "com.example.future",
            "name": "Future Plugin",
            "version": "1.0.0",
            "type": "datasource",
            "library": "libfuture.dylib",
            "api_version": 999
        })";

        THEN( "Parsing fails with version mismatch" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "incompatible" ) );
        }
    }

    GIVEN( "A manifest with an unknown plugin type" )
    {
        auto json = R"({
            "id": "com.example.badtype",
            "name": "Bad Type",
            "version": "1.0.0",
            "type": "unknown_type",
            "library": "libbad.dylib",
            "api_version": 1
        })";

        THEN( "Parsing fails with unknown type error" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "Unknown plugin type" ) );
        }
    }
}

SCENARIO( "PluginMetadata::libraryPath combines directory and library", "[pluginmetadata]" )
{
    GIVEN( "A manifest loaded from a file with a directory set" )
    {
        // fromJsonFile sets the directory; we can test fromJson + check
        // that libraryPath returns empty when directory is not set
        const auto result = PluginMetadata::fromJson( validManifest(), "test" );
        REQUIRE( result.has_value() );

        THEN( "libraryPath is empty when no directory is set" )
        {
            REQUIRE( result->libraryPath().isEmpty() );
        }
    }
}
