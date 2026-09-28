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

#include "plugincatalog.h"
#include "plugindialog.h"
#include "pluginhost.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QPushButton>
#include <QTemporaryDir>

using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

const auto ProbeId = QStringLiteral( "io.github.logsquirl.test.ui-port-probe" );
const auto ExampleId = QStringLiteral( "com.example.my-plugin" );

/// Writes a plugin.json for a library into its own subdirectory of root.
void installPlugin( const QString& root, const QString& id, const QString& library )
{
    const auto pluginDir = QDir( root ).filePath( id );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Plugin %1",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( id, library )
                        .toUtf8() );
}

/// How often the probe plugin was asked for its configuration so far.
int probeConfigureCalls()
{
    QLibrary probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
    REQUIRE( probe.load() );
    using Fn = int ( * )();
    const auto calls = reinterpret_cast<Fn>( probe.resolve( "logsquirl_probe_configure_calls" ) );
    REQUIRE( calls != nullptr );
    return calls();
}

/// The Configure... button of the dialog's only plugin card.
QPushButton* configureButton( const PluginDialog& dialog )
{
    const auto buttons = dialog.findChildren<QPushButton*>( "configureButton" );
    REQUIRE( buttons.size() == 1 );
    return buttons.front();
}

} // namespace

SCENARIO( "PluginDialog footer contains expected buttons", "[plugindialog][plugins]" )
{
    GIVEN( "A freshly constructed PluginDialog" )
    {
        PluginCatalog catalog;
        PluginHost host( catalog );
        PluginDialog dialog( catalog, host );

        THEN( "The dialog contains a 'Plugin Folder' button" )
        {
            const auto buttons = dialog.findChildren<QPushButton*>();
            bool found = false;
            for ( const auto* btn : buttons ) {
                if ( btn->text() == QObject::tr( "Plugin Folder" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }

        THEN( "The dialog contains a 'Close' button" )
        {
            const auto buttons = dialog.findChildren<QPushButton*>();
            bool found = false;
            for ( const auto* btn : buttons ) {
                if ( btn->text() == QObject::tr( "Close" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }

        THEN( "The dialog contains an auto-load checkbox" )
        {
            const auto checkboxes = dialog.findChildren<QCheckBox*>();
            bool found = false;
            for ( const auto* cb : checkboxes ) {
                if ( cb->text().contains( "Auto-load" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }
    }
}

SCENARIO( "Plugin Management opens a plugin's configuration", "[plugindialog][plugins]" )
{
    QTemporaryDir pluginRoot;
    REQUIRE( pluginRoot.isValid() );

    GIVEN( "A loaded plugin that exports the configure function" )
    {
        installPlugin( pluginRoot.path(), ProbeId, QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        PluginDialog dialog( catalog, host );
        auto* button = configureButton( dialog );

        THEN( "Its Configure... button is enabled" )
        {
            REQUIRE( button->isEnabled() );
        }

        WHEN( "The user clicks Configure..." )
        {
            const auto callsBefore = probeConfigureCalls();
            button->click();

            THEN( "The plugin's configure function is called once" )
            {
                REQUIRE( probeConfigureCalls() == callsBefore + 1 );
            }
        }
    }

    GIVEN( "A plugin that exports the configure function but is not loaded" )
    {
        installPlugin( pluginRoot.path(), ProbeId, QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );

        PluginDialog dialog( catalog, host );

        THEN( "Its Configure... button is disabled" )
        {
            REQUIRE_FALSE( configureButton( dialog )->isEnabled() );
        }
    }

    GIVEN( "A loaded plugin without the configure function" )
    {
        installPlugin( pluginRoot.path(), ExampleId, QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_PATH ) );
        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ExampleId ).isEmpty() );

        PluginDialog dialog( catalog, host );

        THEN( "Its Configure... button is disabled" )
        {
            REQUIRE_FALSE( configureButton( dialog )->isEnabled() );
        }
    }
}
