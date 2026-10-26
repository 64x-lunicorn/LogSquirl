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

// A plugin removes a menu item it registered with an id, from every window
// (#857). The plugin is a real library built against the current header,
// loaded by the Application Plugins, and its items sit in the Plugins menus
// of two real windows.

#include "applicationplugins.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QMenu>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.menu-actions-plugin" );

/// The plugin config directory the host creates goes to a test location, not the user's.
struct StandardPathsInTestMode {
    StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( true );
    }
    ~StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( false );
    }
    StandardPathsInTestMode( const StandardPathsInTestMode& ) = delete;
    StandardPathsInTestMode& operator=( const StandardPathsInTestMode& ) = delete;
};

/// Writes a plugin.json for the plugin into its own subdirectory of root.
void installPlugin( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "menu-actions-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Menu Actions Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_MENU_ACTIONS_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// The plugin's exports the tests call. The library stays loaded as long as this does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_MENU_ACTIONS_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    static constexpr int Stays = 0;
    static constexpr int Goes = 1;

    int id( int item )
    {
        using Fn = int ( * )( int );
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_menu_actions_plugin_id" ) )(
            item );
    }

    int runs( int item )
    {
        using Fn = int ( * )( int );
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_menu_actions_plugin_runs" ) )(
            item );
    }

    void unregister( int id )
    {
        using Fn = void ( * )( int );
        reinterpret_cast<Fn>( library.resolve( "logsquirl_menu_actions_plugin_unregister" ) )( id );
    }

    QLibrary library;
};

/// The window's Plugins menu, or nullptr.
QMenu* pluginsMenuOf( const MainWindow& window )
{
    for ( auto* menu : window.findChildren<QMenu*>() ) {
        if ( menu->title() == QStringLiteral( "Plugins" ) ) {
            return menu;
        }
    }
    return nullptr;
}

/// The action of the menu with the given text, or nullptr.
QAction* actionNamed( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

/// The submenu of the menu with the given title, or nullptr.
QMenu* submenuNamed( const QMenu& menu, const QString& title )
{
    auto* action = actionNamed( menu, title );
    return action ? action->menu() : nullptr;
}

/// The "Goes" item in Plugins > Menu Probe > Sub of the window, or nullptr.
QAction* goesActionOf( const MainWindow& window )
{
    auto* plugins = pluginsMenuOf( window );
    auto* probe = plugins ? submenuNamed( *plugins, QStringLiteral( "Menu Probe" ) ) : nullptr;
    auto* sub = probe ? submenuNamed( *probe, QStringLiteral( "Sub" ) ) : nullptr;
    return sub ? actionNamed( *sub, QStringLiteral( "Goes" ) ) : nullptr;
}

/// The "Stays" item directly in the window's Plugins menu, or nullptr.
QAction* staysActionOf( const MainWindow& window )
{
    auto* plugins = pluginsMenuOf( window );
    return plugins ? actionNamed( *plugins, QStringLiteral( "Stays" ) ) : nullptr;
}

std::shared_ptr<Session> newSession()
{
    return std::make_shared<Session>( testSettingsPolicies(),
                                      std::make_shared<LogFormatCatalog>() );
}

} // namespace

SCENARIO( "A plugin removes a menu item it registered with an id from every window",
          "[ui][plugins][pluginmenuactions]" )
{
    GIVEN( "Two shown windows and a plugin that registered two items with "
           "register_menu_action_ex, one of them in a submenu of its own" )
    {
        const StandardPathsInTestMode testPaths;
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installPlugin( pluginRoot.path() );

        QStringList loadErrors;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { PluginId } } ).errors;
            } );
        const auto session = newSession();
        auto first = std::make_unique<MainWindow>( WindowSession{ session, "First", 0 }, plugins );
        showUntilExposed( *first );
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        auto second
            = std::make_unique<MainWindow>( WindowSession{ session, "Second", 1 }, plugins );
        showUntilExposed( *second );
        QTest::qWait( 50 );

        PluginLibrary plugin;
        const auto staysId = plugin.id( PluginLibrary::Stays );
        const auto goesId = plugin.id( PluginLibrary::Goes );

        THEN( "both items show in both windows, each with an id of its own" )
        {
            REQUIRE( staysId > 0 );
            REQUIRE( goesId > 0 );
            REQUIRE( staysId != goesId );
            for ( const auto* window : { first.get(), second.get() } ) {
                REQUIRE( staysActionOf( *window ) != nullptr );
                REQUIRE( goesActionOf( *window ) != nullptr );
            }
        }

        WHEN( "the plugin unregisters the item in the submenu" )
        {
            plugin.unregister( goesId );

            THEN( "it is gone from both windows, and so is the submenu nothing is left in" )
            {
                for ( const auto* window : { first.get(), second.get() } ) {
                    auto* menu = pluginsMenuOf( *window );
                    REQUIRE( menu != nullptr );
                    REQUIRE( goesActionOf( *window ) == nullptr );
                    REQUIRE( submenuNamed( *menu, QStringLiteral( "Menu Probe" ) ) == nullptr );
                }
            }

            THEN( "the other item stays in both windows and still runs the plugin's callback" )
            {
                staysActionOf( *first )->trigger();
                staysActionOf( *second )->trigger();
                REQUIRE( plugin.runs( PluginLibrary::Stays ) == 2 );
                REQUIRE( plugin.runs( PluginLibrary::Goes ) == 0 );
            }

            AND_WHEN( "it unregisters the same id again, and ids it was never given" )
            {
                plugin.unregister( goesId );
                plugin.unregister( staysId + goesId + 100 );
                plugin.unregister( 0 );
                plugin.unregister( -7 );
                QTest::qWait( 50 );

                THEN( "nothing changes" )
                {
                    REQUIRE( staysActionOf( *first ) != nullptr );
                    REQUIRE( staysActionOf( *second ) != nullptr );
                    staysActionOf( *first )->trigger();
                    REQUIRE( plugin.runs( PluginLibrary::Stays ) == 1 );
                }
            }
        }

        WHEN( "a trigger of the item is queued, and the plugin unregisters it before the event "
              "loop turns" )
        {
            auto* goes = goesActionOf( *first );
            REQUIRE( goes != nullptr );
            QMetaObject::invokeMethod( goes, &QAction::trigger, Qt::QueuedConnection );
            plugin.unregister( goesId );
            QTest::qWait( 50 );

            THEN( "the queued trigger does not call the plugin" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Goes ) == 0 );
            }
        }

        second.reset();
        first.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }
}
