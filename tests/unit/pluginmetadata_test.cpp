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

#include "pluginmetadata.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

// A plugin ships Log Formats by naming their files in its manifest (#793):
// paths relative to the plugin directory, which they may not leave.
SCENARIO( "A plugin manifest names the Log Format files the plugin ships", "[pluginmetadata]" )
{
    GIVEN( "a manifest without the formats key" )
    {
        const auto result = PluginMetadata::fromJson( validManifest(), "test" );
        REQUIRE( result.has_value() );

        THEN( "the plugin ships no Log Format" )
        {
            REQUIRE( result->formats().isEmpty() );
            REQUIRE( result->formatPaths().isEmpty() );
        }
    }

    GIVEN( "a manifest naming two Log Format files" )
    {
        const char* json = R"({
            "id": "com.example.test",
            "name": "Test Plugin",
            "version": "1.0.0",
            "type": "ui",
            "library": "libtest.dylib",
            "api_version": 1,
            "formats": [ "formats/first.json", "second.json" ]
        })";

        WHEN( "it is parsed from text" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE( result.has_value() );

            THEN( "the files are kept in the manifest's order, relative to the plugin directory" )
            {
                REQUIRE( result->formats() == QStringList{ "formats/first.json", "second.json" } );
            }

            THEN( "without a plugin directory there is no absolute path to them" )
            {
                REQUIRE( result->formatPaths().isEmpty() );
            }
        }

        WHEN( "it is read from a plugin directory" )
        {
            QTemporaryDir pluginDir;
            REQUIRE( pluginDir.isValid() );
            const auto manifestPath = QDir( pluginDir.path() ).filePath( "plugin.json" );
            {
                QFile manifest( manifestPath );
                REQUIRE( manifest.open( QIODevice::WriteOnly ) );
                manifest.write( json );
            }
            const auto result = PluginMetadata::fromJsonFile( manifestPath );
            REQUIRE( result.has_value() );

            THEN( "the files are resolved against the plugin directory" )
            {
                const auto directory = QFileInfo( manifestPath ).absolutePath();
                REQUIRE( result->formatPaths()
                         == QStringList{ QDir( directory ).filePath( "formats/first.json" ),
                                         QDir( directory ).filePath( "second.json" ) } );
            }
        }
    }

    GIVEN( "manifests whose formats key is not a list of paths inside the plugin directory" )
    {
        const auto manifestWithFormats = []( const char* formats ) {
            return QStringLiteral( R"({
                "id": "com.example.test",
                "name": "Test Plugin",
                "version": "1.0.0",
                "type": "ui",
                "library": "libtest.dylib",
                "api_version": 1,
                "formats": %1
            })" )
                .arg( formats )
                .toUtf8();
        };

        THEN( "a formats key that is not a list is rejected" )
        {
            const auto result
                = PluginMetadata::fromJson( manifestWithFormats( R"("formats/x.json")" ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "formats" ) );
        }

        THEN( "an entry that is not a string is rejected" )
        {
            const auto result
                = PluginMetadata::fromJson( manifestWithFormats( R"([ "x.json", 3 ])" ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "formats" ) );
        }

        THEN( "an empty path is rejected" )
        {
            const auto result
                = PluginMetadata::fromJson( manifestWithFormats( R"([ "" ])" ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "formats" ) );
        }

        THEN( "a path that leaves the plugin directory is rejected" )
        {
            for ( const auto* formats :
                  { R"([ "../x.json" ])", R"([ "formats/../../x.json" ])", R"([ ".." ])" } ) {
                INFO( formats );
                const auto result
                    = PluginMetadata::fromJson( manifestWithFormats( formats ), "test" );
                REQUIRE_FALSE( result.has_value() );
                REQUIRE( result.error().contains( "leaves the plugin directory" ) );
            }
        }

        THEN( "an absolute path is rejected" )
        {
            const auto result
                = PluginMetadata::fromJson( manifestWithFormats( R"([ "/etc/x.json" ])" ), "test" );
            REQUIRE_FALSE( result.has_value() );
            REQUIRE( result.error().contains( "leaves the plugin directory" ) );
        }

        THEN( "a path that stays inside the plugin directory is kept as written" )
        {
            const auto result = PluginMetadata::fromJson(
                manifestWithFormats( R"([ "formats/../x.json", "./y.json" ])" ), "test" );
            REQUIRE( result.has_value() );
            REQUIRE( result->formats() == QStringList{ "formats/../x.json", "./y.json" } );
        }
    }
}

SCENARIO( "A plugin id is a plain name, never a path", "[pluginmetadata][plugins]" )
{
    GIVEN( "Ids made of letters, digits, dots, dashes and underscores" )
    {
        THEN( "They are accepted" )
        {
            CHECK( PluginMetadata::isValidId( "io.github.foo.bar" ) );
            CHECK( PluginMetadata::isValidId( "my-plugin_1" ) );
            CHECK( PluginMetadata::isValidId( "A1" ) );
        }
    }

    GIVEN( "Ids that name a path, hide a folder or are empty" )
    {
        THEN( "They are rejected (#819)" )
        {
            CHECK_FALSE( PluginMetadata::isValidId( "/Users/me/.ssh" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "C:\\Users\\me" ) );
            CHECK_FALSE( PluginMetadata::isValidId( ".." ) );
            CHECK_FALSE( PluginMetadata::isValidId( "a/b" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "a\\b" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "../a" ) );
            CHECK_FALSE( PluginMetadata::isValidId( ".hidden" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "a b" ) );
            CHECK_FALSE( PluginMetadata::isValidId( "a~b" ) );
        }
    }

    GIVEN( "A manifest whose id leaves the plugin directory" )
    {
        const auto json = R"({
            "id": "../../.ssh",
            "name": "Escape",
            "version": "1.0.0",
            "type": "datasource",
            "library": "libescape.dylib",
            "api_version": 1
        })";

        THEN( "Parsing fails naming the id" )
        {
            const auto result = PluginMetadata::fromJson( QByteArray( json ), "test" );
            REQUIRE_FALSE( result.has_value() );
            CHECK( result.error().contains( "'id'" ) );
            CHECK( result.error().contains( "../../.ssh" ) );
        }
    }
}
