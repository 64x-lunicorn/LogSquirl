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

#include "logsquirl_plugin_api.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "pluginuiport.h"

#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QTemporaryDir>

#include <type_traits>
#include <vector>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginUiPort;
using logsquirl::plugins::PluginWidgetHandle;

namespace {

const auto ProbeId = QStringLiteral( "io.github.logsquirl.test.ui-port-probe" );

/// One call that reached the fake Plugin UI Port.
struct PortCall {
    enum class Kind {
        AddStatusWidget,
        RemoveStatusWidget,
        AddSidebarTab,
        RemoveSidebarTab,
        AddFooterWidget,
        RemoveFooterWidget,
        AddMenuAction,
        RemoveContributions,
    };

    Kind kind = Kind::AddStatusWidget;
    QString pluginId{};
    QString menuPath{};
    QString label{};
    PluginWidgetHandle widget{};
    PluginCallbackFn callback = nullptr;
    void* userData = nullptr;
};

/// Records every call instead of showing anything.
class FakePluginUiPort : public PluginUiPort {
public:
    std::vector<PortCall> calls;
    PluginWidgetHandle parent;

    void addStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        calls.push_back(
            { .kind = PortCall::Kind::AddStatusWidget, .pluginId = pluginId, .widget = widget } );
    }

    void removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        calls.push_back( { .kind = PortCall::Kind::RemoveStatusWidget,
                           .pluginId = pluginId,
                           .widget = widget } );
    }

    void addSidebarTab( const QString& pluginId, const QString& label,
                        PluginWidgetHandle widget ) override
    {
        calls.push_back( { .kind = PortCall::Kind::AddSidebarTab,
                           .pluginId = pluginId,
                           .label = label,
                           .widget = widget } );
    }

    void removeSidebarTab( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        calls.push_back(
            { .kind = PortCall::Kind::RemoveSidebarTab, .pluginId = pluginId, .widget = widget } );
    }

    void addFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        calls.push_back(
            { .kind = PortCall::Kind::AddFooterWidget, .pluginId = pluginId, .widget = widget } );
    }

    void removeFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        calls.push_back( { .kind = PortCall::Kind::RemoveFooterWidget,
                           .pluginId = pluginId,
                           .widget = widget } );
    }

    void addMenuAction( const QString& pluginId, const QString& menuPath, const QString& label,
                        PluginCallbackFn callback, void* userData ) override
    {
        calls.push_back( { .kind = PortCall::Kind::AddMenuAction,
                           .pluginId = pluginId,
                           .menuPath = menuPath,
                           .label = label,
                           .callback = callback,
                           .userData = userData } );
    }

    void removeContributions( const QString& pluginId ) override
    {
        calls.push_back( { .kind = PortCall::Kind::RemoveContributions, .pluginId = pluginId } );
    }

    PluginWidgetHandle configurationParent() override
    {
        return parent;
    }
};

/// The exported functions of the probe plugin the tests reach behind the host's back.
struct Probe {
    explicit Probe( const QString& path )
        : library( path )
    {
    }

    const LogSquirlHostApi* hostApi()
    {
        using Fn = const LogSquirlHostApi* ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_host_api" ) )();
    }

    void* hostHandle()
    {
        using Fn = void* ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_host_handle" ) )();
    }

    void* configureParent()
    {
        using Fn = void* ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_configure_parent" ) )();
    }

    void* shutdownFooterWidget()
    {
        using Fn = void* ( * )();
        return reinterpret_cast<Fn>(
            library.resolve( "logsquirl_probe_shutdown_footer_widget" ) )();
    }

    QLibrary library;
};

/// Writes a plugin.json for the probe plugin into its own subdirectory of root.
void installProbe( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "ui-port-probe" );
    QDir().mkpath( pluginDir );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "UI Port Probe",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( ProbeId, QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) )
                        .toUtf8() );
}

/// Writes a plugin.json whose library does not exist into its own subdirectory of root.
void installManifest( const QString& root, const QString& id, const QString& library )
{
    const auto pluginDir = QDir( root ).filePath( id );
    QDir().mkpath( pluginDir );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Manifest Only",
        "version": "1.0.0",
        "type": "datasource",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( id, library )
                        .toUtf8() );
}

void menuCallback( void* ) {}

/// What the active-file callback a test registers through the probe received.
struct ActiveFileCalls {
    QStringList paths;
};

void recordActiveFile( void* userData, const char* filePath )
{
    static_cast<ActiveFileCalls*>( userData )->paths.append( QString::fromUtf8( filePath ) );
}

} // namespace

SCENARIO( "A Plugin Host is only created for a catalog that outlives it", "[pluginhost][plugins]" )
{
    GIVEN( "The ways a Plugin Host can be created" )
    {
        WHEN( "It is given a catalog" )
        {
            THEN( "A catalog variable is accepted and a temporary one is not" )
            {
                // The host keeps a reference to the catalog; a temporary one
                // would be gone before the host uses it.
                REQUIRE( std::is_constructible_v<PluginHost, PluginCatalog&> );
                REQUIRE( std::is_constructible_v<PluginHost, const PluginCatalog&> );
                REQUIRE_FALSE( std::is_constructible_v<PluginHost, PluginCatalog&&> );
                REQUIRE_FALSE( std::is_constructible_v<PluginHost, const PluginCatalog&&> );
            }
        }
    }
}

SCENARIO( "A plugin's host callbacks reach the Plugin UI Port",
          "[pluginhost][pluginuiport][plugins]" )
{
    GIVEN( "A Plugin Host with a fake Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );

        int statusWidget = 0;
        int sidebarWidget = 0;
        int footerWidget = 0;
        int menuUserData = 0;

        WHEN( "The plugin registers and unregisters a status widget" )
        {
            api->register_status_widget( handle, &statusWidget );
            api->unregister_status_widget( handle, &statusWidget );

            THEN( "The port adds and removes that widget for the plugin" )
            {
                REQUIRE( port.calls.size() == 2 );
                REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::AddStatusWidget );
                REQUIRE( port.calls[ 1 ].kind == PortCall::Kind::RemoveStatusWidget );
                for ( const auto& call : port.calls ) {
                    REQUIRE( call.pluginId == ProbeId );
                    REQUIRE( call.widget.widget == &statusWidget );
                }
            }
        }

        WHEN( "The plugin registers and unregisters a sidebar tab" )
        {
            api->register_sidebar_tab( handle, "Probe tab", &sidebarWidget );
            api->unregister_sidebar_tab( handle, &sidebarWidget );

            THEN( "The port adds the tab with its label and removes it for the plugin" )
            {
                REQUIRE( port.calls.size() == 2 );
                REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::AddSidebarTab );
                REQUIRE( port.calls[ 0 ].label == "Probe tab" );
                REQUIRE( port.calls[ 1 ].kind == PortCall::Kind::RemoveSidebarTab );
                for ( const auto& call : port.calls ) {
                    REQUIRE( call.pluginId == ProbeId );
                    REQUIRE( call.widget.widget == &sidebarWidget );
                }
            }
        }

        WHEN( "The plugin registers and unregisters a footer widget" )
        {
            api->register_footer_widget( handle, &footerWidget );
            api->unregister_footer_widget( handle, &footerWidget );

            THEN( "The port adds and removes that widget for the plugin" )
            {
                REQUIRE( port.calls.size() == 2 );
                REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::AddFooterWidget );
                REQUIRE( port.calls[ 1 ].kind == PortCall::Kind::RemoveFooterWidget );
                for ( const auto& call : port.calls ) {
                    REQUIRE( call.pluginId == ProbeId );
                    REQUIRE( call.widget.widget == &footerWidget );
                }
            }
        }

        WHEN( "The plugin registers a menu action" )
        {
            api->register_menu_action( handle, "Plugins", "Probe action", &menuCallback,
                                       &menuUserData );

            THEN( "The port adds the action with its path, label, callback and user data" )
            {
                REQUIRE( port.calls.size() == 1 );
                const auto& call = port.calls[ 0 ];
                REQUIRE( call.kind == PortCall::Kind::AddMenuAction );
                REQUIRE( call.pluginId == ProbeId );
                REQUIRE( call.menuPath == "Plugins" );
                REQUIRE( call.label == "Probe action" );
                REQUIRE( call.callback == &menuCallback );
                REQUIRE( call.userData == &menuUserData );
            }
        }

        WHEN( "The plugin is configured" )
        {
            int parentWidget = 0;
            port.parent = PluginWidgetHandle{ &parentWidget };
            host.configurePlugin( ProbeId );

            THEN( "The plugin gets the parent the port chose" )
            {
                REQUIRE( probe.configureParent() == &parentWidget );
            }
        }

        WHEN( "The plugin is unloaded" )
        {
            const auto* shutdownFooter = probe.shutdownFooterWidget();
            host.unloadPlugin( ProbeId );

            THEN( "The widget it unregisters on shutdown is removed before all its "
                  "contributions are" )
            {
                REQUIRE( port.calls.size() == 2 );
                REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::RemoveFooterWidget );
                REQUIRE( port.calls[ 0 ].pluginId == ProbeId );
                REQUIRE( port.calls[ 0 ].widget.widget == shutdownFooter );
                REQUIRE( port.calls[ 1 ].kind == PortCall::Kind::RemoveContributions );
                REQUIRE( port.calls[ 1 ].pluginId == ProbeId );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

SCENARIO( "The Plugin Host answers a plugin's calls about files and notifications",
          "[pluginhost][plugins]" )
{
    GIVEN( "A Plugin Host with a fake Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );

        ActiveFileCalls activeFiles;

        WHEN( "The plugin registers an active-file callback and the active file changes" )
        {
            api->register_active_file_callback( handle, &recordActiveFile, &activeFiles );
            host.notifyActiveFileChanged( QStringLiteral( "/logs/app.log" ) );
            host.notifyActiveFileChanged( QString() );

            THEN( "The callback receives each active file, an empty path when none is" )
            {
                REQUIRE( activeFiles.paths == QStringList{ "/logs/app.log", "" } );
            }
        }

        WHEN( "The plugin is unloaded after registering an active-file callback" )
        {
            api->register_active_file_callback( handle, &recordActiveFile, &activeFiles );
            host.unloadPlugin( ProbeId );
            host.notifyActiveFileChanged( QStringLiteral( "/logs/app.log" ) );

            THEN( "The callback is not called any more" )
            {
                REQUIRE( activeFiles.paths.isEmpty() );
            }
        }

        WHEN( "The plugin asks for the active file path" )
        {
            host.setActiveFilePathCallback( [] { return QStringLiteral( "/logs/current.log" ); } );

            THEN( "It gets the path the host's callback returns" )
            {
                REQUIRE( QString::fromUtf8( api->get_active_file_path( handle ) )
                         == "/logs/current.log" );
            }
        }

        WHEN( "The plugin asks the host to open a file" )
        {
            QString openedPath;
            bool openedFollowing = false;
            host.setOpenFileCallback( [ & ]( const QString& path, bool follow ) {
                openedPath = path;
                openedFollowing = follow;
            } );
            api->open_file( handle, "/logs/opened.log", 1 );

            THEN( "The host's open-file callback gets the path and the follow flag" )
            {
                REQUIRE( openedPath == "/logs/opened.log" );
                REQUIRE( openedFollowing );
            }
        }

        WHEN( "The plugin shows a notification" )
        {
            QStringList notifications;
            QObject::connect( &host, &PluginHost::notificationRequested,
                              [ & ]( const QString& message ) { notifications << message; } );
            api->show_notification( handle, "Probe says hello" );

            THEN( "The host requests that notification" )
            {
                REQUIRE( notifications == QStringList{ "Probe says hello" } );
            }
        }

        WHEN( "The plugin asks for its configuration directory" )
        {
            const auto configDir = QString::fromUtf8( api->get_config_dir( handle ) );

            THEN( "It gets a directory of its own that exists" )
            {
                REQUIRE( configDir.endsWith( "/plugin_config/" + ProbeId ) );
                REQUIRE( QDir( configDir ).exists() );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

SCENARIO( "The Plugin Host loads the plugins enabled in the configuration",
          "[pluginhost][plugins]" )
{
    GIVEN( "A Plugin Host with a fake Plugin UI Port and an empty catalog" )
    {
        PluginCatalog catalog;
        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        WHEN( "Auto-load is enabled but no plugin is enabled" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = true, .enabled = {} } );

            THEN( "Nothing is loaded or enabled, and there are no errors" )
            {
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }

        WHEN( "The configuration enables plugins the catalog does not list" )
        {
            const auto errors = host.autoLoadPlugins( { .autoLoad = true,
                                                        .enabled = { "com.example.nonexistent",
                                                                     "com.example.missing" } } )
                                    .errors;

            THEN( "They are skipped without an error" )
            {
                REQUIRE( errors.isEmpty() );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }
    }

    GIVEN( "A Plugin Host with a fake Plugin UI Port and a catalog listing the probe plugin "
           "and a plugin whose library is missing" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );
        const auto missingId = QStringLiteral( "com.test.discovered" );
        installManifest( pluginRoot.path(), missingId, QStringLiteral( "libtest.dylib" ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        REQUIRE( catalog.discoveredPlugins().size() == 2 );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        QStringList loadedSignals;
        QObject::connect( &host, &PluginHost::pluginLoaded,
                          [ & ]( const QString& pluginId ) { loadedSignals << pluginId; } );

        WHEN( "Auto-load is disabled and both plugins are enabled" )
        {
            const auto loaded
                = host.autoLoadPlugins( { .autoLoad = false, .enabled = { ProbeId, missingId } } );

            THEN( "Nothing is loaded or enabled, and there are no errors" )
            {
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }

        WHEN( "Auto-load is disabled and no plugin has been enabled yet" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = false, .enabled = {} } );

            THEN( "Nothing is loaded or enabled" )
            {
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }

        WHEN( "The probe plugin is enabled" )
        {
            const auto loaded
                = host.autoLoadPlugins( { .autoLoad = true, .enabled = { ProbeId } } );

            THEN( "It is loaded and announced, and no plugin is enabled besides it" )
            {
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( host.isLoaded( ProbeId ) );
                REQUIRE( host.loadedPluginIds() == QStringList{ ProbeId } );
                REQUIRE( loadedSignals == QStringList{ ProbeId } );
            }

            AND_WHEN( "The plugins are loaded again" )
            {
                const auto againErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { ProbeId } } ).errors;

                THEN( "The loaded plugin is skipped" )
                {
                    REQUIRE( againErrors.isEmpty() );
                    REQUIRE( loadedSignals == QStringList{ ProbeId } );
                }
            }
        }

        WHEN( "The plugin whose library is missing is enabled" )
        {
            const auto errors
                = host.autoLoadPlugins( { .autoLoad = true, .enabled = { missingId } } ).errors;

            THEN( "Loading it fails with an error naming the plugin" )
            {
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors[ 0 ].contains( missingId ) );
                REQUIRE_FALSE( host.isLoaded( missingId ) );
            }
        }

        WHEN( "No plugin has been enabled yet" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = true, .enabled = {} } );

            THEN( "Every plugin in the catalog is enabled, for the caller to keep, and those "
                  "that can be are loaded" )
            {
                REQUIRE( loaded.enabledOnFirstRun );
                // Discovery order depends on the file system.
                auto enabled = *loaded.enabledOnFirstRun;
                enabled.sort();
                auto expected = QStringList{ ProbeId, missingId };
                expected.sort();
                REQUIRE( enabled == expected );
                REQUIRE( loaded.errors.size() == 1 );
                REQUIRE( host.loadedPluginIds() == QStringList{ ProbeId } );
            }
        }

        host.unloadAll();
    }
}

SCENARIO( "The Plugin Host loads only plugins from its catalog", "[pluginhost][plugins]" )
{
    GIVEN( "A Plugin Host whose catalog lists a plugin with a .lua script as its library" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installManifest( pluginRoot.path(), QStringLiteral( "com.test.script" ),
                         QStringLiteral( "script.lua" ) );
        QFile script( QDir( pluginRoot.filePath( "com.test.script" ) ).filePath( "script.lua" ) );
        REQUIRE( script.open( QIODevice::WriteOnly ) );
        script.write( "function plugin_init(host) end\n" );
        script.close();

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        REQUIRE( catalog.discoveredPlugins().size() == 1 );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        WHEN( "That plugin is loaded" )
        {
            const auto error = host.loadPlugin( "com.test.script" );

            THEN( "Loading fails with the normal shared-library load error" )
            {
                REQUIRE( error.startsWith( "Failed to load library" ) );
                REQUIRE( error.contains( "script.lua" ) );
                REQUIRE_FALSE( host.isLoaded( "com.test.script" ) );
            }
        }

        WHEN( "A plugin the catalog does not list is loaded" )
        {
            const auto error = host.loadPlugin( "com.test.unknown" );

            THEN( "Loading fails and nothing is loaded" )
            {
                REQUIRE( error.contains( "not found" ) );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }
    }
}
