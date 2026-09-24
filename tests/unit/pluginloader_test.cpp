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

// The Plugin Loader turns a manifest into a loaded plugin, or says why not (#444).

#include <catch2/catch_test_macros.hpp>

#include "pluginloader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTemporaryDir>

#include <utility>

using logsquirl::plugins::PluginLoader;
using logsquirl::plugins::PluginMetadata;

namespace {

using ShutdownCallsFn = int ( * )();

/// Writes a plugin.json for the library at libraryPath and parses it.
PluginMetadata manifestFor( const QTemporaryDir& dir, const QString& libraryPath,
                            const QString& type = QStringLiteral( "converter" ) )
{
    const auto manifestPath = QDir( dir.path() ).filePath( "plugin.json" );
    QFile manifest( manifestPath );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    // Built as JSON, not pasted into a string: a Windows path has backslashes.
    const QJsonObject object{ { "id", "com.test.loader" },
                              { "name", "Loader Test" },
                              { "version", "1.0.0" },
                              { "type", type },
                              { "library", libraryPath },
                              { "api_version", 1 } };
    manifest.write( QJsonDocument( object ).toJson() );
    manifest.close();

    auto metadata = PluginMetadata::fromJsonFile( manifestPath );
    REQUIRE( metadata.has_value() );
    return *metadata;
}

/// How often the fixture library has been shut down so far.
int shutdownCallsOf( const QString& libraryPath )
{
    QLibrary library( libraryPath );
    REQUIRE( library.load() );
    const auto fn = reinterpret_cast<ShutdownCallsFn>(
        library.resolve( "logsquirl_fixture_shutdown_calls" ) );
    REQUIRE( fn != nullptr );
    return fn();
}

QString loadError( const QString& libraryPath )
{
    QTemporaryDir dir;
    const auto result = PluginLoader::load( manifestFor( dir, libraryPath ) );
    REQUIRE( !result.has_value() );
    return result.error();
}

} // namespace

SCENARIO( "The Plugin Loader reports why a plugin does not load", "[pluginloader][plugins]" )
{
    GIVEN( "A manifest that was not read from a file" )
    {
        const auto metadata = PluginMetadata::fromJson(
            R"({"id":"a","name":"a","version":"1","type":"ui","library":"a.so","api_version":1})",
            "test" );
        REQUIRE( metadata.has_value() );

        THEN( "It has no library path and the loader refuses it" )
        {
            const auto result = PluginLoader::load( *metadata );
            REQUIRE( !result.has_value() );
            REQUIRE( result.error().contains( "empty" ) );
        }
    }

    GIVEN( "A manifest whose library does not exist" )
    {
        THEN( "The error names the library" )
        {
            const auto error = loadError( QStringLiteral( "no-such-plugin-library" ) );
            REQUIRE( error.contains( "Failed to load library" ) );
            REQUIRE( error.contains( "no-such-plugin-library" ) );
        }
    }

    GIVEN( "A library that is not a shared library" )
    {
        QTemporaryDir dir;
        const auto notALibrary = QDir( dir.path() ).filePath( "plugin.txt" );
        QFile file( notALibrary );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "this is text, not machine code" );
        file.close();

        THEN( "The loader fails instead of resolving symbols" )
        {
            const auto result = PluginLoader::load( manifestFor( dir, notALibrary ) );
            REQUIRE( !result.has_value() );
            REQUIRE( result.error().contains( "Failed to load library" ) );
        }
    }

    GIVEN( "A library missing a required entry point" )
    {
        THEN( "The error names the missing symbol" )
        {
            REQUIRE( loadError( QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_NO_GET_INFO_PATH ) )
                         .contains( "logsquirl_plugin_get_info" ) );
            REQUIRE( loadError( QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_NO_INIT_PATH ) )
                         .contains( "logsquirl_plugin_init" ) );
            REQUIRE( loadError( QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_NO_SHUTDOWN_PATH ) )
                         .contains( "logsquirl_plugin_shutdown" ) );
        }
    }

    GIVEN( "A library whose get_info returns nothing" )
    {
        THEN( "The loader reports it" )
        {
            REQUIRE( loadError( QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_NULL_INFO_PATH ) )
                         .contains( "returned null" ) );
        }
    }

    GIVEN( "A library built for another API version than its manifest says" )
    {
        THEN( "The loader names both versions" )
        {
            const auto error
                = loadError( QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_BAD_API_VERSION_PATH ) );
            REQUIRE( error.contains( "api_version 99" ) );
            REQUIRE( error.contains( "host supports 1" ) );
        }
    }
}

SCENARIO( "A loaded plugin is started and stopped once", "[pluginloader][plugins]" )
{
    GIVEN( "A plugin whose init fails" )
    {
        QTemporaryDir dir;
        auto result = PluginLoader::load(
            manifestFor( dir, QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_INIT_FAILS_PATH ) ) );
        REQUIRE( result.has_value() );

        WHEN( "It is initialised" )
        {
            const auto error = result->init( nullptr, nullptr );

            THEN( "The error carries the code and the plugin stays uninitialised" )
            {
                REQUIRE( error.contains( "7" ) );
                REQUIRE( !result->isInitialised() );
            }
        }
    }

    GIVEN( "A plugin that loads" )
    {
        const auto path = QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_CONVERTER_PATH );
        QTemporaryDir dir;
        const int shutdownsBefore = shutdownCallsOf( path );

        THEN( "It keeps the manifest and has no configure dialog" )
        {
            auto result = PluginLoader::load( manifestFor( dir, path ) );
            REQUIRE( result.has_value() );
            REQUIRE( result->metadata().id() == "com.test.loader" );
            REQUIRE( !result->hasConfigureUi() );
            REQUIRE( !result->isInitialised() );
        }

        THEN( "Shutting down or destroying it before init calls no shutdown" )
        {
            {
                auto result = PluginLoader::load( manifestFor( dir, path ) );
                REQUIRE( result.has_value() );
                result->shutdown();
            }
            REQUIRE( shutdownCallsOf( path ) == shutdownsBefore );
        }

        THEN( "A second init is refused and destruction shuts down exactly once" )
        {
            {
                auto result = PluginLoader::load( manifestFor( dir, path ) );
                REQUIRE( result.has_value() );
                REQUIRE( result->init( nullptr, nullptr ).isEmpty() );
                REQUIRE( result->isInitialised() );
                REQUIRE( result->init( nullptr, nullptr ).contains( "already" ) );
                REQUIRE( result->isInitialised() );
            }
            REQUIRE( shutdownCallsOf( path ) == shutdownsBefore + 1 );
        }

        THEN( "Moving an initialised handle moves the duty to shut down" )
        {
            {
                auto result = PluginLoader::load( manifestFor( dir, path ) );
                REQUIRE( result.has_value() );
                REQUIRE( result->init( nullptr, nullptr ).isEmpty() );

                auto moved = std::move( *result );
                REQUIRE( moved.isInitialised() );
                REQUIRE( !result->isInitialised() );
            }
            REQUIRE( shutdownCallsOf( path ) == shutdownsBefore + 1 );
        }
    }
}

SCENARIO( "A converter plugin converts through its C entry points", "[pluginloader][plugins]" )
{
    GIVEN( "A converter library" )
    {
        QTemporaryDir dir;
        auto result = PluginLoader::load(
            manifestFor( dir, QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_CONVERTER_PATH ) ) );
        REQUIRE( result.has_value() );

        THEN( "It offers its extensions and converts" )
        {
            REQUIRE( result->isConverter() );
            REQUIRE( result->converterExtensions() == "abc;xyz" );
            REQUIRE( result->convert( "good", "out" ) == 0 );
            // Paths cross the C boundary as UTF-8: the umlaut is two bytes.
            REQUIRE( result->convert( QString::fromUtf8( "\xc3\xa4" ), "o" ) == 201 );
        }
    }

    GIVEN( "A plugin with converter entry points that the manifest calls a datasource" )
    {
        QTemporaryDir dir;
        auto result = PluginLoader::load(
            manifestFor( dir, QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_CONVERTER_PATH ),
                         QStringLiteral( "datasource" ) ) );
        REQUIRE( result.has_value() );

        THEN( "It is not treated as a converter" )
        {
            REQUIRE( !result->isConverter() );
        }
    }

    GIVEN( "A plugin without converter entry points" )
    {
        QTemporaryDir dir;
        auto result = PluginLoader::load(
            manifestFor( dir, QStringLiteral( LOGSQUIRL_LOADER_FIXTURE_INIT_FAILS_PATH ) ) );
        REQUIRE( result.has_value() );

        THEN( "It is no converter and a conversion fails" )
        {
            REQUIRE( !result->isConverter() );
            REQUIRE( result->converterExtensions().isEmpty() );
            REQUIRE( result->convert( "a", "b" ) == -1 );
        }
    }
}
