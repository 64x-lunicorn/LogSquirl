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
#include "pluginmanager.h"
#include "pluginuiport.h"

#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QTemporaryDir>

#include <vector>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginManager;
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
    QString pluginId;
    QString menuPath;
    QString label;
    PluginWidgetHandle widget;
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

void menuCallback( void* ) {}

} // namespace

SCENARIO( "A plugin's host callbacks reach the Plugin UI Port", "[pluginuiport][plugins]" )
{
    GIVEN( "A PluginManager with a fake Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        FakePluginUiPort port;
        PluginManager manager;
        manager.setUiPort( &port );
        manager.discoverPluginsIn( pluginRoot.path() );
        REQUIRE( manager.loadPlugin( ProbeId ).isEmpty() );

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
                CHECK( port.calls[ 0 ].kind == PortCall::Kind::AddStatusWidget );
                CHECK( port.calls[ 1 ].kind == PortCall::Kind::RemoveStatusWidget );
                for ( const auto& call : port.calls ) {
                    CHECK( call.pluginId == ProbeId );
                    CHECK( call.widget.widget == &statusWidget );
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
                CHECK( port.calls[ 0 ].kind == PortCall::Kind::AddSidebarTab );
                CHECK( port.calls[ 0 ].label == "Probe tab" );
                CHECK( port.calls[ 1 ].kind == PortCall::Kind::RemoveSidebarTab );
                for ( const auto& call : port.calls ) {
                    CHECK( call.pluginId == ProbeId );
                    CHECK( call.widget.widget == &sidebarWidget );
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
                CHECK( port.calls[ 0 ].kind == PortCall::Kind::AddFooterWidget );
                CHECK( port.calls[ 1 ].kind == PortCall::Kind::RemoveFooterWidget );
                for ( const auto& call : port.calls ) {
                    CHECK( call.pluginId == ProbeId );
                    CHECK( call.widget.widget == &footerWidget );
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
                CHECK( call.kind == PortCall::Kind::AddMenuAction );
                CHECK( call.pluginId == ProbeId );
                CHECK( call.menuPath == "Plugins" );
                CHECK( call.label == "Probe action" );
                CHECK( call.callback == &menuCallback );
                CHECK( call.userData == &menuUserData );
            }
        }

        WHEN( "The plugin is configured" )
        {
            int parentWidget = 0;
            port.parent = PluginWidgetHandle{ &parentWidget };
            manager.configurePlugin( ProbeId );

            THEN( "The plugin gets the parent the port chose" )
            {
                CHECK( probe.configureParent() == &parentWidget );
            }
        }

        WHEN( "The plugin is unloaded" )
        {
            const auto* shutdownFooter = probe.shutdownFooterWidget();
            manager.unloadPlugin( ProbeId );

            THEN( "The widget it unregisters on shutdown is removed before all its "
                  "contributions are" )
            {
                REQUIRE( port.calls.size() == 2 );
                CHECK( port.calls[ 0 ].kind == PortCall::Kind::RemoveFooterWidget );
                CHECK( port.calls[ 0 ].pluginId == ProbeId );
                CHECK( port.calls[ 0 ].widget.widget == shutdownFooter );
                CHECK( port.calls[ 1 ].kind == PortCall::Kind::RemoveContributions );
                CHECK( port.calls[ 1 ].pluginId == ProbeId );
            }
        }

        manager.unloadAll();
        probe.library.unload();
    }
}
