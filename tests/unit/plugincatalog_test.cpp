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

#include "datalocation.h"
#include "plugincatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
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

/// This binary is an installed run, unless someone left a logsquirl.conf
/// beside it: then its user plugin directory is the application plugin
/// directory (#602), and what an installed run keeps apart cannot be checked.
void skipWhenPortable()
{
    if ( DataLocation::current().isPortable() ) {
        SKIP( "a logsquirl.conf beside the test binary makes this run portable" );
    }
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

SCENARIO( "The Plugin Catalog searches the user and then the application plugin directory",
          "[plugincatalog][plugins]" )
{
    GIVEN( "The default plugin directories of an installed run" )
    {
        skipWhenPortable();
        const auto dirs = PluginCatalog::defaultPluginDirectories();

        THEN( "The user plugin directory in the user's data comes first" )
        {
            REQUIRE( dirs.size() == 2 );
            CHECK( dirs[ 0 ] == PluginCatalog::userPluginDirectory() );
            CHECK( dirs[ 0 ]
                   == QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                          + "/plugins" );
        }

        THEN( "The application plugin directory next to the application comes second" )
        {
            REQUIRE( dirs.size() == 2 );
            CHECK( dirs[ 1 ] == PluginCatalog::applicationPluginDirectory() );
            CHECK( QDir::cleanPath( dirs[ 1 ] )
                       .startsWith(
                           QDir::cleanPath( QCoreApplication::applicationDirPath() + "/.." ) ) );
        }
    }
}

SCENARIO( "A plugin from the catalog is installed into the user plugin directory",
          "[plugincatalog][plugins]" )
{
    GIVEN( "The id of a plugin in the catalog, in an installed run" )
    {
        skipWhenPortable();
        const auto pluginId = QStringLiteral( "com.test.installed" );

        WHEN( "Its install directory is asked for" )
        {
            const auto installDir = PluginCatalog::installDirectory( pluginId );

            THEN( "It is the plugin's folder in the user plugin directory" )
            {
                CHECK(
                    QDir::cleanPath( installDir )
                    == QDir::cleanPath( PluginCatalog::userPluginDirectory() + "/" + pluginId ) );
            }

            THEN( "It is not in the application plugin directory" )
            {
                CHECK_FALSE(
                    QDir::cleanPath( installDir )
                        .startsWith( QDir::cleanPath( PluginCatalog::applicationPluginDirectory() )
                                     + "/" ) );
            }
        }
    }
}

SCENARIO( "A plugin updated into the user plugin directory wins over the shipped copy",
          "[plugincatalog][plugins]" )
{
    GIVEN( "A shipped plugin and a newer copy of it in the user plugin directory" )
    {
        QTemporaryDir user;
        QTemporaryDir application;
        REQUIRE( user.isValid() );
        REQUIRE( application.isValid() );
        writeManifest( application.path(), "plugin", "com.test.shipped", "Shipped" );
        writeManifest( application.path(), "other", "com.test.bundled", "Bundled" );
        writeManifest( user.path(), "com.test.shipped", "com.test.shipped", "Updated" );

        PluginCatalog catalog;

        WHEN( "The directories are scanned in the default order" )
        {
            // The same order defaultPluginDirectories() returns, with temporary
            // directories standing in for the real ones.
            catalog.discoverPlugins( QStringList{ user.path(), application.path() } );

            THEN( "The user's copy is listed and the other shipped plugin still is" )
            {
                REQUIRE( catalog.discoveredPlugins().size() == 2 );
                const auto* shipped = catalog.findDiscovered( "com.test.shipped" );
                REQUIRE( shipped != nullptr );
                CHECK( shipped->name() == "Updated" );
                CHECK( catalog.findDiscovered( "com.test.bundled" ) != nullptr );
            }
        }
    }
}
