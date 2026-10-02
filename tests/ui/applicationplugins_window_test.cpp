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
#include "configuration.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "recentfiles.h"
#include "session.h"
#include "sessioninfo.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "welcomedashboard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QWindow>

#include <catch2/catch_test_macros.hpp>

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

/// The recent files, with the separators of a path the tests build.
QStringList recentFiles()
{
    QStringList files;
    for ( const auto& file : RecentFiles::getSynced().recentFiles() ) {
        files.append( QDir::fromNativeSeparators( file ) );
    }
    return files;
}

void forgetRecentFiles()
{
    auto& recent = RecentFiles::getSynced();
    recent.removeAll();
    recent.save();
}

/// Whether one of the labels of the window's dashboard mentions the text.
bool dashboardMentions( const MainWindow& window, const QString& text )
{
    const auto* dashboard = window.findChild<WelcomeDashboard*>();
    if ( !dashboard ) {
        return false;
    }
    const auto labels = dashboard->findChildren<QLabel*>();
    return std::ranges::any_of(
        labels, [ &text ]( const QLabel* label ) { return label->text().contains( text ); } );
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

// What a converter plugin writes is read from a temporary file that is gone
// after a restart: it is a Transient Log File, so neither the recent files nor
// the Session keep its path. The recent files keep the Log File it was
// converted from, unless that one is Transient too (#605).
SCENARIO( "A converted Log File leaves no temporary path in the recent files or the Session",
          "[ui][plugins][applicationplugins][session]" )
{
    const auto windowId = QStringLiteral( "applicationplugins_window_test_window_605" );

    GIVEN( "A window whose converter plugin has loaded, with an Ordinary and a Transient Log "
           "File converted by it" )
    {
        const StandardPathsInTestMode testPaths;
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installSlowConverter( pluginRoot.path() );
        qunsetenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" );

        QTemporaryDir fileDir;
        REQUIRE( fileDir.isValid() );
        const auto ordinaryPath = fileDir.filePath( "opened.slowconv" );
        const auto transientPath = fileDir.filePath( "streamed.slowconv" );
        for ( const auto& path : { ordinaryPath, transientPath } ) {
            QFile logFile( path );
            REQUIRE( logFile.open( QIODevice::WriteOnly ) );
            logFile.write( "first line\nsecond line\n" );
        }

        forgetRecentFiles();
        {
            auto& stored = SessionInfo::getSynced();
            stored.remove( windowId );
            stored.save();
        }

        QStringList loadErrors;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { SlowConverterId } } )
                          .errors;
            } );
        const auto appSession = newSession();
        auto window
            = std::make_unique<MainWindow>( WindowSession{ appSession, windowId, 0 }, plugins );
        window->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );

        // A file the user opens, and one a data source writes, which is
        // Transient.
        window->loadInitialFile( ordinaryPath, false );
        REQUIRE( QMetaObject::invokeMethod(
            window.get(), "handleDataSourceStarted", Qt::DirectConnection,
            Q_ARG( QString, QStringLiteral( "io.github.logsquirl.test.stream" ) ),
            Q_ARG( QString, QStringLiteral( "Stream" ) ), Q_ARG( QString, transientPath ) ) );
        REQUIRE( waitUiState( [ & ] { return tabs->logFileTabs().size() == 2; }, 10000 ) );

        // The tab of the Log File the user opened, named after what the
        // converter wrote.
        const auto openedTab = [ & ] {
            for ( const auto i : tabs->logFileTabs() ) {
                if ( tabs->tabText( i ).contains( QStringLiteral( "opened.slowconv.txt" ) ) ) {
                    return i;
                }
            }
            return -1;
        };

        THEN( "both are opened from what the converter wrote, as Transient Log Files" )
        {
            REQUIRE( openedTab() >= 0 );
            for ( const auto i : tabs->logFileTabs() ) {
                REQUIRE( tabs->holdsTransientLogFile( i ) );
            }
        }

        THEN( "the recent files keep the Ordinary Log File converted, and no temporary path" )
        {
            REQUIRE( recentFiles() == QStringList{ QDir::fromNativeSeparators( ordinaryPath ) } );
        }

        WHEN( "the user closes the tab of the Ordinary Log File converted" )
        {
            forgetRecentFiles();
            REQUIRE( openedTab() >= 0 );
            Q_EMIT tabs->tabCloseRequested( openedTab() );

            THEN( "the Log File it was converted from becomes a recent file" )
            {
                REQUIRE( waitUiState( [ & ] { return tabs->logFileTabs().size() == 1; }, 5000 ) );
                REQUIRE( recentFiles()
                         == QStringList{ QDir::fromNativeSeparators( ordinaryPath ) } );
            }
        }

        WHEN( "the application quits, which saves the Session" )
        {
            appSession->setExitRequested( true );
            window->close();
            appSession->setExitRequested( false );

            THEN( "the Session saves neither of them" )
            {
                REQUIRE( SessionInfo::getSynced().openFiles( windowId ).empty() );
            }
        }

        window.reset();
        plugins.reset();
        forgetRecentFiles();
        auto& left = SessionInfo::getSynced();
        left.remove( windowId );
        left.save();
    }
}

// A converted Log File is open by the path the converter wrote it to, not by
// the one asked for: opening that one again shows its tab rather than
// converting it anew (#615).
SCENARIO( "Opening a converted Log File again shows its open tab",
          "[ui][plugins][applicationplugins]" )
{
    GIVEN( "A window whose converter plugin has loaded, with two Log Files converted by it" )
    {
        const StandardPathsInTestMode testPaths;
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installSlowConverter( pluginRoot.path() );
        qunsetenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" );

        QTemporaryDir fileDir;
        REQUIRE( fileDir.isValid() );
        const auto firstPath = fileDir.filePath( "first.slowconv" );
        const auto secondPath = fileDir.filePath( "second.slowconv" );
        for ( const auto& path : { firstPath, secondPath } ) {
            QFile logFile( path );
            REQUIRE( logFile.open( QIODevice::WriteOnly ) );
            logFile.write( "first line\nsecond line\n" );
        }

        QStringList loadErrors;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { SlowConverterId } } )
                          .errors;
            } );
        auto window = std::make_unique<MainWindow>(
            WindowSession{ newSession(), QStringLiteral( "applicationplugins_window_test_615" ),
                           0 },
            plugins );
        window->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );

        window->loadInitialFile( firstPath, false );
        window->loadInitialFile( secondPath, false );
        REQUIRE( waitUiState( [ & ] { return tabs->logFileTabs().size() == 2; }, 10000 ) );

        // Each conversion writes a temporary file of the window's, named
        // after the Log File converted.
        const auto conversionsOfFirst = [ & ] {
            const auto files = window->findChildren<QTemporaryFile*>();
            return std::ranges::count_if( files, []( const QTemporaryFile* file ) {
                return QFileInfo( file->fileName() )
                    .fileName()
                    .startsWith( QStringLiteral( "first.slowconv.txt" ) );
            } );
        };
        const auto currentTabText = [ & ] { return tabs->tabText( tabs->currentIndex() ); };
        REQUIRE( conversionsOfFirst() == 1 );
        REQUIRE( currentTabText().contains( QStringLiteral( "second.slowconv.txt" ) ) );

        WHEN( "the first one is opened again" )
        {
            window->loadInitialFile( firstPath, false );

            THEN( "its tab becomes current, without converting it again or opening another" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return currentTabText().contains( QStringLiteral( "first.slowconv.txt" ) );
                    },
                    5000 ) );
                REQUIRE( tabs->logFileTabs().size() == 2 );
                REQUIRE( conversionsOfFirst() == 1 );
            }
        }

        WHEN( "the tab of the first one is closed, and it is opened again" )
        {
            const auto firstTab = [ & ] {
                for ( const auto i : tabs->logFileTabs() ) {
                    if ( tabs->tabText( i ).contains( QStringLiteral( "first.slowconv.txt" ) ) ) {
                        return i;
                    }
                }
                return -1;
            };
            REQUIRE( firstTab() >= 0 );
            Q_EMIT tabs->tabCloseRequested( firstTab() );
            REQUIRE( waitUiState( [ & ] { return tabs->logFileTabs().size() == 1; }, 5000 ) );

            window->loadInitialFile( firstPath, false );

            THEN( "it is converted anew into a tab of its own" )
            {
                REQUIRE( waitUiState( [ & ] { return tabs->logFileTabs().size() == 2; }, 5000 ) );
                REQUIRE( conversionsOfFirst() == 2 );
                REQUIRE( currentTabText().contains( QStringLiteral( "first.slowconv.txt" ) ) );
            }
        }

        window.reset();
        plugins.reset();
    }
}

// A Log File open in another window is shown there rather than opened again:
// its tab in that window comes to the front, whether it is open by its own
// path or by the path a converter plugin wrote it to (#642).
SCENARIO( "Opening a Log File open in another window shows its tab there",
          "[ui][plugins][applicationplugins]" )
{
    GIVEN( "Two windows whose converter plugin has loaded; window B shows a converted Log File "
           "and an Ordinary one, with a third Ordinary one in front" )
    {
        const StandardPathsInTestMode testPaths;
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installSlowConverter( pluginRoot.path() );
        qunsetenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" );

        QTemporaryDir fileDir;
        REQUIRE( fileDir.isValid() );
        const auto convertedPath = fileDir.filePath( "converted.slowconv" );
        const auto plainPath = fileDir.filePath( "plain.log" );
        const auto frontPath = fileDir.filePath( "front.log" );
        for ( const auto& path : { convertedPath, plainPath, frontPath } ) {
            QFile logFile( path );
            REQUIRE( logFile.open( QIODevice::WriteOnly ) );
            logFile.write( "first line\nsecond line\n" );
        }

        QStringList loadErrors;
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { SlowConverterId } } )
                          .errors;
            } );
        const auto appSession = newSession();
        auto windowA = std::make_unique<MainWindow>(
            WindowSession{ appSession, QStringLiteral( "applicationplugins_window_test_642_a" ),
                           0 },
            plugins );
        auto windowB = std::make_unique<MainWindow>(
            WindowSession{ appSession, QStringLiteral( "applicationplugins_window_test_642_b" ),
                           1 },
            plugins );
        windowA->show();
        windowB->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        auto* tabsA = windowA->findChild<TabbedCrawlerWidget*>();
        auto* tabsB = windowB->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabsA != nullptr );
        REQUIRE( tabsB != nullptr );

        windowB->loadInitialFile( convertedPath, false );
        windowB->loadInitialFile( plainPath, false );
        windowB->loadInitialFile( frontPath, false );
        REQUIRE( waitUiState( [ & ] { return tabsB->logFileTabs().size() == 3; }, 10000 ) );

        const auto currentTabTextB = [ & ] { return tabsB->tabText( tabsB->currentIndex() ); };
        // Every conversion writes a temporary file of the window that converts.
        const auto conversions = []( const MainWindow& window ) {
            const auto files = window.findChildren<QTemporaryFile*>();
            return std::ranges::count_if( files, []( const QTemporaryFile* file ) {
                return QFileInfo( file->fileName() )
                    .fileName()
                    .startsWith( QStringLiteral( "converted.slowconv.txt" ) );
            } );
        };
        REQUIRE( currentTabTextB() == QStringLiteral( "front.log" ) );
        REQUIRE( conversions( *windowB ) == 1 );
        REQUIRE( tabsA->logFileTabs().isEmpty() );

        WHEN( "the user opens in window A the Log File the converted one was converted from" )
        {
            windowA->loadInitialFile( convertedPath, false );

            THEN( "window B's tab of it comes to the front, and no tab is opened in window A" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return currentTabTextB().contains(
                            QStringLiteral( "converted.slowconv.txt" ) );
                    },
                    5000 ) );
                QTest::qWait( 100 );
                REQUIRE( tabsA->logFileTabs().isEmpty() );
                REQUIRE( tabsB->logFileTabs().size() == 3 );
                REQUIRE( conversions( *windowA ) == 0 );
                REQUIRE( conversions( *windowB ) == 1 );
            }
        }

        WHEN( "the user opens in window A the Ordinary Log File open in window B" )
        {
            windowA->loadInitialFile( plainPath, false );

            THEN( "window B's tab of it comes to the front, and no tab is opened in window A" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return currentTabTextB() == QStringLiteral( "plain.log" ); }, 5000 ) );
                QTest::qWait( 100 );
                REQUIRE( tabsA->logFileTabs().isEmpty() );
                REQUIRE( tabsB->logFileTabs().size() == 3 );
            }
        }

        windowA.reset();
        windowB.reset();
        plugins.reset();
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
                  host.uiPort()->addMenuAction( pluginId, QString(), actionLabel, countPluginAction,
                                                nullptr );
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

// A plugin installed while the dashboard is shown, from the Plugins dialog of
// this window or of another, is listed on the dashboard at once, not only
// once the dashboard is shown again (#710).
SCENARIO( "The dashboard lists a plugin installed while it is shown",
          "[ui][plugins][applicationplugins][dashboard]" )
{
    GIVEN( "A window that shows the dashboard, with no plugin installed" )
    {
        const StandardPathsInTestMode testPaths;
        qunsetenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" );
        auto& config = Configuration::get();
        const auto previousShowDashboard = config.showDashboard();
        config.setShowDashboard( true );

        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        auto plugins = std::make_shared<ApplicationPlugins>(
            [ & ]( PluginCatalog& catalog, PluginHost& ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
            } );
        auto window = std::make_unique<MainWindow>(
            WindowSession{ newSession(), QStringLiteral( "applicationplugins_window_test_dash" ),
                           0 },
            plugins );
        window->show();
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( dashboardMentions( *window, QStringLiteral( "No plugins installed" ) ) );

        WHEN( "a plugin is installed and loaded, as the Plugins dialog does" )
        {
            installSlowConverter( pluginRoot.path() );
            plugins->catalog().discoverPlugins( { pluginRoot.path() } );
            REQUIRE( plugins->host().loadPlugin( SlowConverterId ).isEmpty() );

            THEN( "the dashboard lists it" )
            {
                REQUIRE( dashboardMentions( *window, QStringLiteral( "Slow Converter" ) ) );
                // The rows it replaced go once the event loop runs.
                REQUIRE( waitUiState(
                    [ & ] {
                        return !dashboardMentions( *window,
                                                   QStringLiteral( "No plugins installed" ) );
                    },
                    5000 ) );
            }
        }

        window.reset();
        plugins.reset();
        config.setShowDashboard( previousShowDashboard );
        config.save();
    }
}
