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

// The plugins load once per application, after the first window shows (#303).

#include "applicationplugins.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <catch2/catch.hpp>

#include <algorithm>
#include <memory>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginWidgetHandle;

namespace {

const auto SlowConverterId = QStringLiteral( "io.github.logsquirl.test.slow-converter" );

std::shared_ptr<Session> newSession()
{
    return std::make_shared<Session>( testSettingsPolicies(),
                                      std::make_shared<LogFormatCatalog>() );
}

/// Writes a plugin.json for the slow converter into its own subdirectory of root.
void installSlowConverter( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "slow-converter" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write(
        QStringLiteral( R"({
        "id": "%1",
        "name": "Slow Converter",
        "version": "1.0.0",
        "type": "converter",
        "library": "%2",
        "api_version": 1
    })" )
            .arg( SlowConverterId, QStringLiteral( LOGSQUIRL_TEST_SLOW_CONVERTER_PATH ) )
            .toUtf8() );
}

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

/// The text of the tab showing a Log File, or an empty string.
QString logFileTabText( const MainWindow& window )
{
    const auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    if ( !tabs ) {
        return {};
    }
    for ( int i = 0; i < tabs->count(); ++i ) {
        if ( tabs->tabText( i ) != QStringLiteral( "Dashboard" ) ) {
            return tabs->tabText( i );
        }
    }
    return {};
}

/// The action of the window's Plugins menu with the given text, or nullptr.
QAction* pluginMenuAction( const MainWindow& window, const QString& text )
{
    for ( const auto* menu : window.findChildren<QMenu*>() ) {
        if ( menu->title() != QStringLiteral( "Plugins" ) ) {
            continue;
        }
        for ( auto* action : menu->actions() ) {
            if ( action->text() == text ) {
                return action;
            }
        }
    }
    return nullptr;
}

/// Whether one of the window's sidebar tabs shows the widget.
bool showsInSidebar( const MainWindow& window, QWidget* widget )
{
    const auto tabWidgets = window.findChildren<QTabWidget*>();
    return std::ranges::any_of(
        tabWidgets, [ widget ]( const QTabWidget* tabs ) { return tabs->indexOf( widget ) >= 0; } );
}

int pluginActionTriggers = 0;

void countPluginAction( void* )
{
    ++pluginActionTriggers;
}

} // namespace

SCENARIO( "The first window is shown before the plugins load", "[ui][plugins][applicationplugins]" )
{
    GIVEN( "Application Plugins whose loading is observed" )
    {
        std::unique_ptr<MainWindow> window;
        int loads = 0;
        bool windowVisibleWhileLoading = false;
        auto plugins = std::make_shared<ApplicationPlugins>( [ & ]( PluginCatalog&, PluginHost& ) {
            ++loads;
            // Shown on screen, not only asked to show: the window system
            // has exposed it, and it has been painted.
            windowVisibleWhileLoading = window && window->isVisible() && window->windowHandle()
                                        && window->windowHandle()->isExposed();
        } );

        WHEN( "a window is built" )
        {
            window
                = std::make_unique<MainWindow>( WindowSession{ newSession(), "Main", 0 }, plugins );
            QTest::qWait( 50 );

            THEN( "no plugin is loaded while it is not shown" )
            {
                REQUIRE( loads == 0 );
            }

            AND_WHEN( "it is shown" )
            {
                window->show();

                THEN( "the plugins load once, with the window already shown" )
                {
                    REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
                    REQUIRE( loads == 1 );
                    REQUIRE( windowVisibleWhileLoading );
                }
            }
        }
    }
}

SCENARIO( "Opening a second window does not load the plugins again",
          "[ui][plugins][applicationplugins]" )
{
    GIVEN( "A shown window whose plugins have loaded" )
    {
        int loads = 0;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ &loads ]( PluginCatalog&, PluginHost& ) { ++loads; } );
        const auto session = newSession();

        auto first = std::make_unique<MainWindow>( WindowSession{ session, "First", 0 }, plugins );
        first->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loads == 1 );

        WHEN( "a second window is built and shown" )
        {
            auto second
                = std::make_unique<MainWindow>( WindowSession{ session, "Second", 1 }, plugins );
            second->show();
            QTest::qWait( 200 );

            THEN( "the plugins are not loaded again" )
            {
                REQUIRE( loads == 1 );
            }
        }
    }
}

SCENARIO( "A Log File opened before the plugins loaded is opened with the converter plugin",
          "[ui][plugins][applicationplugins]" )
{
    GIVEN( "A slow converter plugin for .slowconv files and a file of that kind" )
    {
        const StandardPathsInTestMode testPaths;
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installSlowConverter( pluginRoot.path() );

        QTemporaryDir fileDir;
        REQUIRE( fileDir.isValid() );
        const auto logFilePath = fileDir.filePath( "capture.slowconv" );
        {
            QFile logFile( logFilePath );
            REQUIRE( logFile.open( QIODevice::WriteOnly ) );
            logFile.write( "first line\nsecond line\n" );
        }

        // Slow enough that a file opened right away is asked for well before
        // the plugin is loaded.
        qputenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS", "300" );

        QElapsedTimer sinceStart;
        qint64 shownAfterMs = -1;
        qint64 loadStartedAfterMs = -1;
        qint64 loadFinishedAfterMs = -1;
        QStringList loadErrors;
        bool windowExposedWhileLoading = false;

        std::unique_ptr<MainWindow> window;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& host ) {
                loadStartedAfterMs = sinceStart.elapsed();
                windowExposedWhileLoading
                    = window && window->windowHandle() && window->windowHandle()->isExposed();
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { SlowConverterId } } )
                          .errors;
                loadFinishedAfterMs = sinceStart.elapsed();
            } );

        WHEN( "a window is shown and the file is opened at once, as from the command line" )
        {
            sinceStart.start();
            window
                = std::make_unique<MainWindow>( WindowSession{ newSession(), "Main", 0 }, plugins );
            window->show();
            shownAfterMs = sinceStart.elapsed();
            window->loadInitialFile( logFilePath, false );

            THEN( "the file is opened through the converter once the plugin has loaded" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return !logFileTabText( *window ).isEmpty(); }, 10000 ) );
                REQUIRE( plugins->isLoaded() );
                REQUIRE( loadErrors.isEmpty() );
                // The converter writes the converted Log File to a temporary file named
                // after the original one.
                REQUIRE( logFileTabText( *window ).contains(
                    QStringLiteral( "capture.slowconv.txt" ) ) );

                // Time to first window vs. plugin load time, for comparison
                // runs (#303): the window shows before the slow plugin starts
                // loading, not after it finished.
                WARN( "first window shown after " << shownAfterMs << " ms; plugins loaded from "
                                                  << loadStartedAfterMs << " ms to "
                                                  << loadFinishedAfterMs << " ms" );
                REQUIRE( shownAfterMs <= loadStartedAfterMs );
                REQUIRE( windowExposedWhileLoading );
                REQUIRE( loadFinishedAfterMs - loadStartedAfterMs >= 300 );
            }
        }

        window.reset();
        plugins.reset();
        qunsetenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" );
    }
}

SCENARIO( "What plugins contribute shows in every window", "[ui][plugins][applicationplugins]" )
{
    GIVEN( "Two shown windows and a plugin that contributes a menu action and a sidebar tab" )
    {
        const auto pluginId = QStringLiteral( "com.test.ui-plugin" );
        const auto actionLabel = QStringLiteral( "Run Test Plugin" );
        QPointer<QLabel> sidebarWidget = new QLabel( QStringLiteral( "plugin sidebar" ) );
        pluginActionTriggers = 0;

        // Contributes as a plugin does while it is initialised: through the
        // Plugin UI Port of the host.
        auto plugins
            = std::make_shared<ApplicationPlugins>( [ & ]( PluginCatalog&, PluginHost& host ) {
                  REQUIRE( host.uiPort() );
                  host.uiPort()->addMenuAction( pluginId, QStringLiteral( "Tools" ), actionLabel,
                                                countPluginAction, nullptr );
                  host.uiPort()->addSidebarTab( pluginId, QStringLiteral( "Test Plugin" ),
                                                PluginWidgetHandle{ sidebarWidget.data() } );
              } );
        const auto session = newSession();

        auto first = std::make_unique<MainWindow>( WindowSession{ session, "First", 0 }, plugins );
        first->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        auto second
            = std::make_unique<MainWindow>( WindowSession{ session, "Second", 1 }, plugins );
        second->show();
        QTest::qWait( 50 );

        THEN( "both windows have the plugin's menu action, and it works in both" )
        {
            auto* firstAction = pluginMenuAction( *first, actionLabel );
            auto* secondAction = pluginMenuAction( *second, actionLabel );
            REQUIRE( firstAction );
            REQUIRE( secondAction );
            firstAction->trigger();
            secondAction->trigger();
            REQUIRE( pluginActionTriggers == 2 );
        }

        THEN( "the sidebar tab shows in the first window" )
        {
            REQUIRE( showsInSidebar( *first, sidebarWidget ) );
            REQUIRE_FALSE( showsInSidebar( *second, sidebarWidget ) );
        }

        WHEN( "the first window closes" )
        {
            first->close();
            first.reset();
            QTest::qWait( 50 );

            THEN( "the second window keeps the menu action and shows the sidebar tab" )
            {
                REQUIRE( sidebarWidget );
                REQUIRE( showsInSidebar( *second, sidebarWidget ) );
                auto* action = pluginMenuAction( *second, actionLabel );
                REQUIRE( action );
                action->trigger();
                REQUIRE( pluginActionTriggers == 1 );
            }
        }

        second.reset();
        first.reset();
        plugins.reset();
        delete sidebarWidget.data();
    }
}
