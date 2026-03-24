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

#include "configuration.h"
#include "pluginmanager.h"

#include <QDir>
#include <QTemporaryDir>

using logsquirl::plugins::PluginManager;

SCENARIO( "PluginManager autoLoadPlugins with empty enabled list", "[pluginmanager][plugins]" )
{
    GIVEN( "A PluginManager with no discovered plugins" )
    {
        PluginManager manager;

        WHEN( "Configuration has auto-load enabled but empty enabled list" )
        {
            auto& config = Configuration::get();
            config.setPluginsAutoLoad( true );
            config.setEnabledPlugins( {} );

            THEN( "autoLoadPlugins returns no errors" )
            {
                const auto errors = manager.autoLoadPlugins();
                REQUIRE( errors.isEmpty() );
            }
        }
    }
}

SCENARIO( "PluginManager autoLoadPlugins skips unknown plugins", "[pluginmanager][plugins]" )
{
    GIVEN( "A PluginManager with no discovered plugins" )
    {
        PluginManager manager;

        WHEN( "Configuration lists plugins that are not discovered" )
        {
            auto& config = Configuration::get();
            config.setPluginsAutoLoad( true );
            config.setEnabledPlugins(
                QStringList{ "com.example.nonexistent", "com.example.missing" } );

            THEN( "autoLoadPlugins returns no errors but skips them" )
            {
                const auto errors = manager.autoLoadPlugins();
                // Unknown plugins are silently skipped (logged as warning)
                REQUIRE( errors.isEmpty() );
            }
        }
    }
}

SCENARIO( "PluginManager autoLoadPlugins respects auto-load=false", "[pluginmanager][plugins]" )
{
    GIVEN( "A PluginManager with auto-load disabled" )
    {
        PluginManager manager;

        WHEN( "Configuration has auto-load disabled with some enabled IDs" )
        {
            auto& config = Configuration::get();
            config.setPluginsAutoLoad( false );
            config.setEnabledPlugins( QStringList{ "com.example.a" } );

            THEN( "autoLoadPlugins returns immediately without attempting to load" )
            {
                const auto errors = manager.autoLoadPlugins();
                REQUIRE( errors.isEmpty() );
                REQUIRE( manager.loadedPluginIds().isEmpty() );
            }
        }

        // Restore default for other tests
        auto& config = Configuration::get();
        config.setPluginsAutoLoad( true );
        config.setEnabledPlugins( {} );
    }
}

SCENARIO( "PluginManager discovers plugins in a temporary directory",
          "[pluginmanager][plugins]" )
{
    GIVEN( "A temporary directory with a valid plugin.json" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        // Create a plugin subdirectory with a manifest
        const auto pluginDir = tmpDir.path() + QDir::separator() + "test-plugin";
        QDir().mkpath( pluginDir );

        QFile manifest( pluginDir + QDir::separator() + "plugin.json" );
        REQUIRE( manifest.open( QIODevice::WriteOnly ) );
        manifest.write( R"({
            "id": "com.test.discovered",
            "name": "Discovered Test",
            "version": "1.0.0",
            "type": "datasource",
            "library": "libtest.dylib",
            "api_version": 1
        })" );
        manifest.close();

        PluginManager manager;
        manager.discoverPluginsIn( tmpDir.path() );

        THEN( "The plugin is in the discovered list" )
        {
            REQUIRE( manager.discoveredPlugins().size() == 1 );
            REQUIRE( manager.discoveredPlugins()[ 0 ].id() == "com.test.discovered" );
        }

        THEN( "The plugin is not loaded yet" )
        {
            REQUIRE_FALSE( manager.isLoaded( "com.test.discovered" ) );
            REQUIRE( manager.loadedPluginIds().isEmpty() );
        }

        WHEN( "autoLoadPlugins is called with this plugin enabled" )
        {
            auto& config = Configuration::get();
            config.setPluginsAutoLoad( true );
            config.setEnabledPlugins( QStringList{ "com.test.discovered" } );

            const auto errors = manager.autoLoadPlugins();

            THEN( "Loading fails because the library does not exist" )
            {
                // The plugin has a manifest but no actual library file,
                // so loadPlugin will fail — this is expected
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors[ 0 ].contains( "com.test.discovered" ) );
            }

            // Restore config
            config.setEnabledPlugins( {} );
        }
    }
}
