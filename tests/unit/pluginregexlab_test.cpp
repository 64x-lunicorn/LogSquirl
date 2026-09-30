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

// A plugin opens the Regex Lab with a pattern and gets back the pattern the
// user applied, or hears that the user cancelled (#662). The plugin is a real
// library built against the current header, loaded by the Plugin Host, and the
// Lab opens in a main window through its side of the Plugin UI Port.

#include <catch2/catch_test_macros.hpp>

#include "logsquirl_plugin_api.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "pluginuiadapter.h"
#include "regexlabwindow.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

#include <memory>

using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.regex-lab-plugin" );

/// Writes a plugin.json for the Regex Lab plugin into its own subdirectory of root.
void installPlugin( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "regex-lab-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Regex Lab Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_REGEX_LAB_PLUGIN_PATH ) )
                        .toUtf8() );
}

template <typename Widget>
Widget* part( const QWidget& parent, const char* name )
{
    auto* widget = parent.findChild<Widget*>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    return widget;
}

QPushButton* labButton( const RegexLabWindow& lab, QDialogButtonBox::StandardButton which )
{
    auto* button = part<QDialogButtonBox>( lab, "buttons" )->button( which );
    REQUIRE( button != nullptr );
    return button;
}

/// The action of the menu with the given text, or nullptr.
QAction* menuAction( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

/// The extra exports of the plugin the tests reach behind the host's back. The
/// library stays loaded as long as this does, also after the host unloads it.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_REGEX_LAB_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    size_t hostApiSize()
    {
        using Fn = size_t ( * )();
        return reinterpret_cast<Fn>(
            library.resolve( "logsquirl_regex_lab_plugin_host_api_size" ) )();
    }

    int answers()
    {
        using Fn = int ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_regex_lab_plugin_answers" ) )();
    }

    QLibrary library;
};

/// Lets a window closed with Qt::WA_DeleteOnClose go.
void deleteClosedWindows()
{
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
}

} // namespace

SCENARIO( "A plugin opens the Regex Lab and gets the pattern the user applied",
          "[pluginregexlab][regexlab][plugins]" )
{
    GIVEN( "A main window and a plugin built against the current header, loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installPlugin( pluginRoot.path() );
        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        PluginLibrary plugin;

        auto window = std::make_unique<QMainWindow>();
        QMenu pluginsMenu;
        auto* separator = pluginsMenu.addSeparator();
        QTabWidget sidebarTabs;
        auto adapter
            = std::make_unique<PluginUiAdapter>( *window, pluginsMenu, separator, sidebarTabs );

        PluginHost host( catalog );
        host.setUiPort( adapter.get() );
        QStringList notifications;
        QObject::connect(
            &host, &PluginHost::notificationRequested,
            [ &notifications ]( const QString& message ) { notifications.append( message ); } );
        REQUIRE( host.loadPlugin( PluginId ).isEmpty() );

        THEN( "The plugin is told the size of this host's callback table, and so offers to "
              "test its pattern" )
        {
            REQUIRE( plugin.hostApiSize() == sizeof( LogSquirlHostApi ) );
            REQUIRE( menuAction( pluginsMenu, QStringLiteral( "Test Pattern" ) ) != nullptr );
        }

        WHEN( "The plugin opens the Regex Lab with its pattern" )
        {
            auto* testPattern = menuAction( pluginsMenu, QStringLiteral( "Test Pattern" ) );
            REQUIRE( testPattern != nullptr );
            testPattern->trigger();

            QPointer<RegexLabWindow> lab = window->findChild<RegexLabWindow*>();
            REQUIRE( !lab.isNull() );
            const auto answersBefore = plugin.answers();

            THEN( "The Lab shows the pattern as the plugin reads it, and offers Apply" )
            {
                REQUIRE( lab->isVisible() );
                REQUIRE( part<QLineEdit>( *lab, "pattern" )->text() == "ERROR (\\d+)" );
                REQUIRE( part<QCheckBox>( *lab, "matchCase" )->isChecked() );
                REQUIRE( part<QCheckBox>( *lab, "matchCase" )->isEnabled() );
                // Always a regular expression, never inverted or combined.
                REQUIRE( part<QCheckBox>( *lab, "useRegexp" )->isChecked() );
                REQUIRE_FALSE( part<QCheckBox>( *lab, "useRegexp" )->isEnabled() );
                REQUIRE( part<QCheckBox>( *lab, "inverse" )->isHidden() );
                REQUIRE( part<QCheckBox>( *lab, "logicalCombination" )->isHidden() );
                REQUIRE( labButton( *lab, QDialogButtonBox::Apply )->isVisible() );
                REQUIRE( notifications.isEmpty() );
            }

            AND_WHEN( "The user changes the pattern, lets it ignore case and applies it" )
            {
                part<QLineEdit>( *lab, "pattern" )->setText( QStringLiteral( "WARN (\\w+)" ) );
                part<QCheckBox>( *lab, "matchCase" )->click();
                labButton( *lab, QDialogButtonBox::Apply )->click();
                deleteClosedWindows();

                THEN( "The plugin gets that pattern, once, and the Lab closes" )
                {
                    REQUIRE( plugin.answers() == answersBefore + 1 );
                    REQUIRE( notifications
                             == QStringList{ "applied, ignoring case: WARN (\\w+)" } );
                    REQUIRE( lab.isNull() );
                }
            }

            AND_WHEN( "The user cancels" )
            {
                labButton( *lab, QDialogButtonBox::Cancel )->click();
                deleteClosedWindows();

                THEN( "The plugin hears that the user cancelled, once" )
                {
                    REQUIRE( plugin.answers() == answersBefore + 1 );
                    REQUIRE( notifications == QStringList{ "cancelled" } );
                    REQUIRE( lab.isNull() );
                }
            }

            AND_WHEN( "The user closes the Lab" )
            {
                lab->close();
                deleteClosedWindows();

                THEN( "The plugin hears that the user cancelled" )
                {
                    REQUIRE( notifications == QStringList{ "cancelled" } );
                }
            }

            AND_WHEN( "The plugin is unloaded while the Lab is open" )
            {
                host.unloadPlugin( PluginId );
                deleteClosedWindows();

                THEN( "The Lab closes, and nothing calls into the plugin" )
                {
                    REQUIRE( lab.isNull() );
                    REQUIRE( plugin.answers() == answersBefore );
                    REQUIRE( notifications.isEmpty() );
                }
            }

            AND_WHEN( "The window goes while the Lab is open" )
            {
                // As a main window's members go before its children.
                host.setUiPort( nullptr );
                adapter.reset();
                window.reset();

                THEN( "The plugin hears that the user cancelled" )
                {
                    REQUIRE( lab.isNull() );
                    REQUIRE( notifications == QStringList{ "cancelled" } );
                }
            }
        }

        WHEN( "The plugin opens the Regex Lab twice" )
        {
            auto* testPattern = menuAction( pluginsMenu, QStringLiteral( "Test Pattern" ) );
            REQUIRE( testPattern != nullptr );
            testPattern->trigger();
            testPattern->trigger();

            THEN( "Each call has a Lab of its own" )
            {
                REQUIRE( window->findChildren<RegexLabWindow*>().size() == 2 );
            }
        }

        host.unloadAll();
        adapter.reset();
        window.reset();
    }
}
