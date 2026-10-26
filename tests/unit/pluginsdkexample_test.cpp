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

// The example plugin of the plugin developer guide loads in LogSquirl (#598):
// installed the way the guide says, with the guide's own plugin.json, it is
// found by the Plugin Catalog and loaded and initialised by the Plugin Host.

#include <catch2/catch_test_macros.hpp>

#include "plugincatalog.h"
#include "pluginhost.h"
#include "pluginlogfiles.h"
#include "pluginuiport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginUiPort;
using logsquirl::plugins::PluginWidgetHandle;

namespace {

const auto ExampleId = QStringLiteral( "com.example.my-plugin" );

/// A menu action a plugin added.
struct MenuAction {
    QString pluginId;
    QString label;
    PluginCallbackFn callback = nullptr;
    void* userData = nullptr;
};

/// Keeps the menu actions plugins add and ignores their widgets.
class MenuActionPort : public PluginUiPort {
public:
    std::vector<MenuAction> actions;
    QStringList removedContributions;

    void addStatusWidget( const QString&, PluginWidgetHandle ) override {}
    void removeStatusWidget( const QString&, PluginWidgetHandle ) override {}
    void addSidebarTab( const QString&, const QString&, PluginWidgetHandle ) override {}
    void removeSidebarTab( const QString&, PluginWidgetHandle ) override {}
    void addFooterWidget( const QString&, PluginWidgetHandle ) override {}
    void removeFooterWidget( const QString&, PluginWidgetHandle ) override {}

    void addMenuAction( const QString& pluginId, const QString& /* menuPath */,
                        const QString& label, PluginCallbackFn callback, void* userData ) override
    {
        actions.push_back( { pluginId, label, callback, userData } );
    }

    void removeMenuAction( const QString& /* pluginId */, void* userData ) override
    {
        std::erase_if( actions, [ userData ]( const MenuAction& action ) {
            return action.userData == userData;
        } );
    }

    /// The labels of the menu actions shown, in the order added.
    QStringList labels() const
    {
        QStringList shown;
        for ( const auto& action : actions ) {
            shown.append( action.label );
        }
        return shown;
    }

    /// Triggers the menu action with the label, which must be shown.
    void trigger( const QString& label ) const
    {
        const auto it = std::ranges::find( actions, label, &MenuAction::label );
        REQUIRE( it != actions.end() );
        // A copy: the callback may remove the action.
        const auto action = *it;
        action.callback( action.userData );
    }

    /// The context menu items plugins add, with their menu paths (#859).
    struct ContextMenuAction {
        QString menuPath;
        QString label;
        logsquirl::plugins::PluginContextMenuCallbackFn callback = nullptr;
        void* userData = nullptr;
    };
    std::vector<ContextMenuAction> contextMenuActions;

    void addContextMenuAction( const QString& /* pluginId */, const QString& menuPath,
                               const QString& label,
                               logsquirl::plugins::PluginContextMenuCallbackFn callback,
                               void* userData ) override
    {
        contextMenuActions.push_back( { menuPath, label, callback, userData } );
    }

    /// Chooses the context menu item with the label, which must be shown.
    void choose( const QString& label,
                 const logsquirl::plugins::PluginContextMenuContext& context ) const
    {
        const auto it = std::ranges::find( contextMenuActions, label, &ContextMenuAction::label );
        REQUIRE( it != contextMenuActions.end() );
        it->callback( it->userData, context );
    }

    void removeContributions( const QString& pluginId ) override
    {
        removedContributions.append( pluginId );
    }

    PluginWidgetHandle configurationParent() override
    {
        return {};
    }

    /// The Log Lines selected in the tab in front; none without a Log File.
    std::optional<QStringList> selected;
    std::vector<std::uint64_t> wentTo;

    std::optional<logsquirl::plugins::PluginSelectedLogLines>
    selectedLogLines( std::size_t /* maxLines */, std::size_t /* maxBytes */ ) override
    {
        if ( !selected ) {
            return std::nullopt;
        }
        return logsquirl::plugins::PluginSelectedLogLines{ .lines = *selected,
                                                           .lastCut = false,
                                                           .more = false };
    }

    logsquirl::plugins::PluginLogLineJump goToLogLine( std::uint64_t logLine ) override
    {
        wentTo.push_back( logLine );
        return logsquirl::plugins::PluginLogLineJump::Shown;
    }

    /// The Encoding of the tab in front; none without a Log File.
    std::optional<QString> encodingInFront;

    std::optional<QString> logFileEncoding( const QString& logFile ) override
    {
        return logFile.isEmpty() ? encodingInFront : std::nullopt;
    }
};

/// An open Log File of count Log Lines, which reads none.
class CountedLogFile final : public logsquirl::plugins::PluginLogFileSource {
public:
    explicit CountedLogFile( std::uint64_t count )
        : count_( count )
    {
    }

    std::uint64_t lineCount() const override
    {
        return count_;
    }

    std::optional<logsquirl::plugins::PluginSelectedLogLines>
    readLogLines( std::uint64_t, std::size_t, std::size_t ) const override
    {
        return std::nullopt;
    }

private:
    std::uint64_t count_;
};

/// Installs the example the way the guide says: its plugin.json and the built
/// library in a subdirectory of its own under the plugin directory root.
void installExample( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( ExampleId );
    REQUIRE( QDir().mkpath( pluginDir ) );

    const QString exampleDir = QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_DIR );
    REQUIRE( QFile::copy( QDir( exampleDir ).filePath( "plugin.json" ),
                          QDir( pluginDir ).filePath( "plugin.json" ) ) );

    const QFileInfo library( QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_PATH ) );
    REQUIRE( QFile::copy( library.filePath(), QDir( pluginDir ).filePath( library.fileName() ) ) );
}

/// The menu actions the example adds through the table an older host passes.
QStringList olderHostMenuActions;

void olderHostLogMessage( void*, int, const char* ) {}

void olderHostRegisterMenuAction( void*, const char*, const char* label, void ( * )( void* ),
                                  void* )
{
    olderHostMenuActions.append( QString::fromUtf8( label ) );
}

} // namespace

SCENARIO( "The example plugin of the plugin developer guide loads", "[pluginsdk][plugins]" )
{
    GIVEN( "The example plugin installed in a plugin directory" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installExample( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPlugins( QStringList{ pluginRoot.path() } );

        THEN( "The Plugin Catalog finds it" )
        {
            REQUIRE( catalog.findDiscovered( ExampleId ) != nullptr );
        }

        WHEN( "The Plugin Host loads it" )
        {
            MenuActionPort port;
            PluginHost host( catalog );
            host.setUiPort( &port );

            QStringList notifications;
            const auto recordNotification
                = [ &notifications ]( const QString& message ) { notifications.append( message ); };
            QObject::connect( &host, &PluginHost::notificationRequested, recordNotification );

            const auto error = host.loadPlugin( ExampleId );

            THEN( "It is initialised and adds its menu actions, stopping saying hello, testing "
                  "a pattern, going to and reading Log Lines, showing the encoding and counting "
                  "Log Lines as well, since this host offers them" )
            {
                REQUIRE( error.isEmpty() );
                REQUIRE( host.isLoaded( ExampleId ) );
                REQUIRE( port.labels()
                         == QStringList{ "Say Hello", "Stop Saying Hello", "Test Pattern",
                                         "Show Selection", "Go to First Line", "Show Encoding",
                                         "Count Lines" } );
                for ( const auto& action : port.actions ) {
                    REQUIRE( action.pluginId == ExampleId );
                }
            }

            THEN( "Its Show Encoding shows the encoding of the tab in front, or that there is "
                  "no Log File" )
            {
                port.trigger( QStringLiteral( "Show Encoding" ) );
                port.encodingInFront = QStringLiteral( "UTF-16LE" );
                port.trigger( QStringLiteral( "Show Encoding" ) );
                REQUIRE( notifications
                         == QStringList{ "No Log File in front", "Read as UTF-16LE" } );
            }

            THEN( "Its Count Lines counts the Log Lines of the tab in front, or says there is no "
                  "Log File (#858)" )
            {
                QTemporaryDir logDir;
                REQUIRE( logDir.isValid() );
                const auto path = logDir.filePath( "app.log" );
                {
                    QFile file( path );
                    REQUIRE( file.open( QIODevice::WriteOnly ) );
                }
                QString inFront;
                host.setActiveFilePathCallback( [ &inFront ] { return inFront; } );
                port.trigger( QStringLiteral( "Count Lines" ) );
                inFront = path;
                const auto id
                    = host.logFiles()->add( path, std::make_shared<CountedLogFile>( 3000 ) );
                port.trigger( QStringLiteral( "Count Lines" ) );
                host.logFiles()->remove( id );
                REQUIRE( notifications == QStringList{ "No Log File in front", "3000 Log Lines" } );
            }

            THEN( "Its Show Selection shows the selected Log Lines, and nothing without" )
            {
                port.trigger( QStringLiteral( "Show Selection" ) );
                REQUIRE( notifications.isEmpty() );

                port.selected = QStringList{ QStringLiteral( "ERROR 42" ) };
                port.trigger( QStringLiteral( "Show Selection" ) );
                REQUIRE( notifications == QStringList{ "ERROR 42" } );
            }

            THEN( "Its Show Selection shows only the beginning of a long selection, whole "
                  "characters" )
            {
                // "a", then two-byte characters: 200 bytes end inside one.
                port.selected
                    = QStringList{ QStringLiteral( "a" ) + QString( 300, QChar( 0x00e9 ) ) };
                port.trigger( QStringLiteral( "Show Selection" ) );
                REQUIRE( notifications.size() == 1 );
                REQUIRE( notifications.front().toUtf8().size() == 199 );
                REQUIRE( notifications.front() == port.selected->front().left( 100 ) );
            }

            THEN( "Its Go to First Line goes to the first Log Line" )
            {
                port.trigger( QStringLiteral( "Go to First Line" ) );
                REQUIRE( port.wentTo == std::vector<std::uint64_t>{ 0 } );
            }

            THEN( "Its menu action shows a notification" )
            {
                port.trigger( QStringLiteral( "Say Hello" ) );
                REQUIRE( notifications == QStringList{ "Hello from My Plugin" } );
            }

            THEN( "Its Stop Saying Hello removes Say Hello and itself (#857)" )
            {
                const auto actions = port.actions;
                port.trigger( QStringLiteral( "Stop Saying Hello" ) );
                REQUIRE( port.labels()
                         == QStringList{ "Test Pattern", "Show Selection", "Go to First Line",
                                         "Show Encoding", "Count Lines" } );

                // A trigger of either still queued does not reach the plugin.
                for ( const auto& action : actions ) {
                    if ( action.label == "Say Hello" || action.label == "Stop Saying Hello" ) {
                        action.callback( action.userData );
                    }
                }
                REQUIRE( notifications.isEmpty() );
            }

            THEN( "Its Show Line Number, in a submenu of the context menu of the Log Lines, "
                  "shows the number of the first selected Log Line, how many more there are, "
                  "and the selected word (#859)" )
            {
                REQUIRE( port.contextMenuActions.size() == 1 );
                REQUIRE( port.contextMenuActions.front().menuPath
                         == QStringLiteral( "My Plugin" ) );
                using logsquirl::plugins::PluginContextMenuContext;
                port.choose(
                    QStringLiteral( "Show Line Number" ),
                    PluginContextMenuContext{ .logLines = { 41 }, .selectedLogLineCount = 1 } );
                port.choose( QStringLiteral( "Show Line Number" ),
                             PluginContextMenuContext{ .logLines = { 9, 10, 11 },
                                                       .selectedLogLineCount = 3 } );
                port.choose( QStringLiteral( "Show Line Number" ),
                             PluginContextMenuContext{ .logLines = { 2 },
                                                       .selectedLogLineCount = 1,
                                                       .selectedText = QStringLiteral( "id-7" ) } );
                port.choose( QStringLiteral( "Show Line Number" ), PluginContextMenuContext{} );
                REQUIRE( notifications
                         == QStringList{ "Line 42", "Line 10 and 2 more", "Line 3: id-7" } );
            }

            THEN( "It unloads again" )
            {
                host.unloadPlugin( ExampleId );
                REQUIRE_FALSE( host.isLoaded( ExampleId ) );
                REQUIRE( port.removedContributions == QStringList{ ExampleId } );
            }
        }
    }
}

SCENARIO( "The example plugin of the plugin developer guide runs on an older host",
          "[pluginsdk][plugins]" )
{
    GIVEN( "The example plugin's library, and the table of a host that knows no function added "
           "later" )
    {
        QLibrary library( QStringLiteral( LOGSQUIRL_SDK_EXAMPLE_PATH ) );
        REQUIRE( library.load() );
        const auto init = reinterpret_cast<LogSquirlPluginInitFn>(
            library.resolve( LOGSQUIRL_PLUGIN_ENTRY_INIT ) );
        const auto shutdown = reinterpret_cast<LogSquirlPluginShutdownFn>(
            library.resolve( LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN ) );
        REQUIRE( init != nullptr );
        REQUIRE( shutdown != nullptr );

        // An older host calls init, not init_ex, and its table ends where
        // later functions start: exactly that much memory, so that reading
        // past it is caught by the address sanitizer.
        LogSquirlHostApi fullTable{};
        fullTable.api_version = LOGSQUIRL_PLUGIN_API_VERSION;
        fullTable.log_message = &olderHostLogMessage;
        fullTable.register_menu_action = &olderHostRegisterMenuAction;
        const auto olderTable = std::make_unique<unsigned char[]>( LOGSQUIRL_HOST_API_BASE_SIZE );
        std::memcpy( olderTable.get(), &fullTable, LOGSQUIRL_HOST_API_BASE_SIZE );
        const auto* olderHost = reinterpret_cast<const LogSquirlHostApi*>( olderTable.get() );
        olderHostMenuActions.clear();

        WHEN( "The older host initialises it" )
        {
            const auto result = init( olderHost, nullptr );

            THEN( "It adds only the menu action that host can serve" )
            {
                REQUIRE( result == 0 );
                REQUIRE( olderHostMenuActions == QStringList{ "Say Hello" } );
            }
        }

        shutdown();
        library.unload();
    }
}
