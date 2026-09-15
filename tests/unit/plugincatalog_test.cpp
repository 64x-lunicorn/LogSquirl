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

#include "plugincatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using logsquirl::plugins::PluginCatalog;

namespace {

/// Writes a plugin.json with the given id and name into root/subdirectory.
void writeManifest( const QString& root, const QString& subdirectory, const QString& id,
                    const QString& name = QStringLiteral( "Test Plugin" ) )
{
    const auto pluginDir = QDir( root ).filePath( subdirectory );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "%2",
        "version": "1.0.0",
        "type": "datasource",
        "library": "libtest.dylib",
        "api_version": 1
    })" )
                        .arg( id, name )
                        .toUtf8() );
}

/// Writes a file with the given content into root/subdirectory.
void writeFile( const QString& root, const QString& subdirectory, const QString& fileName,
                const QByteArray& content )
{
    const auto dir = QDir( root ).filePath( subdirectory );
    REQUIRE( QDir().mkpath( dir ) );

    QFile file( QDir( dir ).filePath( fileName ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( content );
}

} // namespace

SCENARIO( "The Plugin Catalog discovers plugins in a directory", "[plugincatalog][plugins]" )
{
    GIVEN( "A directory with one plugin manifest" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        writeManifest( root.path(), "test-plugin", "com.test.discovered", "Discovered Test" );

        PluginCatalog catalog;

        WHEN( "The directory is scanned" )
        {
            catalog.discoverPluginsIn( root.path() );

            THEN( "The catalog lists the plugin with its manifest and directory" )
            {
                REQUIRE( catalog.discoveredPlugins().size() == 1 );
                const auto& meta = catalog.discoveredPlugins().front();
                CHECK( meta.id() == "com.test.discovered" );
                CHECK( meta.name() == "Discovered Test" );
                CHECK( QDir( meta.directory() ) == QDir( root.filePath( "test-plugin" ) ) );
            }

            THEN( "The plugin is found by its id and an unknown id is not" )
            {
                const auto* found = catalog.findDiscovered( "com.test.discovered" );
                REQUIRE( found != nullptr );
                CHECK( found->id() == "com.test.discovered" );
                CHECK( catalog.findDiscovered( "com.test.unknown" ) == nullptr );
            }
        }
    }
}

SCENARIO( "The Plugin Catalog skips what is not a plugin", "[plugincatalog][plugins]" )
{
    GIVEN( "A directory with a valid manifest, an invalid one, a folder without one and a "
           "manifest at the top level" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        writeManifest( root.path(), "valid", "com.test.valid" );
        writeFile( root.path(), "broken", "plugin.json", "{ not json" );
        writeFile( root.path(), "incomplete", "plugin.json", R"({ "id": "com.test.incomplete" })" );
        writeFile( root.path(), "empty-folder", "readme.txt", "no manifest here" );
        writeFile( root.path(), ".", "plugin.json", R"({ "id": "com.test.top-level" })" );

        PluginCatalog catalog;

        WHEN( "The directory is scanned" )
        {
            catalog.discoverPluginsIn( root.path() );

            THEN( "Only the valid plugin is listed" )
            {
                REQUIRE( catalog.discoveredPlugins().size() == 1 );
                CHECK( catalog.discoveredPlugins().front().id() == "com.test.valid" );
            }
        }
    }

    GIVEN( "A directory that does not exist" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PluginCatalog catalog;

        WHEN( "It is scanned" )
        {
            catalog.discoverPluginsIn( root.filePath( "missing" ) );

            THEN( "The catalog stays empty" )
            {
                CHECK( catalog.discoveredPlugins().empty() );
            }
        }
    }
}

SCENARIO( "The Plugin Catalog keeps the first plugin of an id", "[plugincatalog][plugins]" )
{
    GIVEN( "Two directories that both contain a plugin with the same id" )
    {
        QTemporaryDir first;
        QTemporaryDir second;
        REQUIRE( first.isValid() );
        REQUIRE( second.isValid() );
        writeManifest( first.path(), "plugin", "com.test.same", "First" );
        writeManifest( second.path(), "plugin", "com.test.same", "Second" );
        writeManifest( second.path(), "other", "com.test.other", "Other" );

        PluginCatalog catalog;

        WHEN( "Both directories are scanned in order" )
        {
            catalog.discoverPluginsIn( first.path() );
            catalog.discoverPluginsIn( second.path() );

            THEN( "The results are merged and the id keeps the plugin found first" )
            {
                REQUIRE( catalog.discoveredPlugins().size() == 2 );
                const auto* same = catalog.findDiscovered( "com.test.same" );
                REQUIRE( same != nullptr );
                CHECK( same->name() == "First" );
                CHECK( catalog.findDiscovered( "com.test.other" ) != nullptr );
            }
        }
    }
}

SCENARIO( "Rediscovering replaces what the Plugin Catalog found before",
          "[plugincatalog][plugins]" )
{
    GIVEN( "A catalog that has discovered a plugin in one directory" )
    {
        QTemporaryDir old;
        QTemporaryDir current;
        REQUIRE( old.isValid() );
        REQUIRE( current.isValid() );
        writeManifest( old.path(), "plugin", "com.test.removed" );
        writeManifest( current.path(), "plugin", "com.test.installed" );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( old.path() );
        REQUIRE( catalog.discoveredPlugins().size() == 1 );

        WHEN( "The catalog discovers the plugins of another directory list" )
        {
            catalog.discoverPlugins( QStringList{ current.path() } );

            THEN( "It lists only the plugins found in that list" )
            {
                REQUIRE( catalog.discoveredPlugins().size() == 1 );
                CHECK( catalog.discoveredPlugins().front().id() == "com.test.installed" );
                CHECK( catalog.findDiscovered( "com.test.removed" ) == nullptr );
            }
        }
    }
}

SCENARIO( "The Plugin Catalog searches the application and the user plugin directories",
          "[plugincatalog][plugins]" )
{
    GIVEN( "The default plugin directories" )
    {
        const auto dirs = PluginCatalog::defaultPluginDirectories();

        THEN( "There is one next to the application and one in the user's data" )
        {
            REQUIRE( dirs.size() == 2 );
            CHECK( QDir::cleanPath( dirs[ 0 ] )
                       .startsWith(
                           QDir::cleanPath( QCoreApplication::applicationDirPath() + "/.." ) ) );
            CHECK( dirs[ 1 ].endsWith( "/plugins" ) );
        }
    }
}
