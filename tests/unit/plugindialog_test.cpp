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

#include "applicationplugins.h"
#include "logformatcatalog.h"
#include "plugindialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLibrary>
#include <QPushButton>
#include <QTemporaryDir>

using logsquirl::plugins::ApplicationPlugins;

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
        ApplicationPlugins plugins;
        PluginDialog dialog( plugins );

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
        ApplicationPlugins plugins;
        plugins.catalog().discoverPluginsIn( pluginRoot.path() );
        REQUIRE( plugins.host().loadPlugin( ProbeId ).isEmpty() );

        PluginDialog dialog( plugins );
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
        ApplicationPlugins plugins;
        plugins.catalog().discoverPluginsIn( pluginRoot.path() );

        PluginDialog dialog( plugins );

        THEN( "Its Configure... button is disabled" )
        {
            REQUIRE_FALSE( configureButton( dialog )->isEnabled() );
        }
    }

    GIVEN( "A loaded plugin without the configure function" )
    {
        installPlugin( pluginRoot.path(), ExampleId, QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_PATH ) );
        ApplicationPlugins plugins;
        plugins.catalog().discoverPluginsIn( pluginRoot.path() );
        REQUIRE( plugins.host().loadPlugin( ExampleId ).isEmpty() );

        PluginDialog dialog( plugins );

        THEN( "Its Configure... button is disabled" )
        {
            REQUIRE_FALSE( configureButton( dialog )->isEnabled() );
        }
    }
}

// What the Log Format Catalog could not read of a plugin's Log Format files
// shows in the plugin's details (#793).
SCENARIO( "Plugin Management shows what the Catalog could not read of a plugin's Log Formats",
          "[plugindialog][plugins][logformat]" )
{
    QTemporaryDir pluginRoot;
    REQUIRE( pluginRoot.isValid() );
    installPlugin( pluginRoot.path(), ExampleId, QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_PATH ) );
    ApplicationPlugins plugins;
    plugins.catalog().discoverPluginsIn( pluginRoot.path() );

    // The card's Log Format warning, hidden while there is none.
    const auto formatWarning = []( const PluginDialog& dialog ) {
        // The cards replaced by the last rebuild are gone once their deferred
        // deletion ran.
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        const auto labels = dialog.findChildren<QLabel*>( "formatWarningLabel" );
        REQUIRE( labels.size() == 1 );
        return labels.front();
    };

    GIVEN( "a Catalog that warns about the plugin's Log Format file, and one that does not" )
    {
        const auto missing = QDir( pluginRoot.path() ).filePath( ExampleId + "/missing.json" );
        auto warning = std::make_shared<LogFormatCatalog>();
        warning->setPluginFormats( { { ExampleId, "Plugin " + ExampleId, { missing } } } );
        warning->rebuild();
        REQUIRE( warning->pluginFormatWarnings( ExampleId ).size() == 1 );
        auto silent = std::make_shared<LogFormatCatalog>();
        silent->rebuild();

        WHEN( "the dialog shows no Catalog's warnings" )
        {
            PluginDialog dialog( plugins );

            THEN( "the plugin's card shows no Log Format warning" )
            {
                CHECK_FALSE( formatWarning( dialog )->isVisibleTo( &dialog ) );
            }
        }

        WHEN( "the dialog shows the warning Catalog's" )
        {
            PluginDialog dialog( plugins );
            dialog.showLogFormatWarnings( warning );

            THEN( "the plugin's card names the file that was skipped" )
            {
                const auto* label = formatWarning( dialog );
                CHECK( label->isVisibleTo( &dialog ) );
                CHECK( label->text().contains( "missing.json" ) );
                CHECK( label->text() == warning->pluginFormatWarnings( ExampleId ).join( '\n' ) );
            }
        }

        WHEN( "the dialog shows the silent Catalog's" )
        {
            PluginDialog dialog( plugins );
            dialog.showLogFormatWarnings( silent );

            THEN( "the plugin's card shows no Log Format warning" )
            {
                CHECK_FALSE( formatWarning( dialog )->isVisibleTo( &dialog ) );
            }
        }
    }
}

// A catalog entry whose download could not be verified is listed, but
// cannot be installed, and the card says why (#818).
SCENARIO( "Plugin Management shows a catalog entry without a checksum as not installable",
          "[plugindialog][plugins]" )
{
    ApplicationPlugins plugins;

    const auto catalog = []( const QString& sha256 ) {
        return QStringLiteral( R"({
            "schema_version": 1,
            "plugins": [
                { "id": "io.test.remote", "name": "Remote", "version": "1.0.0",
                  "download_url": "https://files.test/remote.zip", "sha256": "%1",
                  "platforms": ["all"] }
            ]
        })" )
            .arg( sha256 )
            .toUtf8();
    };

    // The one card's Install button and refusal label, once the cards the
    // catalog replaced are gone.
    const auto installButton = []( const PluginDialog& dialog ) {
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        const auto buttons = dialog.findChildren<QPushButton*>( "actionButton" );
        REQUIRE( buttons.size() == 1 );
        return buttons.front();
    };
    const auto refusalLabel = []( const PluginDialog& dialog ) {
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        const auto labels = dialog.findChildren<QLabel*>( "installRefusalLabel" );
        REQUIRE( labels.size() == 1 );
        return labels.front();
    };

    GIVEN( "a catalog whose entry carries a checksum" )
    {
        PluginDialog dialog( plugins );
        dialog.showCatalog( catalog( QString( 64, 'a' ) ) );

        THEN( "the card offers Install and shows no refusal" )
        {
            CHECK( installButton( dialog )->isEnabled() );
            CHECK_FALSE( refusalLabel( dialog )->isVisibleTo( &dialog ) );
        }
    }

    GIVEN( "a catalog whose entry carries no checksum" )
    {
        PluginDialog dialog( plugins );
        dialog.showCatalog( catalog( "" ) );

        THEN( "the card's Install is disabled and the card says the entry carries no checksum" )
        {
            CHECK_FALSE( installButton( dialog )->isEnabled() );
            const auto* label = refusalLabel( dialog );
            CHECK( label->isVisibleTo( &dialog ) );
            CHECK( label->text().contains( "checksum" ) );
            CHECK( installButton( dialog )->toolTip() == label->text() );
        }
    }

    GIVEN( "a catalog whose entry carries a malformed checksum" )
    {
        PluginDialog dialog( plugins );
        dialog.showCatalog( catalog( "abc" ) );

        THEN( "the card's Install is disabled and the card says why" )
        {
            CHECK_FALSE( installButton( dialog )->isEnabled() );
            CHECK( refusalLabel( dialog )->text().contains( "checksum" ) );
        }
    }
}
