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

#include "configuration.h"
#include "configurationfixture.h"
#include "convert_probe.h"
#include "logsquirl_plugin_api.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "pluginlogfiles.h"
#include "pluginuiport.h"
#include "test_utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QLibrary>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using logsquirl::plugins::ConversionEnd;
using logsquirl::plugins::ConversionOutcome;
using logsquirl::plugins::ConversionReport;
using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginContextMenuCallbackFn;
using logsquirl::plugins::PluginContextMenuContext;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginLogFileSource;
using logsquirl::plugins::PluginLogFormatFields;
using logsquirl::plugins::PluginLogLineFields;
using logsquirl::plugins::PluginLogLineFieldsUnread;
using logsquirl::plugins::PluginLogLineJump;
using logsquirl::plugins::PluginLogLinesChange;
using logsquirl::plugins::PluginLogView;
using logsquirl::plugins::PluginPattern;
using logsquirl::plugins::PluginRegexLabAnswer;
using logsquirl::plugins::PluginSelectedLogLines;
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
        RemoveMenuAction,
        AddContextMenuAction,
        RemoveContextMenuAction,
        RemoveContributions,
    };

    Kind kind = Kind::AddStatusWidget;
    QString pluginId{};
    QString menuPath{};
    QString label{};
    PluginWidgetHandle widget{};
    PluginCallbackFn callback = nullptr;
    void* userData = nullptr;
    PluginContextMenuCallbackFn contextMenuCallback = nullptr;
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

    void removeMenuAction( const QString& pluginId, void* userData ) override
    {
        calls.push_back( { .kind = PortCall::Kind::RemoveMenuAction,
                           .pluginId = pluginId,
                           .userData = userData } );
    }

    void addContextMenuAction( const QString& pluginId, const QString& menuPath,
                               const QString& label, PluginContextMenuCallbackFn callback,
                               void* userData ) override
    {
        calls.push_back( { .kind = PortCall::Kind::AddContextMenuAction,
                           .pluginId = pluginId,
                           .menuPath = menuPath,
                           .label = label,
                           .userData = userData,
                           .contextMenuCallback = callback } );
    }

    void removeContextMenuAction( const QString& pluginId, void* userData ) override
    {
        calls.push_back( { .kind = PortCall::Kind::RemoveContextMenuAction,
                           .pluginId = pluginId,
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

    /// A Regex Lab a plugin opened, and what answers it.
    struct OpenedLab {
        QString pluginId;
        PluginPattern pattern;
        QPointer<QObject> context;
        PluginRegexLabAnswer answer;
    };
    std::vector<OpenedLab> labs;
    bool opensLabs = true;

    bool openRegexLab( const QString& pluginId, const PluginPattern& pattern, QObject* context,
                       PluginRegexLabAnswer answer ) override
    {
        if ( !opensLabs ) {
            return false;
        }
        labs.push_back( { pluginId, pattern, context, std::move( answer ) } );
        return true;
    }

    /// The Log Lines (from 0) a plugin went to, and how it ends.
    std::vector<std::uint64_t> wentTo;
    PluginLogLineJump jump = PluginLogLineJump::Shown;

    PluginLogLineJump goToLogLine( std::uint64_t logLine ) override
    {
        wentTo.push_back( logLine );
        return jump;
    }

    /// The selected Log Lines of the tab in front; none without a Log File.
    std::optional<QStringList> selected;
    /// Whether the port cut the last Log Line, or left some unread, for the bytes.
    bool lastCut = false;
    bool more = false;
    /// How many Log Lines, and bytes, a plugin asked for at most, at each call.
    std::vector<std::pair<std::size_t, std::size_t>> askedForAtMost;

    std::optional<PluginSelectedLogLines> selectedLogLines( std::size_t maxLines,
                                                            std::size_t maxBytes ) override
    {
        askedForAtMost.emplace_back( maxLines, maxBytes );
        if ( !selected ) {
            return std::nullopt;
        }
        return PluginSelectedLogLines{ .lines
                                       = selected->mid( 0, static_cast<qsizetype>( maxLines ) ),
                                       .lastCut = lastCut,
                                       .more = more };
    }

    /// The Encoding of the tab in front, and of each Log File a tab shows;
    /// the Log Files a plugin asked about, "" for the tab in front.
    std::optional<QString> encodingInFront;
    std::map<QString, QString> encodingOf;
    QStringList askedForEncodingOf;

    std::optional<QString> logFileEncoding( const QString& logFile ) override
    {
        askedForEncodingOf.append( logFile );
        if ( logFile.isEmpty() ) {
            return encodingInFront;
        }
        const auto tab = encodingOf.find( logFile );
        return tab != encodingOf.end() ? std::optional( tab->second ) : std::nullopt;
    }

    /// A change of Marks a plugin had carried out (#866), and on which thread.
    struct MarksChange {
        bool added = true;
        QString logFile{};
        std::vector<std::uint64_t> logLines{};
        QThread* on = nullptr;
    };
    std::vector<MarksChange> marksChanges;
    /// The Marks of each Log File a tab shows; the Log Files a plugin asked about.
    std::map<QString, std::vector<std::uint64_t>> marksOf;
    QStringList askedForMarksOf;

    void addMarks( const QString& logFile, const std::vector<std::uint64_t>& logLines ) override
    {
        marksChanges.push_back( { true, logFile, logLines, QThread::currentThread() } );
    }

    void removeMarks( const QString& logFile, const std::vector<std::uint64_t>& logLines ) override
    {
        marksChanges.push_back( { false, logFile, logLines, QThread::currentThread() } );
    }

    std::optional<std::vector<std::uint64_t>> marks( const QString& logFile ) override
    {
        askedForMarksOf.append( logFile );
        const auto tab = marksOf.find( logFile );
        return tab != marksOf.end() ? std::optional( tab->second ) : std::nullopt;
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

    size_t hostApiSize()
    {
        using Fn = size_t ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_host_api_size" ) )();
    }

    std::vector<size_t> firstMemberOffsets()
    {
        using Fn = const size_t* ( * )( size_t* );
        size_t count = 0;
        const auto* offsets = reinterpret_cast<Fn>(
            library.resolve( "logsquirl_probe_first_member_offsets" ) )( &count );
        return std::vector<size_t>( offsets, offsets + count );
    }

    /// Offset and size of each member of both tables as 26.12 published them (#863).
    std::vector<size_t> members2612()
    {
        using Fn = const size_t* ( * )( size_t* );
        size_t count = 0;
        const auto* members
            = reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_members_2612" ) )( &count );
        return std::vector<size_t>( members, members + count );
    }

    size_t convertControlSize()
    {
        using Fn = size_t ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_probe_convert_control_size" ) )();
    }

    /// What the probe's call of get_log_file_encoding returned, and the name.
    std::pair<int, QString> logFileEncoding( const char* filePath )
    {
        using Fn = int ( * )( const char*, const char** );
        const char* encoding = nullptr;
        const auto result = reinterpret_cast<Fn>(
            library.resolve( "logsquirl_probe_log_file_encoding" ) )( filePath, &encoding );
        return { result, encoding ? QString::fromUtf8( encoding ) : QString() };
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
void installProbe( const QString& root,
                   const QString& library = QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) )
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
                        .arg( ProbeId, library )
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

void menuCallback( void* userData )
{
    ++*static_cast<int*>( userData );
}

/// What a menu action that unloads its own plugin while it runs saw (#691).
struct UnloadingMenuAction {
    PluginHost* host = nullptr;
    const std::vector<PortCall>* portCalls = nullptr;
    bool loadedAgain = false;
    std::optional<size_t> portCallsWhileRunning{};
    std::optional<bool> loadedWhileRunning{};
};

// Stands for a plugin's menu action whose modal dialog the user leaves open
// to disable the plugin: the unload comes from below the running call.
void unloadingMenuCallback( void* userData )
{
    auto* action = static_cast<UnloadingMenuAction*>( userData );
    action->host->unloadPlugin( ProbeId );
    if ( action->loadedAgain ) {
        REQUIRE( action->host->loadPlugin( ProbeId ).isEmpty() );
    }
    action->portCallsWhileRunning = action->portCalls->size();
    action->loadedWhileRunning = action->host->isLoaded( ProbeId );
}

/// What the active-file callback a test registers through the probe received.
struct ActiveFileCalls {
    QStringList paths;
};

void recordActiveFile( void* userData, const char* filePath )
{
    static_cast<ActiveFileCalls*>( userData )->paths.append( QString::fromUtf8( filePath ) );
}

/// The user data of a callback a plugin registers and removes from a thread
/// of its own while notifications come on the UI thread (#869). Each callback
/// is only ever registered with the user data of its own tag: called with
/// the other one, the host mixed an old pair with a new one. Counted on the
/// UI thread only.
struct SwappedCallback {
    int tag = 0;
    int calls = 0;
    int mixed = 0;

    void count( int expectedTag )
    {
        ++calls;
        if ( tag != expectedTag ) {
            ++mixed;
        }
    }
};

void firstActiveFile( void* userData, const char* /*filePath*/ )
{
    static_cast<SwappedCallback*>( userData )->count( 1 );
}

void secondActiveFile( void* userData, const char* /*filePath*/ )
{
    static_cast<SwappedCallback*>( userData )->count( 2 );
}

void firstEncoding( void* userData, const char* /*filePath*/, const char* /*encoding*/ )
{
    static_cast<SwappedCallback*>( userData )->count( 1 );
}

void secondEncoding( void* userData, const char* /*filePath*/, const char* /*encoding*/ )
{
    static_cast<SwappedCallback*>( userData )->count( 2 );
}

/// How often the plugin's thread swaps its callback while the UI thread notifies.
constexpr int CallbackSwaps = 2000;

/// What the Regex Lab callback a test passes through the probe received.
struct RegexLabAnswers {
    struct Answer {
        int result = -1;
        std::optional<QString> pattern;
        int flags = -1;
    };
    std::vector<Answer> answers;
};

void recordRegexLabAnswer( void* userData, int result, const char* pattern, int flags )
{
    static_cast<RegexLabAnswers*>( userData )
        ->answers.push_back(
            { result,
              pattern ? std::optional<QString>( QString::fromUtf8( pattern ) ) : std::nullopt,
              flags } );
}

/// Runs what a test hands it as a call into the plugin: a menu action of the
/// plugin, triggered.
void runInPlugin( void* userData )
{
    ( *static_cast<std::function<void()>*>( userData ) )();
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

            THEN( "The port adds the action with its path and label, and triggering it runs "
                  "the plugin's callback with its user data" )
            {
                REQUIRE( port.calls.size() == 1 );
                const auto& call = port.calls[ 0 ];
                REQUIRE( call.kind == PortCall::Kind::AddMenuAction );
                REQUIRE( call.pluginId == ProbeId );
                REQUIRE( call.menuPath == "Plugins" );
                REQUIRE( call.label == "Probe action" );
                REQUIRE( call.callback != nullptr );

                call.callback( call.userData );
                REQUIRE( menuUserData == 1 );
            }
        }

        WHEN( "The plugin registers a menu action without a callback" )
        {
            api->register_menu_action( handle, "Plugins", "Probe action", nullptr, &menuUserData );

            THEN( "The port adds the action without one" )
            {
                REQUIRE( port.calls.size() == 1 );
                REQUIRE( port.calls[ 0 ].callback == nullptr );
            }
        }

        WHEN( "The plugin's menu action unloads the plugin while it runs" )
        {
            UnloadingMenuAction action{ .host = &host, .portCalls = &port.calls };
            api->register_menu_action( handle, "Plugins", "Probe action", &unloadingMenuCallback,
                                       &action );
            const auto added = port.calls.back();
            const auto callsBefore = port.calls.size();

            added.callback( added.userData );

            THEN( "The plugin counts as unloaded at once, but is shut down only after its "
                  "call has returned, from the event loop" )
            {
                REQUIRE( action.loadedWhileRunning == false );
                REQUIRE( action.portCallsWhileRunning == callsBefore );
                REQUIRE( port.calls.size() == callsBefore );
                REQUIRE_FALSE( host.isLoaded( ProbeId ) );
                REQUIRE( host.loadedPluginIds().isEmpty() );

                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                REQUIRE( port.calls.size() == callsBefore + 2 );
                REQUIRE( port.calls[ callsBefore ].kind == PortCall::Kind::RemoveFooterWidget );
                REQUIRE( port.calls[ callsBefore + 1 ].kind
                         == PortCall::Kind::RemoveContributions );
            }

            AND_THEN( "Triggering the action again meanwhile does not call into the plugin" )
            {
                action.portCallsWhileRunning.reset();
                added.callback( added.userData );
                REQUIRE_FALSE( action.portCallsWhileRunning.has_value() );
            }
        }

        WHEN( "The plugin's menu action unloads the plugin and it is loaded again before the "
              "call returns" )
        {
            UnloadingMenuAction action{ .host = &host,
                                        .portCalls = &port.calls,
                                        .loadedAgain = true };
            api->register_menu_action( handle, "Plugins", "Probe action", &unloadingMenuCallback,
                                       &action );
            const auto added = port.calls.back();
            const auto callsBefore = port.calls.size();

            added.callback( added.userData );
            QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

            THEN( "The plugin stays loaded and is never shut down" )
            {
                REQUIRE( action.loadedWhileRunning == true );
                REQUIRE( host.isLoaded( ProbeId ) );
                REQUIRE( port.calls.size() == callsBefore );
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

        WHEN( "The plugin registers a second active-file callback, and the active file "
              "changes (#857)" )
        {
            ActiveFileCalls secondFiles;
            api->register_active_file_callback( handle, &recordActiveFile, &activeFiles );
            api->register_active_file_callback( handle, &recordActiveFile, &secondFiles );
            host.notifyActiveFileChanged( QStringLiteral( "/logs/app.log" ) );

            THEN( "Only the second one is called: it replaced the first" )
            {
                REQUIRE( activeFiles.paths.isEmpty() );
                REQUIRE( secondFiles.paths == QStringList{ "/logs/app.log" } );
            }

            AND_WHEN( "It registers a NULL callback" )
            {
                api->register_active_file_callback( handle, nullptr, &secondFiles );
                host.notifyActiveFileChanged( QStringLiteral( "/logs/other.log" ) );

                THEN( "No callback is called any more" )
                {
                    REQUIRE( activeFiles.paths.isEmpty() );
                    REQUIRE( secondFiles.paths == QStringList{ "/logs/app.log" } );
                }
            }
        }

        WHEN( "The plugin registers and removes active-file callbacks from a thread of its own "
              "while the active file changes (#869)" )
        {
            SwappedCallback first{ .tag = 1 };
            SwappedCallback second{ .tag = 2 };
            std::atomic<bool> swapping = true;
            std::thread plugin( [ & ] {
                for ( int i = 0; i < CallbackSwaps; ++i ) {
                    api->register_active_file_callback( handle, &firstActiveFile, &first );
                    api->register_active_file_callback( handle, &secondActiveFile, &second );
                    api->register_active_file_callback( handle, nullptr, nullptr );
                }
                swapping = false;
            } );
            while ( swapping ) {
                host.notifyActiveFileChanged( QStringLiteral( "/logs/app.log" ) );
            }
            plugin.join();
            const auto callsWhileSwapping = first.calls + second.calls;
            host.notifyActiveFileChanged( QStringLiteral( "/logs/app.log" ) );

            THEN( "Each call has its own callback's user data, and none comes once removed" )
            {
                REQUIRE( first.mixed == 0 );
                REQUIRE( second.mixed == 0 );
                REQUIRE( first.calls + second.calls == callsWhileSwapping );
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

        WHEN( "The plugin asks for the active file path off the UI thread" )
        {
            int asked = 0;
            host.setActiveFilePathCallback( [ & ] {
                ++asked;
                return QStringLiteral( "/logs/current.log" );
            } );
            const char* path = nullptr;
            std::thread( [ & ] { path = api->get_active_file_path( handle ); } ).join();

            THEN( "It gets an empty string, as without a file, and the host's callback is "
                  "not asked" )
            {
                REQUIRE( path != nullptr );
                REQUIRE( QString::fromUtf8( path ).isEmpty() );
                REQUIRE( asked == 0 );
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

        WHEN( "The plugin asks the host to open a file off the UI thread" )
        {
            // Written on the thread the callback runs on, read after the
            // join or after the event loop turned: never at the same time.
            int opened = 0;
            QThread* openedOn = nullptr;
            QString openedPath;
            bool openedFollowing = false;
            host.setOpenFileCallback( [ & ]( const QString& path, bool follow ) {
                ++opened;
                openedOn = QThread::currentThread();
                openedPath = path;
                openedFollowing = follow;
            } );
            std::thread( [ & ] { api->open_file( handle, "/logs/opened.log", 1 ); } ).join();

            THEN( "The host's open-file callback runs on the host's thread once the event "
                  "loop turns, with the path and the follow flag" )
            {
                REQUIRE( opened == 0 );

                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                REQUIRE( opened == 1 );
                REQUIRE( openedOn == host.thread() );
                REQUIRE( openedPath == "/logs/opened.log" );
                REQUIRE( openedFollowing );
            }

            AND_WHEN( "The plugin is unloaded before the event loop turns" )
            {
                host.unloadPlugin( ProbeId );
                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                THEN( "The callback never runs" )
                {
                    REQUIRE( opened == 0 );
                }
            }

            AND_WHEN( "The plugin is unloaded and loaded again before the event loop turns" )
            {
                host.unloadPlugin( ProbeId );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                THEN( "The callback never runs: the call was the unloaded plugin's" )
                {
                    REQUIRE( opened == 0 );
                }
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

namespace {

/// The id of the probe built against the 26.12 header, installed beside the current one.
const auto OtherProbeId = QStringLiteral( "io.github.logsquirl.test.ui-port-probe-2612" );

/// Writes a plugin.json for a probe library under the id, into its own subdirectory of root.
void installProbeAs( const QString& root, const QString& id, const QString& library )
{
    const auto pluginDir = QDir( root ).filePath( id );
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
                        .arg( id, library )
                        .toUtf8() );
}

} // namespace

SCENARIO( "The active file path a plugin got stays intact when another plugin asks",
          "[pluginhost][plugins]" )
{
    GIVEN( "Two plugins loaded, each a library of its own" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );
        installProbeAs( pluginRoot.path(), OtherProbeId,
                        QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
        REQUIRE( host.loadPlugin( OtherProbeId ).isEmpty() );

        Probe first( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        Probe second( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );
        REQUIRE( first.library.load() );
        REQUIRE( second.library.load() );
        REQUIRE( first.hostHandle() != second.hostHandle() );

        auto activeFile = QStringLiteral( "/logs/first.log" );
        host.setActiveFilePathCallback( [ & ] { return activeFile; } );

        WHEN( "The first plugin asks for the active file path, the file changes and the second "
              "plugin asks (#857)" )
        {
            const char* firstPath = first.hostApi()->get_active_file_path( first.hostHandle() );
            activeFile = QStringLiteral( "/logs/a much longer path than the first one.log" );
            const char* secondPath = second.hostApi()->get_active_file_path( second.hostHandle() );

            THEN( "Each plugin's string holds the path it was given" )
            {
                REQUIRE( QString::fromUtf8( firstPath ) == "/logs/first.log" );
                REQUIRE( QString::fromUtf8( secondPath )
                         == "/logs/a much longer path than the first one.log" );
            }

            AND_WHEN( "The first plugin asks again" )
            {
                const char* again = first.hostApi()->get_active_file_path( first.hostHandle() );

                THEN( "It gets the new path, and the second plugin's string is intact" )
                {
                    REQUIRE( QString::fromUtf8( again )
                             == "/logs/a much longer path than the first one.log" );
                    REQUIRE( QString::fromUtf8( secondPath )
                             == "/logs/a much longer path than the first one.log" );
                }
            }
        }

        host.unloadAll();
        second.library.unload();
        first.library.unload();
    }
}

namespace {

/// What a menu action registered with an id saw of the host when it ran.
struct SelfRemovingMenuAction {
    const LogSquirlHostApi* api = nullptr;
    void* handle = nullptr;
    int id = 0;
    int runs = 0;
};

void removeOwnMenuAction( void* userData )
{
    auto* action = static_cast<SelfRemovingMenuAction*>( userData );
    ++action->runs;
    action->api->unregister_menu_action( action->handle, action->id );
}

} // namespace

SCENARIO( "A plugin removes a menu item it registered with an id", "[pluginhost][plugins]" )
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
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), unregister_menu_action ) );

        int staysRuns = 0;
        int goesRuns = 0;

        WHEN( "The plugin registers two menu items with register_menu_action_ex (#857)" )
        {
            const auto stays = api->register_menu_action_ex( handle, "Plugins", "Stays",
                                                             &menuCallback, &staysRuns );
            const auto goes = api->register_menu_action_ex( handle, "Probe/Sub", "Goes",
                                                            &menuCallback, &goesRuns );
            REQUIRE( port.calls.size() == 2 );
            const auto staysAdded = port.calls[ 0 ];
            const auto goesAdded = port.calls[ 1 ];

            THEN( "Each gets an id greater than 0 of its own, and the port adds both" )
            {
                REQUIRE( stays > 0 );
                REQUIRE( goes > 0 );
                REQUIRE( stays != goes );
                REQUIRE( staysAdded.kind == PortCall::Kind::AddMenuAction );
                REQUIRE( staysAdded.label == "Stays" );
                REQUIRE( goesAdded.kind == PortCall::Kind::AddMenuAction );
                REQUIRE( goesAdded.menuPath == "Probe/Sub" );
                REQUIRE( goesAdded.label == "Goes" );
                REQUIRE( staysAdded.userData != goesAdded.userData );
            }

            AND_WHEN( "It unregisters one of them" )
            {
                api->unregister_menu_action( handle, goes );

                THEN( "The port removes that one, and the other still runs its callback" )
                {
                    REQUIRE( port.calls.size() == 3 );
                    REQUIRE( port.calls[ 2 ].kind == PortCall::Kind::RemoveMenuAction );
                    REQUIRE( port.calls[ 2 ].pluginId == ProbeId );
                    REQUIRE( port.calls[ 2 ].userData == goesAdded.userData );

                    staysAdded.callback( staysAdded.userData );
                    REQUIRE( staysRuns == 1 );
                }

                THEN( "A trigger of the removed one does not reach the plugin" )
                {
                    goesAdded.callback( goesAdded.userData );
                    REQUIRE( goesRuns == 0 );
                }

                AND_WHEN( "It unregisters the same id again, and ids it was never given" )
                {
                    api->unregister_menu_action( handle, goes );
                    api->unregister_menu_action( handle, 0 );
                    api->unregister_menu_action( handle, -1 );
                    api->unregister_menu_action( handle, stays + goes + 100 );

                    THEN( "Nothing happens" )
                    {
                        REQUIRE( port.calls.size() == 3 );
                        staysAdded.callback( staysAdded.userData );
                        REQUIRE( staysRuns == 1 );
                    }
                }

                AND_WHEN( "It registers an item of the removed one's label again" )
                {
                    const auto again = api->register_menu_action_ex( handle, "Probe/Sub", "Goes",
                                                                     &menuCallback, &goesRuns );

                    THEN( "It gets a new id, never handed out before" )
                    {
                        REQUIRE( again > 0 );
                        REQUIRE( again != stays );
                        REQUIRE( again != goes );
                        REQUIRE( port.calls.back().kind == PortCall::Kind::AddMenuAction );
                    }
                }
            }

            AND_WHEN( "A trigger of one is queued, and the plugin unregisters it before the "
                      "event loop turns" )
            {
                QMetaObject::invokeMethod(
                    &host, [ goesAdded ] { goesAdded.callback( goesAdded.userData ); },
                    Qt::QueuedConnection );
                api->unregister_menu_action( handle, goes );
                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                THEN( "The queued trigger does not call the plugin" )
                {
                    REQUIRE( goesRuns == 0 );
                }
            }

            AND_WHEN( "It unregisters one from a thread of its own" )
            {
                std::thread( [ & ] { api->unregister_menu_action( handle, goes ); } ).join();

                THEN( "A trigger after that does not call the plugin" )
                {
                    goesAdded.callback( goesAdded.userData );
                    REQUIRE( goesRuns == 0 );
                    REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveMenuAction );
                }
            }

            AND_WHEN( "The plugin is unloaded" )
            {
                host.unloadPlugin( ProbeId );

                THEN( "Its items go with all its contributions, and no id removes one" )
                {
                    REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveContributions );
                }
            }
        }

        WHEN( "The plugin registers an item with register_menu_action_ex from a thread of its "
              "own" )
        {
            int id = 0;
            std::thread( [ & ] {
                id = api->register_menu_action_ex( handle, "", "Threaded", &menuCallback,
                                                   &staysRuns );
            } ).join();

            THEN( "It gets the id at once" )
            {
                REQUIRE( id > 0 );
                REQUIRE( port.calls.size() == 1 );
            }
        }

        WHEN( "The plugin registers an item that removes itself when triggered" )
        {
            SelfRemovingMenuAction action{ .api = api, .handle = handle };
            action.id = api->register_menu_action_ex( handle, "Plugins", "Once",
                                                      &removeOwnMenuAction, &action );
            const auto added = port.calls.back();
            added.callback( added.userData );
            added.callback( added.userData );

            THEN( "It runs once, and the port removes it" )
            {
                REQUIRE( action.runs == 1 );
                REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveMenuAction );
                REQUIRE( port.calls.back().userData == added.userData );
            }
        }

        WHEN( "The plugin registers an item with register_menu_action_ex that cannot be added" )
        {
            api->register_menu_action( handle, "Plugins", "Old", &menuCallback, &staysRuns );
            REQUIRE( api->register_menu_action_ex( handle, "Plugins", "Taken", &menuCallback,
                                                   &staysRuns )
                     > 0 );
            const auto callsBefore = port.calls.size();

            const auto noLabel = api->register_menu_action_ex( handle, "Plugins", nullptr,
                                                               &menuCallback, nullptr );
            const auto emptyLabel
                = api->register_menu_action_ex( handle, "Plugins", "", &menuCallback, nullptr );
            const auto noCallback
                = api->register_menu_action_ex( handle, "Plugins", "Nothing", nullptr, nullptr );
            const auto noHandle = api->register_menu_action_ex( nullptr, "Plugins", "Lost",
                                                                &menuCallback, nullptr );
            const auto taken = api->register_menu_action_ex( handle, "Plugins", "Taken",
                                                             &menuCallback, nullptr );
            const auto takenByAnotherPath
                = api->register_menu_action_ex( handle, "/", "Taken", &menuCallback, nullptr );
            const auto old
                = api->register_menu_action_ex( handle, "Plugins", "Old", &menuCallback, nullptr );

            THEN( "It gets a negative value, and nothing is added" )
            {
                REQUIRE( noLabel < 0 );
                REQUIRE( emptyLabel < 0 );
                REQUIRE( noCallback < 0 );
                REQUIRE( noHandle < 0 );
                REQUIRE( taken < 0 );
                REQUIRE( takenByAnotherPath < 0 );
                REQUIRE( old < 0 );
                REQUIRE( port.calls.size() == callsBefore );
            }
        }

        WHEN( "The plugin registers items with register_menu_action_ex of a label it has an "
              "item of in another submenu (#870)" )
        {
            api->register_menu_action( handle, "Tools", "Export", &menuCallback, &staysRuns );
            const auto report = api->register_menu_action_ex( handle, "Report", "Export",
                                                              &menuCallback, nullptr );
            const auto toolsSub = api->register_menu_action_ex( handle, "Tools/Sub", "Export",
                                                                &menuCallback, nullptr );
            const auto tools = api->register_menu_action_ex( handle, "Plugins/Tools/", "Export",
                                                             &menuCallback, nullptr );
            const auto reportAgain = api->register_menu_action_ex( handle, " Report ", "Export",
                                                                   &menuCallback, nullptr );

            THEN( "Each in another submenu gets an id and is added, one in the same submenu "
                  "fails" )
            {
                REQUIRE( report > 0 );
                REQUIRE( toolsSub > 0 );
                REQUIRE( report != toolsSub );
                REQUIRE( tools < 0 );
                REQUIRE( reportAgain < 0 );
                REQUIRE( port.calls.size() == 3 );
                REQUIRE( port.calls[ 1 ].menuPath == "Report" );
                REQUIRE( port.calls[ 2 ].menuPath == "Tools/Sub" );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }

    GIVEN( "A Plugin Host without a Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        int runs = 0;

        WHEN( "The plugin registers a menu item with register_menu_action_ex" )
        {
            const auto id = api->register_menu_action_ex( handle, "Plugins", "Nowhere",
                                                          &menuCallback, &runs );
            api->unregister_menu_action( handle, id );

            THEN( "It gets a negative value, and unregistering it does nothing" )
            {
                REQUIRE( id < 0 );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

namespace {

/// What a context menu item of the plugin's was handed when it was chosen, copied.
struct ContextMenuChoice {
    size_t size = 0;
    QString filePath;
    int view = -1;
    std::vector<uint64_t> lineNumbers;
    bool lineNumbersNull = false;
    uint64_t selectedLineCount = 0;
    std::optional<QByteArray> selectedText;
    size_t selectedTextLength = 0;
};

/// The choices of the items a test registers, and what an item does as it is chosen.
struct ContextMenuChoices {
    std::vector<ContextMenuChoice> choices;
    // Set for an item that removes itself as it is chosen.
    const LogSquirlHostApi* api = nullptr;
    void* handle = nullptr;
    int removeId = 0;
};

void recordContextMenuChoice( void* userData, const LogSquirlContextMenuContext* context )
{
    auto* recorded = static_cast<ContextMenuChoices*>( userData );
    ContextMenuChoice choice;
    choice.size = context->size;
    choice.filePath = QString::fromUtf8( context->file_path );
    choice.view = context->view;
    choice.lineNumbersNull = context->line_numbers == nullptr;
    if ( context->line_numbers != nullptr ) {
        choice.lineNumbers.assign( context->line_numbers,
                                   context->line_numbers + context->line_number_count );
    }
    choice.selectedLineCount = context->selected_line_count;
    if ( context->selected_text != nullptr ) {
        choice.selectedText = QByteArray( context->selected_text,
                                          static_cast<qsizetype>( context->selected_text_length ) );
    }
    choice.selectedTextLength = context->selected_text_length;
    recorded->choices.push_back( std::move( choice ) );
    if ( recorded->api != nullptr ) {
        recorded->api->unregister_context_menu_action( recorded->handle, recorded->removeId );
    }
}

/// Chooses the item the port was handed in call, as the view does with what was selected.
void choose( const PortCall& call, const PluginContextMenuContext& context )
{
    REQUIRE( call.contextMenuCallback != nullptr );
    call.contextMenuCallback( call.userData, context );
}

} // namespace

SCENARIO( "A plugin adds items to the context menu of the Log Lines through the Plugin Host",
          "[pluginhost][plugincontextmenu][plugins]" )
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
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), register_context_menu_action ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), unregister_context_menu_action ) );

        ContextMenuChoices decoded;
        ContextMenuChoices copied;

        WHEN( "The plugin registers a menu item with an id, then two context menu items (#859)" )
        {
            int menuRuns = 0;
            const auto menuId = api->register_menu_action_ex( handle, "Plugins", "Menu",
                                                              &menuCallback, &menuRuns );
            const auto decodeId = api->register_context_menu_action(
                handle, "Probe/Decode", "Decode", &recordContextMenuChoice, &decoded );
            const auto copyId = api->register_context_menu_action(
                handle, nullptr, "Copy id", &recordContextMenuChoice, &copied );
            REQUIRE( port.calls.size() == 3 );
            const auto decode = port.calls[ 1 ];
            const auto copy = port.calls[ 2 ];

            THEN( "Each gets an id of its own from the plugin's one counter, and the port adds "
                  "both in that order" )
            {
                REQUIRE( menuId > 0 );
                REQUIRE( decodeId > menuId );
                REQUIRE( copyId > decodeId );
                REQUIRE( decode.kind == PortCall::Kind::AddContextMenuAction );
                REQUIRE( decode.pluginId == ProbeId );
                REQUIRE( decode.menuPath == QStringLiteral( "Probe/Decode" ) );
                REQUIRE( decode.label == QStringLiteral( "Decode" ) );
                REQUIRE( copy.kind == PortCall::Kind::AddContextMenuAction );
                REQUIRE( copy.menuPath.isEmpty() );
                REQUIRE( copy.label == QStringLiteral( "Copy id" ) );
                REQUIRE( decode.userData != copy.userData );
            }

            AND_WHEN( "The user chooses one in the Filtered View, on three Log Lines selected" )
            {
                choose( decode, PluginContextMenuContext{
                                    .logFile = QStringLiteral( "/logs/app.log" ),
                                    .view = PluginLogView::Filtered,
                                    .logLines = { 0, 41, 99 },
                                    .selectedLogLineCount = 3,
                                    .selectedText = std::nullopt,
                                } );

                THEN( "Its callback is handed the Log File, the view and the Log Lines "
                      "numbered from 1, and no selected text" )
                {
                    REQUIRE( decoded.choices.size() == 1 );
                    REQUIRE( copied.choices.empty() );
                    const auto& choice = decoded.choices.front();
                    REQUIRE( choice.size == sizeof( LogSquirlContextMenuContext ) );
                    REQUIRE( choice.filePath == QStringLiteral( "/logs/app.log" ) );
                    REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_FILTERED );
                    REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 1, 42, 100 } );
                    REQUIRE( choice.selectedLineCount == 3 );
                    REQUIRE_FALSE( choice.selectedText.has_value() );
                    REQUIRE( choice.selectedTextLength == 0 );
                }
            }

            AND_WHEN( "The user chooses one in the Table View, on a word selected in a cell" )
            {
                choose( copy, PluginContextMenuContext{
                                  .logFile = QStringLiteral( "/logs/app.log" ),
                                  .view = PluginLogView::Table,
                                  .logLines = { 7 },
                                  .selectedLogLineCount = 1,
                                  .selectedText = QStringLiteral( "größe" ),
                              } );

                THEN( "Its callback is handed the word in UTF-8, with its length in bytes" )
                {
                    REQUIRE( copied.choices.size() == 1 );
                    const auto& choice = copied.choices.front();
                    REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_TABLE );
                    REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 8 } );
                    REQUIRE( choice.selectedText == QStringLiteral( "größe" ).toUtf8() );
                    REQUIRE( choice.selectedTextLength == 7 );
                }
            }

            AND_WHEN( "The user chooses one in the Text View, on more Log Lines than it is "
                      "handed" )
            {
                PluginContextMenuContext many{ .logFile = QStringLiteral( "/logs/app.log" ),
                                               .view = PluginLogView::Text,
                                               .logLines = {},
                                               .selectedLogLineCount = 5000,
                                               .selectedText = std::nullopt };
                for ( uint64_t line = 0; line < 1200; ++line ) {
                    many.logLines.push_back( line );
                }
                choose( decode, many );

                THEN( "Its callback is handed the first of them only, and how many there were" )
                {
                    REQUIRE( decoded.choices.size() == 1 );
                    const auto& choice = decoded.choices.front();
                    REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_TEXT );
                    REQUIRE( choice.lineNumbers.size() == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                    REQUIRE( choice.lineNumbers.front() == 1 );
                    REQUIRE( choice.lineNumbers.back() == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                    REQUIRE( choice.selectedLineCount == 5000 );
                }
            }

            AND_WHEN( "It unregisters one of them" )
            {
                api->unregister_context_menu_action( handle, decodeId );

                THEN( "The port removes that one, a choice of it does not reach the plugin, and "
                      "the other still does" )
                {
                    REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveContextMenuAction );
                    REQUIRE( port.calls.back().pluginId == ProbeId );
                    REQUIRE( port.calls.back().userData == decode.userData );
                    choose( decode, PluginContextMenuContext{ .logLines = { 1 },
                                                              .selectedLogLineCount = 1 } );
                    choose( copy, PluginContextMenuContext{ .logLines = { 1 },
                                                            .selectedLogLineCount = 1 } );
                    REQUIRE( decoded.choices.empty() );
                    REQUIRE( copied.choices.size() == 1 );
                }

                AND_WHEN( "It unregisters the same id again, ids it was never given, and the id "
                          "of its menu item" )
                {
                    const auto callsBefore = port.calls.size();
                    api->unregister_context_menu_action( handle, decodeId );
                    api->unregister_context_menu_action( handle, 0 );
                    api->unregister_context_menu_action( handle, -3 );
                    api->unregister_context_menu_action( handle, copyId + 100 );
                    api->unregister_context_menu_action( handle, menuId );

                    THEN( "Nothing happens, and the menu item stays" )
                    {
                        REQUIRE( port.calls.size() == callsBefore );
                        port.calls[ 0 ].callback( port.calls[ 0 ].userData );
                        REQUIRE( menuRuns == 1 );
                    }
                }
            }

            AND_WHEN( "It unregisters the id of a context menu item with unregister_menu_action" )
            {
                const auto callsBefore = port.calls.size();
                api->unregister_menu_action( handle, copyId );

                THEN( "Nothing happens" )
                {
                    REQUIRE( port.calls.size() == callsBefore );
                }
            }

            AND_WHEN( "It unregisters one from a thread of its own" )
            {
                std::thread( [ & ] {
                    api->unregister_context_menu_action( handle, copyId );
                } ).join();

                THEN( "A choice of it after that does not reach the plugin" )
                {
                    choose( copy, PluginContextMenuContext{ .logLines = { 1 },
                                                            .selectedLogLineCount = 1 } );
                    REQUIRE( copied.choices.empty() );
                }
            }

            AND_WHEN( "The plugin is unloaded" )
            {
                host.unloadPlugin( ProbeId );

                THEN( "Its items go with all its contributions" )
                {
                    REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveContributions );
                }
            }
        }

        WHEN( "The plugin registers an item from a thread of its own" )
        {
            int id = 0;
            std::thread( [ & ] {
                id = api->register_context_menu_action( handle, "", "Threaded",
                                                        &recordContextMenuChoice, &copied );
            } ).join();

            THEN( "It gets the id at once" )
            {
                REQUIRE( id > 0 );
                REQUIRE( port.calls.size() == 1 );
                REQUIRE( port.calls.back().kind == PortCall::Kind::AddContextMenuAction );
            }
        }

        WHEN( "The plugin registers an item that removes itself when chosen" )
        {
            copied.api = api;
            copied.handle = handle;
            copied.removeId = api->register_context_menu_action(
                handle, "", "Once", &recordContextMenuChoice, &copied );
            const auto added = port.calls.back();
            choose( added,
                    PluginContextMenuContext{ .logLines = { 1 }, .selectedLogLineCount = 1 } );
            choose( added,
                    PluginContextMenuContext{ .logLines = { 1 }, .selectedLogLineCount = 1 } );

            THEN( "It runs once, and the port removes it" )
            {
                REQUIRE( copied.choices.size() == 1 );
                REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveContextMenuAction );
                REQUIRE( port.calls.back().userData == added.userData );
            }
        }

        WHEN( "The plugin registers items that cannot be added" )
        {
            const auto noLabel = api->register_context_menu_action(
                handle, "", nullptr, &recordContextMenuChoice, &copied );
            const auto emptyLabel = api->register_context_menu_action(
                handle, "", "", &recordContextMenuChoice, &copied );
            const auto noCallback
                = api->register_context_menu_action( handle, "", "Nothing", nullptr, &copied );
            const auto noHandle = api->register_context_menu_action(
                nullptr, "", "Lost", &recordContextMenuChoice, &copied );

            THEN( "It gets a negative value, and nothing is added" )
            {
                REQUIRE( noLabel < 0 );
                REQUIRE( emptyLabel < 0 );
                REQUIRE( noCallback < 0 );
                REQUIRE( noHandle < 0 );
                REQUIRE( port.calls.empty() );
            }
        }

        WHEN( "The plugin registers two items of the same label" )
        {
            const auto first = api->register_context_menu_action(
                handle, "", "Same", &recordContextMenuChoice, &copied );
            const auto second = api->register_context_menu_action(
                handle, "", "Same", &recordContextMenuChoice, &decoded );

            THEN( "Each is an item of its own" )
            {
                REQUIRE( first > 0 );
                REQUIRE( second > first );
                REQUIRE( port.calls.size() == 2 );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }

    GIVEN( "A Plugin Host without a Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        ContextMenuChoices nowhere;

        WHEN( "The plugin registers a context menu item" )
        {
            const auto id = api->register_context_menu_action( handle, "", "Nowhere",
                                                               &recordContextMenuChoice, &nowhere );
            api->unregister_context_menu_action( handle, id );

            THEN( "It gets a negative value, and unregistering it does nothing" )
            {
                REQUIRE( id < 0 );
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

        WHEN( "Auto-load is enabled and no plugin has been chosen yet" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = true, .enabled = {} } );

            THEN( "Nothing is loaded, and the first run is over with nothing enabled (#820)" )
            {
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE( loaded.enabledOnFirstRun );
                REQUIRE( loaded.enabledOnFirstRun->isEmpty() );
                REQUIRE( host.loadedPluginIds().isEmpty() );
            }
        }

        WHEN( "Auto-load is enabled and the user disabled every plugin" )
        {
            const auto loaded
                = host.autoLoadPlugins( { .autoLoad = true, .enabled = {}, .chosen = true } );

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

        WHEN( "The user disabled every plugin" )
        {
            const auto loaded
                = host.autoLoadPlugins( { .autoLoad = true, .enabled = {}, .chosen = true } );

            THEN( "The choice stands: nothing is enabled or loaded (#820)" )
            {
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE( host.loadedPluginIds().isEmpty() );
                REQUIRE( loadedSignals.isEmpty() );
            }
        }

        host.unloadAll();
    }

    // The two cases below go through a Configuration as the application does:
    // it is handed the enabled plugins and whether they were chosen, and keeps
    // what a first run enabled (#820).
    GIVEN( "A configuration whose last plugin was disabled, and a plugin dropped into the "
           "plugin folder since" )
    {
        Configuration config;
        config.setEnabledPlugins( {} );
        REQUIRE( config.enabledPluginsChosen() );

        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        WHEN( "The plugins are loaded at start-up" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = config.pluginsAutoLoad(),
                                                        .enabled = config.enabledPlugins(),
                                                        .chosen = config.enabledPluginsChosen() } );

            THEN( "The dropped-in plugin is listed but neither enabled nor loaded" )
            {
                REQUIRE( catalog.findDiscovered( ProbeId ) != nullptr );
                REQUIRE_FALSE( loaded.enabledOnFirstRun );
                REQUIRE( loaded.errors.isEmpty() );
                REQUIRE( host.loadedPluginIds().isEmpty() );
                REQUIRE( config.enabledPlugins().isEmpty() );
            }
        }

        host.unloadAll();
    }

    GIVEN( "A fresh configuration and a catalog listing the probe plugin" )
    {
        Configuration config;
        REQUIRE_FALSE( config.enabledPluginsChosen() );

        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        WHEN( "The plugins are loaded at start-up and the first run's choice is kept" )
        {
            const auto loaded = host.autoLoadPlugins( { .autoLoad = config.pluginsAutoLoad(),
                                                        .enabled = config.enabledPlugins(),
                                                        .chosen = config.enabledPluginsChosen() } );
            REQUIRE( loaded.enabledOnFirstRun );
            config.setEnabledPlugins( *loaded.enabledOnFirstRun );

            THEN( "The discovered plugin is enabled and loaded once, and the choice is stored "
                  "with the list" )
            {
                REQUIRE( host.loadedPluginIds() == QStringList{ ProbeId } );
                REQUIRE( config.enabledPlugins() == QStringList{ ProbeId } );
                REQUIRE( config.enabledPluginsChosen() );

                const auto stored = configuration_fixture::storedSettings( config );
                REQUIRE( stored.value( "plugins.chosen" ).toBool() );
                REQUIRE( stored.value( "plugins.enabledPlugins" ).toStringList()
                         == QStringList{ ProbeId } );
            }

            AND_WHEN( "The next start finds the probe plugin disabled meanwhile" )
            {
                host.unloadAll();
                config.setEnabledPlugins( {} );
                const auto again
                    = host.autoLoadPlugins( { .autoLoad = config.pluginsAutoLoad(),
                                              .enabled = config.enabledPlugins(),
                                              .chosen = config.enabledPluginsChosen() } );

                THEN( "No first run happens again and nothing is loaded" )
                {
                    REQUIRE_FALSE( again.enabledOnFirstRun );
                    REQUIRE( host.loadedPluginIds().isEmpty() );
                }
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

SCENARIO( "A plugin opens the Regex Lab through the Plugin Host",
          "[pluginhost][pluginregexlab][plugins]" )
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
        REQUIRE( api->open_regex_lab != nullptr );

        RegexLabAnswers received;

        WHEN( "The plugin opens the Regex Lab with a pattern that matches case" )
        {
            const auto opened = api->open_regex_lab( handle, "ERROR \xc3\xa4 (\\d+)",
                                                     LOGSQUIRL_REGEX_LAB_MATCH_CASE,
                                                     &recordRegexLabAnswer, &received );

            THEN( "The port opens a Lab for the plugin with that pattern" )
            {
                REQUIRE( opened == 0 );
                REQUIRE( port.labs.size() == 1 );
                REQUIRE( port.labs[ 0 ].pluginId == ProbeId );
                REQUIRE( port.labs[ 0 ].pattern
                         == PluginPattern{ .pattern = QStringLiteral( "ERROR \u00e4 (\\d+)" ),
                                           .matchesCase = true } );
                REQUIRE( received.answers.empty() );
            }

            AND_WHEN( "The user applies a pattern that ignores case" )
            {
                REQUIRE( port.labs.size() == 1 );
                port.labs[ 0 ].answer( PluginPattern{ .pattern = QStringLiteral( "WARN \u00f6" ),
                                                      .matchesCase = false } );

                THEN( "The plugin gets the pattern in UTF-8, without the match case flag" )
                {
                    REQUIRE( received.answers.size() == 1 );
                    REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_APPLIED );
                    REQUIRE( received.answers[ 0 ].pattern == QStringLiteral( "WARN \u00f6" ) );
                    REQUIRE( received.answers[ 0 ].flags == 0 );
                }
            }

            AND_WHEN( "The user cancels" )
            {
                REQUIRE( port.labs.size() == 1 );
                port.labs[ 0 ].answer( std::nullopt );

                THEN( "The plugin hears it was cancelled, with no pattern" )
                {
                    REQUIRE( received.answers.size() == 1 );
                    REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_CANCELLED );
                    REQUIRE_FALSE( received.answers[ 0 ].pattern.has_value() );
                    REQUIRE( received.answers[ 0 ].flags == 0 );
                }
            }

            AND_WHEN( "The plugin is unloaded" )
            {
                host.unloadPlugin( ProbeId );

                THEN( "The context the answer is connected with is gone" )
                {
                    REQUIRE( port.labs.size() == 1 );
                    REQUIRE( port.labs[ 0 ].context.isNull() );
                }
            }
        }

        WHEN( "The plugin opens the Regex Lab with no pattern and ignoring case" )
        {
            const auto opened
                = api->open_regex_lab( handle, nullptr, 0, &recordRegexLabAnswer, &received );

            THEN( "The Lab opens with an empty pattern that ignores case" )
            {
                REQUIRE( opened == 0 );
                REQUIRE( port.labs.size() == 1 );
                REQUIRE( port.labs[ 0 ].pattern
                         == PluginPattern{ .pattern = QString(), .matchesCase = false } );
            }
        }

        WHEN( "The plugin opens the Regex Lab without a callback" )
        {
            const auto opened = api->open_regex_lab( handle, "ERROR", 0, nullptr, nullptr );

            THEN( "No Lab opens" )
            {
                REQUIRE( opened != 0 );
                REQUIRE( port.labs.empty() );
            }
        }

        WHEN( "The plugin opens the Regex Lab off the UI thread" )
        {
            int opened = 0;
            std::thread( [ & ] {
                opened
                    = api->open_regex_lab( handle, "ERROR", 0, &recordRegexLabAnswer, &received );
            } ).join();

            THEN( "No Lab opens" )
            {
                REQUIRE( opened != 0 );
                REQUIRE( port.labs.empty() );
            }
        }

        WHEN( "The port shows no Regex Lab, as without a window" )
        {
            port.opensLabs = false;
            const auto opened
                = api->open_regex_lab( handle, "ERROR", 0, &recordRegexLabAnswer, &received );

            THEN( "The plugin is told no Lab opened" )
            {
                REQUIRE( opened != 0 );
                REQUIRE( received.answers.empty() );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

SCENARIO( "A Regex Lab answer that arrives while the plugin's unload is pending reaches it "
          "only if it stays loaded",
          "[pluginhost][pluginregexlab][plugins]" )
{
    GIVEN( "The probe plugin loaded, with a Regex Lab open, and a menu action that runs "
           "what the test hands it as a call into the plugin" )
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

        RegexLabAnswers received;
        REQUIRE( api->open_regex_lab( handle, "ERROR", 0, &recordRegexLabAnswer, &received ) == 0 );
        REQUIRE( port.labs.size() == 1 );

        std::function<void()> whileRunning;
        api->register_menu_action( handle, "Plugins", "Probe action", &runInPlugin, &whileRunning );
        const auto added = port.calls.back();
        REQUIRE( added.kind == PortCall::Kind::AddMenuAction );
        const auto runningCall = [ & ]( std::function<void()> call ) {
            whileRunning = std::move( call );
            added.callback( added.userData );
        };
        const auto turnEventLoop
            = [ & ] { QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall ); };
        const auto applied
            = PluginPattern{ .pattern = QStringLiteral( "WARN \u00f6" ), .matchesCase = true };

        WHEN( "The Lab is applied while the unload waits, and the plugin is loaded again "
              "before the call returns" )
        {
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 0 ].answer( applied );
                REQUIRE( received.answers.empty() );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
                REQUIRE( received.answers.empty() );
            } );

            THEN( "Once the event loop turns, the plugin gets the applied pattern and flags, "
                  "exactly once" )
            {
                REQUIRE( received.answers.empty() );
                turnEventLoop();
                REQUIRE( received.answers.size() == 1 );
                REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_APPLIED );
                REQUIRE( received.answers[ 0 ].pattern == QStringLiteral( "WARN \u00f6" ) );
                REQUIRE( received.answers[ 0 ].flags == LOGSQUIRL_REGEX_LAB_MATCH_CASE );

                turnEventLoop();
                REQUIRE( received.answers.size() == 1 );
                REQUIRE( host.isLoaded( ProbeId ) );
            }

            AND_WHEN( "The plugin is unloaded before the event loop turns" )
            {
                host.unloadPlugin( ProbeId );
                turnEventLoop();

                THEN( "The answer never reaches it" )
                {
                    REQUIRE( received.answers.empty() );
                }
            }
        }

        WHEN( "The Lab is cancelled while the unload waits, and the plugin is loaded again "
              "before the call returns" )
        {
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 0 ].answer( std::nullopt );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
            } );
            turnEventLoop();
            turnEventLoop();

            THEN( "The plugin hears exactly once that it was cancelled" )
            {
                REQUIRE( received.answers.size() == 1 );
                REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_CANCELLED );
                REQUIRE_FALSE( received.answers[ 0 ].pattern.has_value() );
            }
        }

        WHEN( "Two Labs are answered while the unload waits, and the plugin is loaded again "
              "before the call returns" )
        {
            REQUIRE( api->open_regex_lab( handle, "INFO", 0, &recordRegexLabAnswer, &received )
                     == 0 );
            REQUIRE( port.labs.size() == 2 );
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 1 ].answer( std::nullopt );
                port.labs[ 0 ].answer( applied );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
            } );

            THEN( "The plugin gets both answers in the order they arrived" )
            {
                turnEventLoop();
                REQUIRE( received.answers.size() == 2 );
                REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_CANCELLED );
                REQUIRE( received.answers[ 1 ].result == LOGSQUIRL_REGEX_LAB_APPLIED );
            }
        }

        WHEN( "A Lab is answered after the plugin is loaded again, before the kept answer is "
              "delivered" )
        {
            REQUIRE( api->open_regex_lab( handle, "INFO", 0, &recordRegexLabAnswer, &received )
                     == 0 );
            REQUIRE( port.labs.size() == 2 );
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 0 ].answer( applied );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
            } );
            port.labs[ 1 ].answer( std::nullopt );

            THEN( "It waits behind the kept answer, and both arrive in order" )
            {
                REQUIRE( received.answers.empty() );
                turnEventLoop();
                REQUIRE( received.answers.size() == 2 );
                REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_APPLIED );
                REQUIRE( received.answers[ 1 ].result == LOGSQUIRL_REGEX_LAB_CANCELLED );
            }
        }

        WHEN( "The Lab is applied while the unload waits, and the unload completes" )
        {
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 0 ].answer( applied );
                REQUIRE( received.answers.empty() );
            } );
            REQUIRE( host.isLoaded( ProbeId ) == false );
            REQUIRE( received.answers.empty() );
            turnEventLoop();
            turnEventLoop();

            THEN( "The plugin is shut down and the answer never reaches it" )
            {
                REQUIRE( port.calls.back().kind == PortCall::Kind::RemoveContributions );
                REQUIRE( port.labs[ 0 ].context.isNull() );
                REQUIRE( received.answers.empty() );
            }
        }

        WHEN( "The plugin is loaded again, then its unload waits again before the delivery "
              "runs" )
        {
            runningCall( [ & ] {
                host.unloadPlugin( ProbeId );
                port.labs[ 0 ].answer( applied );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
                host.unloadPlugin( ProbeId );
                turnEventLoop();
                REQUIRE( received.answers.empty() );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
            } );
            turnEventLoop();

            THEN( "The answer is kept and reaches the plugin exactly once when it stays" )
            {
                REQUIRE( received.answers.size() == 1 );
                REQUIRE( received.answers[ 0 ].result == LOGSQUIRL_REGEX_LAB_APPLIED );
                turnEventLoop();
                REQUIRE( received.answers.size() == 1 );
            }
        }

        WHEN( "The Lab is answered while the plugin's call runs and no unload waits" )
        {
            runningCall( [ & ] { port.labs[ 0 ].answer( applied ); } );

            THEN( "The plugin gets the answer right away, exactly once" )
            {
                REQUIRE( received.answers.size() == 1 );
                turnEventLoop();
                REQUIRE( received.answers.size() == 1 );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

SCENARIO( "The host API table starts as the first plugin header declared it",
          "[pluginhost][plugins]" )
{
    GIVEN( "The probe plugin built against the header of LogSquirl 26.10, and built against the "
           "current one" )
    {
        Probe first( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API1_PATH ) );
        Probe current( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( first.library.load() );
        REQUIRE( current.library.load() );

        THEN( "The table then ended where later functions start now" )
        {
            REQUIRE( first.hostApiSize() == LOGSQUIRL_HOST_API_BASE_SIZE );
            REQUIRE( current.hostApiSize() == sizeof( LogSquirlHostApi ) );
        }

        THEN( "Each member of then lies where it lies now" )
        {
            const auto offsets = first.firstMemberOffsets();
            REQUIRE( offsets.size() == 18 );
            REQUIRE( offsets == current.firstMemberOffsets() );
        }

        current.library.unload();
        first.library.unload();
    }
}

SCENARIO( "A plugin built against the first plugin header loads and runs unchanged",
          "[pluginhost][plugins]" )
{
    GIVEN( "The probe plugin built against the header of LogSquirl 26.10, which knows no later "
           "host function" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path(), QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API1_PATH ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        WHEN( "The Plugin Host loads it" )
        {
            const auto error = host.loadPlugin( ProbeId );

            Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API1_PATH ) );
            REQUIRE( probe.library.load() );
            const auto* api = probe.hostApi();
            auto* handle = probe.hostHandle();

            THEN( "It is initialised with the table it knows, and its calls reach the host" )
            {
                REQUIRE( error.isEmpty() );
                REQUIRE( host.isLoaded( ProbeId ) );
                REQUIRE( api != nullptr );
                REQUIRE( api->api_version == LOGSQUIRL_PLUGIN_API_VERSION );

                int menuUserData = 0;
                api->register_menu_action( handle, "Plugins", "Probe", &menuCallback,
                                           &menuUserData );
                REQUIRE( port.calls.size() == 1 );
                REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::AddMenuAction );
                REQUIRE( port.calls[ 0 ].label == "Probe" );
            }

            AND_WHEN( "It is unloaded" )
            {
                host.unloadPlugin( ProbeId );

                THEN( "Its shutdown ran and its contributions are removed" )
                {
                    REQUIRE_FALSE( host.isLoaded( ProbeId ) );
                    REQUIRE( port.calls.size() == 2 );
                    REQUIRE( port.calls[ 0 ].kind == PortCall::Kind::RemoveFooterWidget );
                    REQUIRE( port.calls[ 1 ].kind == PortCall::Kind::RemoveContributions );
                }
            }

            host.unloadAll();
            probe.library.unload();
        }
    }
}

namespace {

/// What logsquirl_probe_log_file_encoding returns on a host without the function.
constexpr int ProbeNotOffered = 100;

void ignoreFooterWidget( void* /*handle*/, void* /*qwidget_ptr*/ ) {}

} // namespace

SCENARIO( "The host API table and the convert control keep every member as 26.12 published them",
          "[pluginhost][plugins]" )
{
    GIVEN( "The probe plugin built against the header of LogSquirl 26.12.0, and built against the "
           "current one" )
    {
        Probe published( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );
        Probe current( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( published.library.load() );
        REQUIRE( current.library.load() );

        THEN( "Each member of then lies where it lies now and is as big (#863)" )
        {
            const auto members = published.members2612();
            // 23 members of LogSquirlHostApi and 4 of LogSquirlConvertControl,
            // an offset and a size each.
            REQUIRE( members.size() == 2 * ( 23 + 4 ) );
            REQUIRE( members == current.members2612() );
        }

        THEN( "Both tables then ended where the current ones end or later members start" )
        {
            REQUIRE( published.hostApiSize()
                     == offsetof( LogSquirlHostApi, register_encoding_callback )
                            + sizeof( LogSquirlHostApi::register_encoding_callback ) );
            REQUIRE( published.hostApiSize() <= sizeof( LogSquirlHostApi ) );
            REQUIRE( published.convertControlSize()
                     == offsetof( LogSquirlConvertControl, is_cancelled )
                            + sizeof( LogSquirlConvertControl::is_cancelled ) );
            REQUIRE( published.convertControlSize() <= sizeof( LogSquirlConvertControl ) );
        }

        current.library.unload();
        published.library.unload();
    }
}

SCENARIO( "A plugin built against the 26.12 plugin header loads and finds the 26.12 functions",
          "[pluginhost][pluginencoding][plugins]" )
{
    GIVEN( "The probe plugin built against the header of LogSquirl 26.12.0, loaded by the "
           "Plugin Host" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path(), QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        port.encodingInFront = QStringLiteral( "UTF-16LE" );
        PluginHost host( catalog );
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );
        REQUIRE( probe.library.load() );

        WHEN( "It asks for the Encoding of the tab in front, checking LOGSQUIRL_HOST_API_HAS" )
        {
            const auto [ result, encoding ] = probe.logFileEncoding( nullptr );

            THEN( "The host offers the function and answers (#863)" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( encoding == QStringLiteral( "UTF-16LE" ) );
                REQUIRE( port.askedForEncodingOf == QStringList{ QString() } );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }

    GIVEN( "The same probe initialised with the table of a LogSquirl 26.11 host" )
    {
        QLibrary library( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );
        REQUIRE( library.load() );
        const auto initEx = reinterpret_cast<LogSquirlPluginInitExFn>(
            library.resolve( LOGSQUIRL_PLUGIN_ENTRY_INIT_EX ) );
        const auto shutdown = reinterpret_cast<LogSquirlPluginShutdownFn>(
            library.resolve( LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN ) );
        REQUIRE( initEx != nullptr );
        REQUIRE( shutdown != nullptr );

        // A 26.11 host's table ends where the 26.12 functions start: exactly
        // that much memory, so that reading past it is caught by the address
        // sanitizer.
        constexpr auto size2611 = offsetof( LogSquirlHostApi, get_log_file_encoding );
        LogSquirlHostApi fullTable{};
        fullTable.api_version = LOGSQUIRL_PLUGIN_API_VERSION;
        fullTable.unregister_footer_widget = &ignoreFooterWidget;
        const auto table2611 = std::make_unique<unsigned char[]>( size2611 );
        std::memcpy( table2611.get(), &fullTable, size2611 );
        REQUIRE( initEx( reinterpret_cast<const LogSquirlHostApi*>( table2611.get() ), nullptr,
                         size2611 )
                 == 0 );

        WHEN( "It asks for the Encoding, checking LOGSQUIRL_HOST_API_HAS" )
        {
            Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_API2612_PATH ) );
            REQUIRE( probe.library.load() );
            const auto [ result, encoding ] = probe.logFileEncoding( nullptr );

            THEN( "It finds the host does not offer it and does without (#863)" )
            {
                REQUIRE( result == ProbeNotOffered );
                REQUIRE( encoding.isEmpty() );
            }
            probe.library.unload();
        }

        shutdown();
        library.unload();
    }
}

namespace {

/// What get_selected_log_lines handed back.
struct SelectedText {
    int result = 0;
    const char* text = nullptr;
    size_t length = 0;
    size_t lineCount = 0;
};

SelectedText readSelected( const LogSquirlHostApi& api, void* handle )
{
    SelectedText read;
    read.result = api.get_selected_log_lines( handle, &read.text, &read.length, &read.lineCount );
    return read;
}

} // namespace

SCENARIO( "A plugin goes to a Log Line and reads the selected Log Lines through the Plugin Host",
          "[pluginhost][pluginloglines][plugins]" )
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
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), go_to_log_line ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_selected_log_lines ) );

        WHEN( "The plugin goes to line number 1" )
        {
            const auto result = api->go_to_log_line( handle, 1 );

            THEN( "The port goes to the first Log Line, counted from 0" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( port.wentTo == std::vector<std::uint64_t>{ 0 } );
            }
        }

        WHEN( "The plugin goes to line number 0" )
        {
            const auto result = api->go_to_log_line( handle, 0 );

            THEN( "It is out of range, and the port is not asked" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( port.wentTo.empty() );
            }
        }

        WHEN( "The Log File has no such line" )
        {
            port.jump = PluginLogLineJump::OutOfRange;

            THEN( "The plugin is told it is out of range" )
            {
                REQUIRE( api->go_to_log_line( handle, 5000 ) == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
            }
        }

        WHEN( "No Log File is open" )
        {
            port.jump = PluginLogLineJump::NoLogFile;
            port.selected.reset();

            THEN( "The plugin is told so by both functions, and gets no text" )
            {
                REQUIRE( api->go_to_log_line( handle, 1 ) == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                const auto read = readSelected( *api, handle );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.text == nullptr );
                REQUIRE( read.length == 0 );
                REQUIRE( read.lineCount == 0 );
            }
        }

        WHEN( "No Log Line is selected" )
        {
            port.selected = QStringList{};

            THEN( "The plugin is told so, and gets no text" )
            {
                const auto read = readSelected( *api, handle );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_SELECTION );
                REQUIRE( read.text == nullptr );
            }
        }

        WHEN( "Log Lines are selected and the plugin reads them" )
        {
            port.selected = QStringList{ QStringLiteral( "first \u00e4" ), QString(),
                                         QStringLiteral( "third" ) };
            const auto read = readSelected( *api, handle );

            THEN( "It gets them in UTF-8, joined by line feeds, with no line feed at the end" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.text != nullptr );
                REQUIRE( std::string( read.text ) == "first \xc3\xa4\n\nthird" );
                REQUIRE( read.length == std::strlen( read.text ) );
                REQUIRE( read.lineCount == 3 );
            }

            THEN( "The port is asked for one Log Line more than a plugin gets, and no more "
                  "bytes" )
            {
                REQUIRE( port.askedForAtMost
                         == std::vector<std::pair<std::size_t, std::size_t>>{
                             { LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES + 1,
                               LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES } } );
            }

            AND_WHEN( "It reads them once more after the selection changed" )
            {
                const std::string before( read.text );
                port.selected = QStringList{ QStringLiteral( "other" ) };
                const auto again = readSelected( *api, handle );

                THEN( "The text of the first call was valid until then, the new one is the "
                      "new selection's" )
                {
                    REQUIRE( before == "first \xc3\xa4\n\nthird" );
                    REQUIRE( std::string( again.text ) == "other" );
                }
            }
        }

        WHEN( "The plugin wants only the text" )
        {
            port.selected = QStringList{ QStringLiteral( "only" ) };
            const char* text = nullptr;
            const auto result = api->get_selected_log_lines( handle, &text, nullptr, nullptr );

            THEN( "It gets it without the length and the count" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( std::string( text ) == "only" );
            }
        }

        WHEN( "The plugin passes no place for the text" )
        {
            port.selected = QStringList{ QStringLiteral( "only" ) };
            size_t length = 7;

            THEN( "It is an invalid argument, and the port is not asked" )
            {
                REQUIRE( api->get_selected_log_lines( handle, nullptr, &length, nullptr )
                         == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( length == 0 );
                REQUIRE( port.askedForAtMost.empty() );
            }
        }

        WHEN( "More Log Lines are selected than a plugin gets" )
        {
            QStringList lines;
            for ( int i = 0; i < LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES + 5; ++i ) {
                lines.append( QString::number( i ) );
            }
            port.selected = lines;
            const auto read = readSelected( *api, handle );

            THEN( "It gets the first ones and is told there are more" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( read.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                const auto text = QString::fromUtf8( read.text );
                REQUIRE( text.startsWith( QStringLiteral( "0\n1\n" ) ) );
                REQUIRE( text.endsWith(
                    QStringLiteral( "\n%1" ).arg( LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES - 1 ) ) );
            }
        }

        WHEN( "Exactly as many Log Lines are selected as a plugin gets" )
        {
            QStringList lines;
            for ( int i = 0; i < LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES; ++i ) {
                lines.append( QStringLiteral( "x" ) );
            }
            port.selected = lines;

            THEN( "It gets all of them, not truncated" )
            {
                const auto read = readSelected( *api, handle );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
            }
        }

        WHEN( "The selected Log Lines hold more bytes than a plugin gets" )
        {
            constexpr auto MaxBytes = qsizetype{ LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES };
            // Two lines fit with their line feed, the third does not.
            const auto half = QString( MaxBytes / 2 - 1, QChar( 'a' ) );
            port.selected = QStringList{ half, half, QStringLiteral( "b" ) };
            const auto read = readSelected( *api, handle );

            THEN( "It gets the whole lines that fit, and is told there are more" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( read.lineCount == 2 );
                REQUIRE( read.length == static_cast<size_t>( 2 * half.size() + 1 ) );
                REQUIRE( read.length <= LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
            }
        }

        WHEN( "The first selected Log Line alone holds more bytes than a plugin gets" )
        {
            constexpr auto MaxBytes = qsizetype{ LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES };
            // "a" and then two-byte characters: the bound falls inside one.
            const auto line = QStringLiteral( "a" ) + QString( MaxBytes / 2, QChar( 0x00e4 ) );
            port.selected = QStringList{ line, QStringLiteral( "next" ) };
            const auto read = readSelected( *api, handle );

            THEN( "It gets that Log Line cut at a character, within the bound" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( read.lineCount == 1 );
                REQUIRE( read.length == static_cast<size_t>( MaxBytes - 1 ) );
                const auto text
                    = QString::fromUtf8( read.text, static_cast<qsizetype>( read.length ) );
                REQUIRE( text == line.left( ( MaxBytes - 1 ) / 2 + 1 ) );
                REQUIRE( text.toUtf8().size() == MaxBytes - 1 );
            }
        }

        WHEN( "The port cut the first Log Line short, for the bytes" )
        {
            port.selected = QStringList{ QStringLiteral( "cut short" ) };
            port.lastCut = true;
            const auto read = readSelected( *api, handle );

            THEN( "The plugin gets what was read and is told it is truncated" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( std::string( read.text ) == "cut short" );
            }
        }

        WHEN( "The port cut a Log Line short after whole ones, for the bytes" )
        {
            port.selected = QStringList{ QStringLiteral( "whole" ), QStringLiteral( "cut" ) };
            port.lastCut = true;
            const auto read = readSelected( *api, handle );

            THEN( "The plugin gets the whole ones only, and is told it is truncated" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( std::string( read.text ) == "whole" );
                REQUIRE( read.lineCount == 1 );
            }
        }

        WHEN( "The port left Log Lines unread, for the bytes" )
        {
            port.selected = QStringList{ QStringLiteral( "read" ) };
            port.more = true;

            THEN( "The plugin is told it is truncated" )
            {
                REQUIRE( readSelected( *api, handle ).result == LOGSQUIRL_LOG_LINES_TRUNCATED );
            }
        }

        WHEN( "The plugin goes to a Log Line with no handle" )
        {
            THEN( "It is an invalid argument, and the port is not asked" )
            {
                REQUIRE( api->go_to_log_line( nullptr, 1 )
                         == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( port.wentTo.empty() );
            }
        }

        WHEN( "The plugin calls both off the UI thread" )
        {
            port.selected = QStringList{ QStringLiteral( "only" ) };
            int wentTo = 0;
            SelectedText read;
            std::thread( [ & ] {
                wentTo = api->go_to_log_line( handle, 1 );
                read = readSelected( *api, handle );
            } ).join();

            THEN( "Neither reaches the port, and both say why" )
            {
                REQUIRE( wentTo == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( read.text == nullptr );
                REQUIRE( port.wentTo.empty() );
                REQUIRE( port.askedForAtMost.empty() );
            }
        }

        WHEN( "The host has no Plugin UI Port, as without a window" )
        {
            host.setUiPort( nullptr );

            THEN( "There is no Log File" )
            {
                REQUIRE( api->go_to_log_line( handle, 1 ) == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( readSelected( *api, handle ).result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
            host.setUiPort( &port );
        }

        host.unloadAll();
        probe.library.unload();
    }
}

// ── Conversions in the background (#795) ───────────────────────────────────

namespace {

/// Writes content into a new file at path.
void writeInput( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( content );
}

/// The whole of the file at path.
QByteArray readAll( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

/// Turns the event loop for ms milliseconds.
void turnFor( int ms )
{
    QEventLoop loop;
    QTimer::singleShot( ms, &loop, &QEventLoop::quit );
    loop.exec();
}

/// What a test heard of a Conversion; written on the host's thread only.
struct ConversionHeard {
    int started = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    std::optional<ConversionEnd> end;
    QThread* endedOn = nullptr;

    ConversionReport report()
    {
        return ConversionReport{
            .started = [ this ] { ++started; },
            .progress = [ this ]( std::uint64_t done,
                                  std::uint64_t total ) { progress.emplace_back( done, total ); },
            .finished =
                [ this ]( const ConversionEnd& ended ) {
                    end = ended;
                    endedOn = QThread::currentThread();
                },
        };
    }

    bool ended() const
    {
        return end.has_value();
    }
};

using Progress = std::pair<std::uint64_t, std::uint64_t>;

} // namespace

SCENARIO( "The Plugin Host converts in the background, with progress and Cancel",
          "[pluginhost][plugins][conversion]" )
{
    GIVEN( "A Plugin Host with a convert probe of kind \"ui\" loaded, and a file to convert" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installConvertProbe( pluginRoot.path() );
        installPlainConvertProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        auto host = std::make_unique<PluginHost>( catalog );
        host->setUiPort( &port );
        REQUIRE( host->loadPlugin( ConvertProbeId ).isEmpty() );

        ConvertProbe probe( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PATH ) );

        QTemporaryDir files;
        REQUIRE( files.isValid() );
        const auto input = files.filePath( "capture.cvx" );
        writeInput( input, "one\ntwo\nthree\n" );
        const auto output = files.filePath( "capture.cvx.txt" );
        const auto secondInput = files.filePath( "second.cvx" );
        writeInput( secondInput, "four\n" );
        const auto secondOutput = files.filePath( "second.cvx.txt" );

        ConversionHeard heard;
        ConversionHeard secondHeard;

        WHEN( "A Conversion is started and the converter holds after its first line" )
        {
            // Counts how often the event loop turned meanwhile.
            int ticks = 0;
            QTimer ticker;
            QObject::connect( &ticker, &QTimer::timeout, [ &ticks ] { ++ticks; } );
            ticker.start( 1 );

            const auto id = host->startConversion( ConvertProbeId, input, output, heard.report() );
            REQUIRE( id != 0 );
            REQUIRE( heard.started == 1 );
            REQUIRE( waitUiState( [ & ] { return !heard.progress.empty(); }, 10000 ) );

            THEN( "It started at once, the call runs on another thread while this one turns, "
                  "and the progress reported arrives on the host's thread" )
            {
                REQUIRE( heard.progress.front() == Progress{ 1, 3 } );
                REQUIRE( probe.running() == 1 );
                REQUIRE_FALSE( heard.ended() );
                REQUIRE( ticks > 0 );
                REQUIRE( host->isLoaded( ConvertProbeId ) );
            }

            AND_WHEN( "The converter runs through" )
            {
                probe.openGate();

                THEN( "The Conversion finishes successfully on the host's thread, with every "
                      "line converted and the last progress reported" )
                {
                    REQUIRE( waitUiState( [ & ] { return heard.ended(); }, 10000 ) );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Succeeded );
                    REQUIRE( heard.end->returnCode == 0 );
                    REQUIRE( heard.endedOn == host->thread() );
                    REQUIRE( heard.progress.back() == Progress{ 3, 3 } );
                    REQUIRE( readAll( output )
                             == "converted: one\nconverted: two\nconverted: three\n" );
                    REQUIRE( probe.conversionsReturned() == 1 );
                    REQUIRE( probe.running() == 0 );
                }
            }

            AND_WHEN( "The Conversion is cancelled" )
            {
                host->cancelConversion( id );

                THEN( "The converter hears it through is_cancelled, returns, and the Conversion "
                      "finishes cancelled once the call has returned" )
                {
                    REQUIRE( waitUiState( [ & ] { return heard.ended(); }, 10000 ) );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Cancelled );
                    REQUIRE( probe.cancellationsSeen() == 1 );
                    REQUIRE( probe.conversionsReturned() == 1 );
                    REQUIRE( probe.running() == 0 );
                    REQUIRE( host->isLoaded( ConvertProbeId ) );
                }
            }

            AND_WHEN( "A second Conversion of the same plugin is started meanwhile" )
            {
                const auto secondId = host->startConversion( ConvertProbeId, secondInput,
                                                             secondOutput, secondHeard.report() );
                REQUIRE( secondId != 0 );
                REQUIRE( secondId != id );
                turnFor( 100 );

                THEN( "It waits its turn: one call into the plugin at a time, in the order "
                      "started" )
                {
                    REQUIRE( secondHeard.started == 0 );
                    REQUIRE( probe.conversionsStarted() == 1 );
                    REQUIRE( probe.running() == 1 );

                    probe.openGate();
                    REQUIRE( waitUiState( [ & ] { return heard.ended() && secondHeard.ended(); },
                                          10000 ) );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Succeeded );
                    REQUIRE( secondHeard.started == 1 );
                    REQUIRE( secondHeard.end->outcome == ConversionOutcome::Succeeded );
                    REQUIRE( probe.mostRunningAtOnce() == 1 );
                    REQUIRE( readAll( secondOutput ) == "converted: four\n" );
                }

                AND_WHEN( "The queued one is cancelled before its turn" )
                {
                    host->cancelConversion( secondId );

                    THEN( "It finishes cancelled at once, without a call into the plugin, and "
                          "the first one runs on" )
                    {
                        REQUIRE( secondHeard.ended() );
                        REQUIRE( secondHeard.end->outcome == ConversionOutcome::Cancelled );
                        REQUIRE( secondHeard.started == 0 );
                        REQUIRE_FALSE( heard.ended() );

                        probe.openGate();
                        REQUIRE( waitUiState( [ & ] { return heard.ended(); }, 10000 ) );
                        REQUIRE( heard.end->outcome == ConversionOutcome::Succeeded );
                        REQUIRE( probe.conversionsStarted() == 1 );
                    }
                }
            }

            AND_WHEN( "The plugin is unloaded with that Conversion under way and another queued" )
            {
                REQUIRE( host->startConversion( ConvertProbeId, secondInput, secondOutput,
                                                secondHeard.report() )
                         != 0 );
                host->unloadPlugin( ConvertProbeId );

                THEN( "Both are cancelled, the running call had returned before the plugin was "
                      "shut down, and nothing calls into the plugin any more" )
                {
                    REQUIRE( heard.ended() );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Cancelled );
                    REQUIRE( secondHeard.ended() );
                    REQUIRE( secondHeard.end->outcome == ConversionOutcome::Cancelled );
                    REQUIRE( secondHeard.started == 0 );
                    REQUIRE( probe.cancellationsSeen() == 1 );
                    REQUIRE( probe.conversionsReturned() == 1 );
                    REQUIRE( probe.conversionsStarted() == 1 );
                    REQUIRE( probe.shutdownCalls() == 1 );
                    REQUIRE_FALSE( host->isLoaded( ConvertProbeId ) );
                    REQUIRE( host->loadedPluginIds().isEmpty() );

                    // Whatever the worker posted before it returned changes nothing.
                    QCoreApplication::sendPostedEvents( host.get(), QEvent::MetaCall );
                    REQUIRE( probe.conversionsStarted() == 1 );
                    REQUIRE( probe.shutdownCalls() == 1 );
                }

                AND_THEN( "Loaded again, the plugin converts as before" )
                {
                    REQUIRE( host->loadPlugin( ConvertProbeId ).isEmpty() );
                    ConversionHeard again;
                    probe.openGate();
                    REQUIRE( host->startConversion( ConvertProbeId, secondInput, secondOutput,
                                                    again.report() )
                             != 0 );
                    REQUIRE( again.started == 1 );
                    REQUIRE( waitUiState( [ & ] { return again.ended(); }, 10000 ) );
                    REQUIRE( again.end->outcome == ConversionOutcome::Succeeded );
                    REQUIRE( readAll( secondOutput ) == "converted: four\n" );
                }
            }

            AND_WHEN( "The Plugin Host goes with that Conversion under way" )
            {
                host.reset();

                THEN( "It cancelled the Conversion and waited for the call to return" )
                {
                    REQUIRE( heard.ended() );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Cancelled );
                    REQUIRE( probe.cancellationsSeen() == 1 );
                    REQUIRE( probe.running() == 0 );
                    REQUIRE( probe.shutdownCalls() == 1 );
                }
            }
        }

        WHEN( "The converter fails" )
        {
            probe.openGate();
            const auto failing = files.filePath( "failing.cvx" );
            writeInput( failing, "fail\n" );
            REQUIRE( host->startConversion( ConvertProbeId, failing, output, heard.report() )
                     != 0 );

            THEN( "The Conversion finishes failed with what the converter returned" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.ended(); }, 10000 ) );
                REQUIRE( heard.end->outcome == ConversionOutcome::Failed );
                REQUIRE( heard.end->returnCode == 3 );
            }
        }

        WHEN( "Conversions of two plugins are started" )
        {
            REQUIRE( host->loadPlugin( PlainConvertProbeId ).isEmpty() );
            ConvertProbe plain( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PLAIN_PATH ) );
            const auto plainInput = files.filePath( "capture.cvxplain" );
            writeInput( plainInput, "five\nsix\n" );
            const auto plainOutput = files.filePath( "capture.cvxplain.txt" );

            REQUIRE( host->startConversion( ConvertProbeId, input, output, heard.report() ) != 0 );
            const auto plainId = host->startConversion( PlainConvertProbeId, plainInput,
                                                        plainOutput, secondHeard.report() );
            REQUIRE( plainId != 0 );
            REQUIRE( waitUiState( [ & ] { return probe.running() == 1 && plain.running() == 1; },
                                  10000 ) );

            THEN( "They run at the same time" )
            {
                REQUIRE( secondHeard.started == 1 );
                REQUIRE_FALSE( heard.ended() );
                REQUIRE_FALSE( secondHeard.ended() );

                probe.openGate();
                plain.openGate();
                REQUIRE(
                    waitUiState( [ & ] { return heard.ended() && secondHeard.ended(); }, 10000 ) );
                REQUIRE( heard.end->outcome == ConversionOutcome::Succeeded );
                REQUIRE( secondHeard.end->outcome == ConversionOutcome::Succeeded );
                REQUIRE( readAll( plainOutput ) == "converted: five\nconverted: six\n" );
                // A plain convert reports no progress.
                REQUIRE( secondHeard.progress.empty() );
            }

            AND_WHEN( "The plain one, which cannot hear a cancellation, is cancelled" )
            {
                host->cancelConversion( plainId );
                plain.openGate();
                probe.openGate();

                THEN( "Its result is discarded when the call returns: the Conversion finishes "
                      "cancelled" )
                {
                    REQUIRE( waitUiState( [ & ] { return heard.ended() && secondHeard.ended(); },
                                          10000 ) );
                    REQUIRE( secondHeard.end->outcome == ConversionOutcome::Cancelled );
                    REQUIRE( plain.conversionsReturned() == 1 );
                    REQUIRE( heard.end->outcome == ConversionOutcome::Succeeded );
                }
            }
        }

        WHEN( "The host is asked which converter handles the probe's extension" )
        {
            THEN( "The probe is it, though its kind is \"ui\", and its filter is offered" )
            {
                REQUIRE( host->converterForExtension( "cvx" ) == ConvertProbeId );
                REQUIRE( host->converterForExtension( ".CVX" ) == ConvertProbeId );
                REQUIRE( host->converterFileFilters()
                         == QStringList{ QStringLiteral( "Convert Probe (*.cvx)" ) } );
            }
        }

        WHEN( "A Conversion is asked of a plugin that is not loaded" )
        {
            THEN( "None starts" )
            {
                REQUIRE( host->startConversion( PlainConvertProbeId, input, output, heard.report() )
                         == 0 );
                REQUIRE( heard.started == 0 );
            }
        }

        if ( host ) {
            host->unloadAll();
        }
    }
}

namespace {

/// What get_log_file_encoding handed the plugin; the name copied, as it is
/// valid until the next call only.
struct EncodingRead {
    int result = 0;
    const char* encoding = nullptr;
    QString nameRead;

    QString name() const
    {
        return nameRead;
    }
};

EncodingRead readEncoding( const LogSquirlHostApi& api, void* handle, const char* filePath )
{
    EncodingRead read;
    read.result = api.get_log_file_encoding( handle, filePath, &read.encoding );
    read.nameRead = QString::fromUtf8( read.encoding );
    return read;
}

/// The calls of the encoding callback the tests register for the plugin.
struct EncodingChanges {
    std::vector<std::pair<QString, QString>> told;

    static void record( void* userData, const char* filePath, const char* encoding )
    {
        static_cast<EncodingChanges*>( userData )
            ->told.emplace_back( QString::fromUtf8( filePath ), QString::fromUtf8( encoding ) );
    }
};

} // namespace

SCENARIO( "A plugin reads which Encoding a Log File is read in through the Plugin Host",
          "[pluginhost][pluginencoding][plugins]" )
{
    GIVEN( "A Plugin Host with a fake Plugin UI Port and the probe plugin loaded" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        port.encodingInFront = QStringLiteral( "UTF-16LE" );
        port.encodingOf
            = { { QStringLiteral( "/logs/other.log" ), QStringLiteral( "windows-1252" ) } };
        PluginHost host( catalog );
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_log_file_encoding ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), register_encoding_callback ) );

        WHEN( "The plugin asks for the Encoding with no path, or an empty one" )
        {
            const auto none = readEncoding( *api, handle, nullptr );
            const auto empty = readEncoding( *api, handle, "" );

            THEN( "It gets the Encoding of the tab in front, by its IANA name" )
            {
                REQUIRE( none.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( none.name() == QStringLiteral( "UTF-16LE" ) );
                REQUIRE( empty.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( empty.name() == QStringLiteral( "UTF-16LE" ) );
                REQUIRE( port.askedForEncodingOf == QStringList{ QString(), QString() } );
            }
        }

        WHEN( "The plugin names an open Log File" )
        {
            const auto read = readEncoding( *api, handle, "/logs/other.log" );

            THEN( "It gets the Encoding of that Log File" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.name() == QStringLiteral( "windows-1252" ) );
            }
        }

        WHEN( "The plugin names a Log File no tab shows" )
        {
            const auto read = readEncoding( *api, handle, "/logs/closed.log" );

            THEN( "It is told there is no such Log File, and gets no name" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.encoding == nullptr );
            }
        }

        WHEN( "The tab in front shows no Log File" )
        {
            port.encodingInFront.reset();
            const auto read = readEncoding( *api, handle, nullptr );

            THEN( "It is told there is no Log File, and gets no name" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.encoding == nullptr );
            }
        }

        WHEN( "The plugin asks with no handle, or nowhere to put the name" )
        {
            const auto noHandle = readEncoding( *api, nullptr, nullptr );
            const auto noName = api->get_log_file_encoding( handle, nullptr, nullptr );

            THEN( "It is an invalid argument, and the port is not asked" )
            {
                REQUIRE( noHandle.result == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noHandle.encoding == nullptr );
                REQUIRE( noName == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( port.askedForEncodingOf.isEmpty() );
            }
        }

        WHEN( "The plugin asks off the UI thread" )
        {
            EncodingRead read;
            std::thread( [ & ] { read = readEncoding( *api, handle, nullptr ); } ).join();

            THEN( "The port is not asked, and the plugin is told why" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( read.encoding == nullptr );
                REQUIRE( port.askedForEncodingOf.isEmpty() );
            }
        }

        WHEN( "The host has no Plugin UI Port, as without a window" )
        {
            host.setUiPort( nullptr );

            THEN( "There is no Log File" )
            {
                REQUIRE( readEncoding( *api, handle, nullptr ).result
                         == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
            host.setUiPort( &port );
        }

        WHEN( "The plugin registered an encoding callback and a Log File's Encoding changes" )
        {
            EncodingChanges changes;
            api->register_encoding_callback( handle, &EncodingChanges::record, &changes );
            host.notifyEncodingChanged( QStringLiteral( "/logs/other.log" ),
                                        QStringLiteral( "ISO-8859-1" ) );

            THEN( "The callback is told the Log File and its new Encoding" )
            {
                REQUIRE(
                    changes.told
                    == std::vector<std::pair<QString, QString>>{
                        { QStringLiteral( "/logs/other.log" ), QStringLiteral( "ISO-8859-1" ) } } );
            }

            AND_WHEN( "The plugin removes its callback and another change comes" )
            {
                api->register_encoding_callback( handle, nullptr, nullptr );
                host.notifyEncodingChanged( QStringLiteral( "/logs/other.log" ),
                                            QStringLiteral( "UTF-8" ) );

                THEN( "It is not told" )
                {
                    REQUIRE( changes.told.size() == 1 );
                }
            }
        }

        WHEN( "The plugin registers and removes encoding callbacks from a thread of its own "
              "while Encodings change (#869)" )
        {
            SwappedCallback first{ .tag = 1 };
            SwappedCallback second{ .tag = 2 };
            std::atomic<bool> swapping = true;
            std::thread plugin( [ & ] {
                for ( int i = 0; i < CallbackSwaps; ++i ) {
                    api->register_encoding_callback( handle, &firstEncoding, &first );
                    api->register_encoding_callback( handle, &secondEncoding, &second );
                    api->register_encoding_callback( handle, nullptr, nullptr );
                }
                swapping = false;
            } );
            while ( swapping ) {
                host.notifyEncodingChanged( QStringLiteral( "/logs/other.log" ),
                                            QStringLiteral( "UTF-8" ) );
            }
            plugin.join();
            const auto callsWhileSwapping = first.calls + second.calls;
            host.notifyEncodingChanged( QStringLiteral( "/logs/other.log" ),
                                        QStringLiteral( "UTF-8" ) );

            THEN( "Each call has its own callback's user data, and none comes once removed" )
            {
                REQUIRE( first.mixed == 0 );
                REQUIRE( second.mixed == 0 );
                REQUIRE( first.calls + second.calls == callsWhileSwapping );
            }
        }

        WHEN( "The plugin registered an encoding callback and is unloaded" )
        {
            EncodingChanges changes;
            api->register_encoding_callback( handle, &EncodingChanges::record, &changes );
            host.unloadAll();
            host.notifyEncodingChanged( QStringLiteral( "/logs/other.log" ),
                                        QStringLiteral( "UTF-8" ) );

            THEN( "It is not told any more" )
            {
                REQUIRE( changes.told.empty() );
            }
        }

        host.unloadAll();
        probe.library.unload();
    }
}

namespace {

const auto StreamProbeId = QStringLiteral( "io.github.logsquirl.test.stream-probe" );

/// Writes a plugin.json for a data source plugin into the subdirectory directory of root.
void installDataSource( const QString& root, const QString& directory, const QString& id,
                        const QString& name, const QString& library )
{
    const auto pluginDir = QDir( root ).filePath( directory );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "%2",
        "version": "1.0.0",
        "type": "datasource",
        "library": "%3",
        "api_version": 1
    })" )
                        .arg( id, name, library )
                        .toUtf8() );
}

/// Writes a plugin.json for the stream probe, built from library, into its own subdirectory of
/// root.
void installStreamProbe( const QString& root,
                         const QString& library = QStringLiteral( LOGSQUIRL_STREAM_PROBE_PATH ) )
{
    installDataSource( root, QStringLiteral( "stream-probe" ), StreamProbeId,
                       QStringLiteral( "Stream Probe" ), library );
}

/// The exported functions of the stream probe the tests read behind the host's back.
struct StreamProbe {
    explicit StreamProbe( const QString& path = QStringLiteral( LOGSQUIRL_STREAM_PROBE_PATH ) )
        : library( path )
    {
        REQUIRE( library.load() );
    }

    StreamProbe( const StreamProbe& ) = delete;
    StreamProbe& operator=( const StreamProbe& ) = delete;

    std::uint64_t linesPushed()
    {
        using Fn = std::uint64_t ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_stream_probe_lines_pushed" ) )();
    }

    int shutdownCalls()
    {
        using Fn = int ( * )();
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_stream_probe_shutdown_calls" ) )();
    }

    void reset()
    {
        using Fn = void ( * )();
        reinterpret_cast<Fn>( library.resolve( "logsquirl_stream_probe_reset" ) )();
    }

    void signalEosFromThread()
    {
        using Fn = void ( * )();
        reinterpret_cast<Fn>(
            library.resolve( "logsquirl_stream_probe_signal_eos_from_thread" ) )();
    }

    QLibrary library;
};

/// What the host said of the probe's streams, whether the probe had been
/// shut down by the time it said the stream stopped, and on which thread it
/// said so last.
struct StreamsHeard {
    int stopped = 0;
    bool stoppedBeforeShutdown = false;
    QThread* stoppedOn = nullptr;
};

} // namespace

SCENARIO( "A streaming plugin is shut down before its stream goes away",
          "[pluginhost][plugins][datasource]" )
{
    GIVEN( "A Plugin Host with the stream probe in its catalog, a data source whose thread pushes "
           "lines until its shutdown returns" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installStreamProbe( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        auto host = std::make_unique<PluginHost>( catalog );
        host->setUiPort( &port );

        StreamProbe probe;
        StreamsHeard heard;
        // Connected without a receiver: the slot runs on whichever thread
        // the host says it on.
        QObject::connect( host.get(), &PluginHost::dataSourceStopped, [ & ] {
            ++heard.stopped;
            heard.stoppedOn = QThread::currentThread();
            if ( probe.shutdownCalls() == 0 ) {
                heard.stoppedBeforeShutdown = true;
            }
        } );

        // Loads the probe and starts its stream, which its thread is pushing into.
        const auto startStreaming = [ & ]() -> QString {
            probe.reset();
            REQUIRE( host->loadPlugin( StreamProbeId ).isEmpty() );
            REQUIRE( host->startDataSource( StreamProbeId ).isEmpty() );
            auto* stream = host->streamWriter( StreamProbeId );
            REQUIRE( stream != nullptr );
            const auto path = stream->filePath();
            REQUIRE( QFile::exists( path ) );
            // Lines are arriving: the pusher is past the host's set-up.
            const auto pushedSoFar = probe.linesPushed();
            REQUIRE(
                waitUiState( [ & ] { return probe.linesPushed() > pushedSoFar + 300; }, 10000 ) );
            return path;
        };

        WHEN( "The plugin is unloaded while its lines arrive, over and over" )
        {
            constexpr int Rounds = 20;
            for ( int round = 0; round < Rounds; ++round ) {
                const auto path = startStreaming();
                host->unloadPlugin( StreamProbeId );
                REQUIRE( probe.shutdownCalls() == 1 );
                REQUIRE_FALSE( host->isLoaded( StreamProbeId ) );
                REQUIRE_FALSE( QFile::exists( path ) );
                REQUIRE( heard.stopped == round + 1 );
            }

            THEN( "Each time the plugin was shut down, and its thread joined, before the host "
                  "ended the stream and removed its file" )
            {
                REQUIRE( heard.stopped == Rounds );
                REQUIRE_FALSE( heard.stoppedBeforeShutdown );
            }
        }

        WHEN( "Its data source is stopped while its lines arrive" )
        {
            const auto path = startStreaming();
            const auto pushedBefore = probe.linesPushed();
            host->stopDataSource( StreamProbeId );

            THEN( "The stream ended and its file is gone, the plugin stays loaded, and what its "
                  "thread pushes from then on is dropped" )
            {
                REQUIRE( heard.stopped == 1 );
                REQUIRE_FALSE( QFile::exists( path ) );
                REQUIRE( host->isLoaded( StreamProbeId ) );
                REQUIRE( host->streamWriter( StreamProbeId ) == nullptr );
                REQUIRE( probe.shutdownCalls() == 0 );
                REQUIRE( waitUiState( [ & ] { return probe.linesPushed() > pushedBefore + 300; },
                                      10000 ) );

                AND_WHEN( "It is unloaded" )
                {
                    host->unloadPlugin( StreamProbeId );

                    THEN( "It was shut down once, and the host said nothing more of the stream" )
                    {
                        REQUIRE( probe.shutdownCalls() == 1 );
                        REQUIRE( heard.stopped == 1 );
                    }
                }
            }
        }

        // The stopped signal reaches its receivers on the host's thread
        // whichever thread the plugin signals the end of its stream from (#843).
        WHEN( "The plugin signals the end of its stream from a thread of its own" )
        {
            const auto path = startStreaming();
            probe.signalEosFromThread();

            THEN( "The host says the stream stopped on its own thread, once the event loop "
                  "turns, and the plugin stays loaded" )
            {
                REQUIRE( heard.stopped == 0 );
                REQUIRE( waitUiState( [ & ] { return heard.stopped == 1; }, 5000 ) );
                REQUIRE( heard.stoppedOn == QThread::currentThread() );
                REQUIRE( host->isLoaded( StreamProbeId ) );
                REQUIRE( probe.shutdownCalls() == 0 );
                REQUIRE( QFile::exists( path ) );
            }
        }

        WHEN( "The plugin signals the end of its stream from a thread of its own and is unloaded "
              "before the host's thread hears of it" )
        {
            startStreaming();
            probe.signalEosFromThread();
            host->unloadPlugin( StreamProbeId );

            THEN( "The stream stopped once, with the unload, and the end the plugin signalled "
                  "is dropped" )
            {
                REQUIRE( heard.stopped == 1 );
                REQUIRE( heard.stoppedOn == QThread::currentThread() );
                turnFor( 50 );
                REQUIRE( heard.stopped == 1 );
            }
        }

        WHEN( "The Plugin Host goes while its lines arrive" )
        {
            const auto path = startStreaming();
            host.reset();

            THEN( "The plugin was shut down, and its thread joined, before the host ended the "
                  "stream and removed its file" )
            {
                REQUIRE( probe.shutdownCalls() == 1 );
                REQUIRE( heard.stopped == 1 );
                REQUIRE_FALSE( heard.stoppedBeforeShutdown );
                REQUIRE_FALSE( QFile::exists( path ) );
            }
        }
    }
}

SCENARIO( "A data source without start and stop starts and stops as before",
          "[pluginhost][plugins][datasource]" )
{
    // The stream probe exports neither entry point: built against the current
    // header, and against the header as 26.12.0 published it (#855).
    for ( const auto& library : { QStringLiteral( LOGSQUIRL_STREAM_PROBE_PATH ),
                                  QStringLiteral( LOGSQUIRL_STREAM_PROBE_API2612_PATH ) } ) {
        INFO( library.toStdString() );
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installStreamProbe( pluginRoot.path(), library );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        PluginHost host( catalog );
        host.setUiPort( &port );

        StreamProbe probe( library );
        probe.reset();
        int started = 0;
        int stopped = 0;
        QObject::connect( &host, &PluginHost::dataSourceStarted, [ & ] { ++started; } );
        QObject::connect( &host, &PluginHost::dataSourceStopped, [ & ] { ++stopped; } );

        REQUIRE( host.loadPlugin( StreamProbeId ).isEmpty() );
        REQUIRE( host.startDataSource( StreamProbeId ).isEmpty() );
        REQUIRE( started == 1 );
        REQUIRE( host.streamWriter( StreamProbeId ) != nullptr );
        REQUIRE_FALSE( host.startDataSource( StreamProbeId ).isEmpty() );
        REQUIRE( started == 1 );

        host.stopDataSource( StreamProbeId );
        REQUIRE( stopped == 1 );
        REQUIRE( host.streamWriter( StreamProbeId ) == nullptr );
        REQUIRE( host.isLoaded( StreamProbeId ) );

        REQUIRE( host.startDataSource( StreamProbeId ).isEmpty() );
        REQUIRE( started == 2 );
        host.unloadPlugin( StreamProbeId );
        REQUIRE( stopped == 2 );
        REQUIRE( probe.shutdownCalls() == 1 );
    }
}

namespace {

const auto LifecycleProbeId = QStringLiteral( "io.github.logsquirl.test.lifecycle-probe" );

/// The exported functions of the lifecycle probe the tests use behind the host's back.
struct LifecycleProbe {
    using Hook = void ( * )( void* userData );

    LifecycleProbe()
        : library( QStringLiteral( LOGSQUIRL_LIFECYCLE_PROBE_PATH ) )
    {
        REQUIRE( library.load() );
    }

    LifecycleProbe( const LifecycleProbe& ) = delete;
    LifecycleProbe& operator=( const LifecycleProbe& ) = delete;

    /// What the probe wrote down, comma-separated.
    QString events()
    {
        using Fn = const char* ( * )();
        return QString::fromUtf8(
            reinterpret_cast<Fn>( library.resolve( "logsquirl_lifecycle_probe_events" ) )() );
    }

    void note( const char* event )
    {
        using Fn = void ( * )( const char* );
        reinterpret_cast<Fn>( library.resolve( "logsquirl_lifecycle_probe_note" ) )( event );
    }

    void setStartResult( int result )
    {
        using Fn = void ( * )( int );
        reinterpret_cast<Fn>( library.resolve( "logsquirl_lifecycle_probe_set_start_result" ) )(
            result );
    }

    size_t contextSize()
    {
        using Fn = size_t ( * )();
        return reinterpret_cast<Fn>(
            library.resolve( "logsquirl_lifecycle_probe_context_size" ) )();
    }

    void setHooks( Hook onStart, Hook onStop, Hook onMenu, void* userData )
    {
        using Fn = void ( * )( Hook, Hook, Hook, void* );
        reinterpret_cast<Fn>( library.resolve( "logsquirl_lifecycle_probe_set_hooks" ) )(
            onStart, onStop, onMenu, userData );
    }

    void reset()
    {
        using Fn = void ( * )();
        reinterpret_cast<Fn>( library.resolve( "logsquirl_lifecycle_probe_reset" ) )();
    }

    QLibrary library;
};

/// What runs inside the probe's start or stop, as a dialog of a plugin's would.
struct InsidePluginCall {
    PluginHost* host = nullptr;
    std::function<void()> work;
    QThread* ranOn = nullptr;

    static void run( void* userData )
    {
        auto* self = static_cast<InsidePluginCall*>( userData );
        self->ranOn = QThread::currentThread();
        if ( self->work ) {
            self->work();
        }
    }
};

} // namespace

SCENARIO( "A data source plugin hears when its stream starts and stops",
          "[pluginhost][plugins][datasource]" )
{
    GIVEN( "A Plugin Host with the lifecycle probe loaded, which exports start and stop" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installDataSource( pluginRoot.path(), QStringLiteral( "lifecycle-probe" ), LifecycleProbeId,
                           QStringLiteral( "Lifecycle Probe" ),
                           QStringLiteral( LOGSQUIRL_LIFECYCLE_PROBE_PATH ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        FakePluginUiPort port;
        auto host = std::make_unique<PluginHost>( catalog );
        host->setUiPort( &port );

        LifecycleProbe probe;
        probe.reset();
        // What the host says goes into the probe's own list, so one list
        // shows the order of the host's calls and signals.
        QObject::connect( host.get(), &PluginHost::dataSourceStarted,
                          [ & ] { probe.note( "started" ); } );
        QObject::connect( host.get(), &PluginHost::dataSourceStopped,
                          [ & ] { probe.note( "stopped" ); } );

        InsidePluginCall inside;
        inside.host = host.get();
        probe.setHooks( &InsidePluginCall::run, &InsidePluginCall::run, nullptr, &inside );

        REQUIRE( host->loadPlugin( LifecycleProbeId ).isEmpty() );

        WHEN( "The user starts the data source" )
        {
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );

            THEN( "The plugin's start ran on the UI thread with a context of the host's size, "
                  "before the stream counted as started, and what it pushed meanwhile is in the "
                  "stream" )
            {
                REQUIRE( probe.events() == "init,start,started" );
                REQUIRE( inside.ranOn == QThread::currentThread() );
                REQUIRE( probe.contextSize() == sizeof( LogSquirlDataSourceContext ) );
                auto* stream = host->streamWriter( LifecycleProbeId );
                REQUIRE( stream != nullptr );
                REQUIRE( readAll( stream->filePath() ) == "lifecycle probe opened\n" );
            }

            AND_WHEN( "The user starts it again while it runs" )
            {
                const auto error = host->startDataSource( LifecycleProbeId );

                THEN( "The host refuses, and the plugin hears nothing of it" )
                {
                    REQUIRE_FALSE( error.isEmpty() );
                    REQUIRE( probe.events() == "init,start,started" );
                }
            }

            AND_WHEN( "The user stops it" )
            {
                QByteArray inStreamDuringStop;
                inside.ranOn = nullptr;
                inside.work = [ & ] {
                    inStreamDuringStop
                        = readAll( host->streamWriter( LifecycleProbeId )->filePath() );
                };
                host->stopDataSource( LifecycleProbeId );

                THEN( "The plugin's stop ran once on the UI thread, with the stream still "
                      "taking its last line, before the stream ended; the plugin stays loaded" )
                {
                    REQUIRE( probe.events() == "init,start,started,stop,stopped" );
                    REQUIRE( inside.ranOn == QThread::currentThread() );
                    REQUIRE( probe.contextSize() == sizeof( LogSquirlDataSourceContext ) );
                    REQUIRE( inStreamDuringStop
                             == "lifecycle probe opened\nlifecycle probe closing\n" );
                    REQUIRE( host->streamWriter( LifecycleProbeId ) == nullptr );
                    REQUIRE( host->isLoaded( LifecycleProbeId ) );
                }

                AND_WHEN( "The user stops it again, and then unloads the plugin" )
                {
                    host->stopDataSource( LifecycleProbeId );
                    host->unloadPlugin( LifecycleProbeId );

                    THEN( "The plugin was stopped only once" )
                    {
                        REQUIRE( probe.events() == "init,start,started,stop,stopped,shutdown" );
                    }
                }

                AND_WHEN( "The user starts it once more" )
                {
                    REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );

                    THEN( "The plugin starts again" )
                    {
                        REQUIRE( probe.events()
                                 == "init,start,started,stop,stopped,start,started" );
                    }
                }
            }

            AND_WHEN( "The plugin is unloaded" )
            {
                host->unloadPlugin( LifecycleProbeId );

                THEN( "It is stopped once, then shut down, and then the stream ends" )
                {
                    REQUIRE( probe.events() == "init,start,started,stop,shutdown,stopped" );
                }
            }

            AND_WHEN( "The Plugin Host goes, as LogSquirl quits" )
            {
                host.reset();

                THEN( "The plugin is stopped once, then shut down, and then the stream ends" )
                {
                    REQUIRE( probe.events() == "init,start,started,stop,shutdown,stopped" );
                }
            }
        }

        WHEN( "The plugin's start fails" )
        {
            probe.setStartResult( LOGSQUIRL_DATASOURCE_FAILED );
            const auto error = host->startDataSource( LifecycleProbeId );

            THEN( "The start is refused with the plugin's code, no stream remains and nothing "
                  "said it started" )
            {
                REQUIRE( error.contains( QStringLiteral( "code 1" ) ) );
                REQUIRE( probe.events() == "init,start" );
                REQUIRE( host->streamWriter( LifecycleProbeId ) == nullptr );
            }

            AND_WHEN( "The plugin is unloaded" )
            {
                host->unloadPlugin( LifecycleProbeId );

                THEN( "It is not stopped, and no stream ends" )
                {
                    REQUIRE( probe.events() == "init,start,shutdown" );
                }
            }

            AND_WHEN( "The user starts it again, and it lets the stream run" )
            {
                probe.setStartResult( LOGSQUIRL_DATASOURCE_STARTED );
                REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );

                THEN( "It starts" )
                {
                    REQUIRE( probe.events() == "init,start,start,started" );
                }
            }
        }

        WHEN( "The plugin's start returns a code of its own" )
        {
            probe.setStartResult( 42 );
            const auto error = host->startDataSource( LifecycleProbeId );

            THEN( "It failed, with that code" )
            {
                REQUIRE( error.contains( QStringLiteral( "code 42" ) ) );
                REQUIRE( host->streamWriter( LifecycleProbeId ) == nullptr );
                host.reset();
                REQUIRE( probe.events() == "init,start,shutdown" );
            }
        }

        WHEN( "The user cancels in the plugin's start" )
        {
            probe.setStartResult( LOGSQUIRL_DATASOURCE_CANCELLED );
            const auto error = host->startDataSource( LifecycleProbeId );

            THEN( "Nothing is reported, no stream remains, and the plugin is not stopped" )
            {
                REQUIRE( error.isEmpty() );
                REQUIRE( host->streamWriter( LifecycleProbeId ) == nullptr );
                host->stopDataSource( LifecycleProbeId );
                host.reset();
                REQUIRE( probe.events() == "init,start,shutdown" );
            }
        }

        WHEN( "The data source is started again from inside the plugin's start" )
        {
            QString nestedError;
            inside.work = [ & ] { nestedError = host->startDataSource( LifecycleProbeId ); };
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );

            THEN( "The second start is refused without reaching the plugin" )
            {
                REQUIRE_FALSE( nestedError.isEmpty() );
                REQUIRE( probe.events() == "init,start,started" );
            }
        }

        WHEN( "The data source is stopped from inside the plugin's start" )
        {
            inside.work = [ & ] { host->stopDataSource( LifecycleProbeId ); };
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );

            THEN( "The stop does nothing, and the stream starts" )
            {
                REQUIRE( probe.events() == "init,start,started" );
                REQUIRE( host->streamWriter( LifecycleProbeId ) != nullptr );
            }
        }

        WHEN( "The plugin is unloaded from inside its start, which lets the stream run" )
        {
            inside.work = [ & ] { host->unloadPlugin( LifecycleProbeId ); };
            const auto error = host->startDataSource( LifecycleProbeId );

            THEN( "Nothing said it started, and once the call has returned the plugin is "
                  "stopped, shut down and its stream ended" )
            {
                REQUIRE_FALSE( error.isEmpty() );
                REQUIRE( probe.events() == "init,start" );
                REQUIRE( waitUiState(
                    [ & ] { return probe.events() == "init,start,stop,shutdown,stopped"; },
                    5000 ) );
                REQUIRE( host->streamWriter( LifecycleProbeId ) == nullptr );
            }
        }

        WHEN( "The user stops the data source and the plugin is unloaded from inside its stop" )
        {
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );
            inside.work = [ & ] { host->unloadPlugin( LifecycleProbeId ); };
            host->stopDataSource( LifecycleProbeId );

            THEN( "The stream ends once the stop has returned, and the plugin is shut down "
                  "after, stopped once" )
            {
                REQUIRE( probe.events() == "init,start,started,stop,stopped" );
                REQUIRE( waitUiState(
                    [ & ] { return probe.events() == "init,start,started,stop,stopped,shutdown"; },
                    5000 ) );
            }
        }

        // The menu action of the plugin's, as the host handed it to the port:
        // triggering it is a call into the plugin, which may show a dialog.
        const auto triggerMenuAction = [ & ] {
            const auto action
                = std::ranges::find( port.calls, PortCall::Kind::AddMenuAction, &PortCall::kind );
            REQUIRE( action != port.calls.end() );
            action->callback( action->userData );
        };

        WHEN( "The plugin is unloaded from inside a menu action of its own while its stream "
              "runs" )
        {
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );
            inside.work = [ & ] { host->unloadPlugin( LifecycleProbeId ); };
            probe.setHooks( nullptr, nullptr, &InsidePluginCall::run, &inside );
            triggerMenuAction();

            THEN( "Once the menu action has returned, the plugin is stopped once, shut down, and "
                  "its stream ended" )
            {
                REQUIRE( probe.events() == "init,start,started" );
                REQUIRE( waitUiState(
                    [ & ] { return probe.events() == "init,start,started,stop,shutdown,stopped"; },
                    5000 ) );
            }
        }

        WHEN( "The plugin is unloaded from inside a menu action of its own, and its stream is "
              "stopped there too" )
        {
            REQUIRE( host->startDataSource( LifecycleProbeId ).isEmpty() );
            inside.work = [ & ] {
                host->unloadPlugin( LifecycleProbeId );
                host->stopDataSource( LifecycleProbeId );
            };
            probe.setHooks( nullptr, nullptr, &InsidePluginCall::run, &inside );
            triggerMenuAction();

            THEN( "The plugin, not shut down yet, is stopped before its stream ends, and shut "
                  "down once the menu action has returned, stopped once" )
            {
                REQUIRE( probe.events() == "init,start,started,stop,stopped" );
                REQUIRE( waitUiState(
                    [ & ] { return probe.events() == "init,start,started,stop,stopped,shutdown"; },
                    5000 ) );
            }
        }

        // What a section set to run inside the plugin refers to its locals.
        inside.work = nullptr;
        if ( host ) {
            host->unloadAll();
        }
    }
}

namespace {

/// An open Log File whose Log Lines are held in memory, read as the UI reads
/// a Log File's: at most maxLines of them, none after one cut for the bytes
/// or after their bytes passed the bound. whileReading, set before any read,
/// runs inside every read, on the reading thread.
class FakeLogFile final : public PluginLogFileSource {
public:
    explicit FakeLogFile( QStringList lines )
        : lines_( std::move( lines ) )
    {
    }

    std::uint64_t lineCount() const override
    {
        return static_cast<std::uint64_t>( lines_.size() );
    }

    std::optional<PluginSelectedLogLines> readLogLines( std::uint64_t logLine, std::size_t maxLines,
                                                        std::size_t maxBytes ) const override
    {
        if ( whileReading ) {
            whileReading();
        }
        if ( logLine >= lineCount() ) {
            return std::nullopt;
        }
        PluginSelectedLogLines read;
        std::size_t bytes = 0;
        for ( auto line = logLine;
              line < lineCount() && static_cast<std::size_t>( read.lines.size() ) < maxLines;
              ++line ) {
            const auto utf8 = lines_[ static_cast<qsizetype>( line ) ].toUtf8();
            const auto cut = static_cast<std::size_t>( utf8.size() ) > maxBytes;
            read.lines.append(
                QString::fromUtf8( cut ? utf8.left( static_cast<qsizetype>( maxBytes ) ) : utf8 ) );
            read.lastCut = cut;
            bytes += ( read.lines.size() > 1 ? 1 : 0 ) + static_cast<std::size_t>( utf8.size() );
            if ( cut || bytes > maxBytes ) {
                read.more = line + 1 < lineCount()
                            && static_cast<std::size_t>( read.lines.size() ) < maxLines;
                break;
            }
        }
        return read;
    }

    std::optional<PluginLogFormatFields> logFormat() const override
    {
        return format;
    }

    std::expected<PluginLogLineFields, PluginLogLineFieldsUnread>
    readLogLineFields( std::uint64_t logLine, std::size_t maxLines,
                       std::size_t maxBytes ) const override
    {
        if ( whileReading ) {
            whileReading();
        }
        if ( !format ) {
            return std::unexpected( PluginLogLineFieldsUnread::NoLogFormat );
        }
        if ( logLine >= lineCount() ) {
            return std::unexpected( PluginLogLineFieldsUnread::OutOfRange );
        }
        PluginLogLineFields read;
        const auto fieldCount = format->fieldNames.size();
        read.fieldCount = static_cast<std::size_t>( fieldCount );
        std::size_t bytes = 0;
        for ( auto line = logLine; line < lineCount() && read.parsed.size() < maxLines; ++line ) {
            const auto fields = lines_[ static_cast<qsizetype>( line ) ].split( '|' );
            const auto parsed = fields.size() == fieldCount;
            read.parsed.push_back( parsed );
            for ( qsizetype field = 0; field < fieldCount; ++field ) {
                auto value = parsed ? fields[ field ].toUtf8() : QByteArray{};
                bytes += static_cast<std::size_t>( value.size() );
                read.values.push_back( std::move( value ) );
            }
            if ( bytes > maxBytes ) {
                read.more = line + 1 < lineCount() && read.parsed.size() < maxLines;
                break;
            }
        }
        return read;
    }

    std::function<void()> whileReading;
    /// The Log Format its fields are read under, none by default: a Log Line
    /// is parsed when '|' splits it into as many fields as the Log Format has.
    std::optional<PluginLogFormatFields> format;

private:
    QStringList lines_;
};

/// Log Lines "line 1" up to "line count", as the views number them.
QStringList numberedLines( int count )
{
    QStringList lines;
    for ( int number = 1; number <= count; ++number ) {
        lines.append( QStringLiteral( "line %1" ).arg( number ) );
    }
    return lines;
}

/// The Log Lines from first up to last, as the views number them, joined as get_log_lines joins
/// them.
QString numberedText( int first, int last )
{
    return numberedLines( last ).mid( first - 1 ).join( '\n' );
}

/// What get_log_lines handed back, and the text copied as it came: the host's
/// text is valid until the next call on the same thread only.
struct LogLinesRead {
    int result = 0;
    const char* text = nullptr;
    size_t length = 0;
    size_t lineCount = 0;
    QString copied{};
    bool terminated = false;

    /// The text where the host keeps it, as it is now.
    QString textNow() const
    {
        return text != nullptr ? QString::fromUtf8( text, static_cast<qsizetype>( length ) )
                               : QString();
    }
};

LogLinesRead readLogLines( const LogSquirlHostApi& api, void* handle, const QString& path,
                           uint64_t firstLine, size_t maxLines )
{
    const auto pathUtf8 = path.toUtf8();
    LogLinesRead read;
    read.result = api.get_log_lines( handle, pathUtf8.constData(), firstLine, maxLines, &read.text,
                                     &read.length, &read.lineCount );
    read.copied = read.textNow();
    read.terminated = read.text != nullptr && read.text[ read.length ] == '\0';
    return read;
}

std::pair<int, uint64_t> countLogLines( const LogSquirlHostApi& api, void* handle,
                                        const QString& path )
{
    const auto pathUtf8 = path.toUtf8();
    uint64_t count = 99;
    const auto result = api.get_log_line_count( handle, pathUtf8.constData(), &count );
    return { result, count };
}

/// The Log Lines changes the callback a test registers was told, in order.
struct LogLinesChanges {
    struct Change {
        QString path;
        uint64_t lineCount = 0;
        int change = -1;

        bool operator==( const Change& ) const = default;
    };
    std::vector<Change> told;

    static void record( void* userData, const char* filePath, uint64_t lineCount, int change )
    {
        static_cast<LogLinesChanges*>( userData )
            ->told.push_back( { QString::fromUtf8( filePath ), lineCount, change } );
    }
};

} // namespace

SCENARIO( "A plugin reads the Log Lines of an open Log File by path, on any thread, through the "
          "Plugin Host",
          "[pluginhost][pluginloglines][plugins]" )
{
    GIVEN( "A Plugin Host with the probe plugin loaded, and a Log File of 3000 Log Lines open" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );
        QTemporaryDir logDir;
        REQUIRE( logDir.isValid() );
        const auto path = logDir.filePath( "app.log" );
        {
            QFile file( path );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
        }
        const auto closedPath = logDir.filePath( "closed.log" );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_log_line_count ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_log_lines ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), register_log_lines_callback ) );

        auto logFile = std::make_shared<FakeLogFile>( numberedLines( 3000 ) );
        const std::weak_ptr<FakeLogFile> heldLogFile = logFile;
        const auto& logFiles = host.logFiles();
        const auto id = logFiles->add( path, std::move( logFile ) );

        WHEN( "The plugin counts its Log Lines, by its path and by another spelling of it" )
        {
            const auto byPath = countLogLines( *api, handle, path );
            const auto spelledOtherwise
                = countLogLines( *api, handle, logDir.path() + QStringLiteral( "/./app.log" ) );

            THEN( "It gets the count of both" )
            {
                REQUIRE( byPath == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, 3000 } );
                REQUIRE( spelledOtherwise
                         == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, 3000 } );
            }
        }

        WHEN( "The plugin reads the first Log Lines, some in the middle, the last ones and past "
              "the end" )
        {
            const auto first = readLogLines( *api, handle, path, 1, 3 );
            const auto middle = readLogLines( *api, handle, path, 1500, 2 );
            const auto last = readLogLines( *api, handle, path, 2999, 5 );
            const auto pastEnd = readLogLines( *api, handle, path, 3001, 5 );
            const auto zero = readLogLines( *api, handle, path, 0, 5 );

            THEN( "It gets them numbered as the views number them, and no text past the end" )
            {
                REQUIRE( first.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( first.lineCount == 3 );
                REQUIRE( first.copied == numberedText( 1, 3 ) );
                REQUIRE( first.terminated );
                REQUIRE( middle.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( middle.copied == numberedText( 1500, 1501 ) );
                // Fewer than asked for, but every one there is: whole.
                REQUIRE( last.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( last.lineCount == 2 );
                REQUIRE( last.copied == numberedText( 2999, 3000 ) );
                REQUIRE( pastEnd.result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( pastEnd.text == nullptr );
                REQUIRE( pastEnd.lineCount == 0 );
                REQUIRE( zero.result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
            }
        }

        WHEN( "The plugin asks for none" )
        {
            const auto none = readLogLines( *api, handle, path, 10, 0 );

            THEN( "It gets an empty text" )
            {
                REQUIRE( none.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( none.text != nullptr );
                REQUIRE( none.length == 0 );
                REQUIRE( none.lineCount == 0 );
            }
        }

        WHEN( "The plugin asks for more Log Lines than one call returns" )
        {
            const auto many = readLogLines( *api, handle, path, 1, 2500 );
            const auto exactly
                = readLogLines( *api, handle, path, 2001, LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
            const auto rest = readLogLines( *api, handle, path, 2001, 2500 );

            THEN( "It gets the first ones that fit, whole, and is told there are more" )
            {
                REQUIRE( many.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( many.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( many.copied == numberedText( 1, 1000 ) );
                REQUIRE( exactly.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( exactly.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                // As many as are left, though more were asked for.
                REQUIRE( rest.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( rest.copied == numberedText( 2001, 3000 ) );
            }
        }

        WHEN( "The plugin asks for more bytes than one call returns" )
        {
            // 600 KiB each: two do not fit into 1 MiB, and a third is longer still.
            const QString wide( 600 * 1024, QChar( 'w' ) );
            const QString wider( LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES + 10, QChar( 'x' ) );
            const auto widePath = logDir.filePath( "wide.log" );
            {
                QFile file( widePath );
                REQUIRE( file.open( QIODevice::WriteOnly ) );
            }
            const auto wideId = logFiles->add(
                widePath, std::make_shared<FakeLogFile>( QStringList{ wide, wide, wider } ) );
            const auto two = readLogLines( *api, handle, widePath, 1, 3 );
            const auto cut = readLogLines( *api, handle, widePath, 3, 1 );
            logFiles->remove( wideId );

            THEN( "It gets whole Log Lines, but for a first one longer than that, cut" )
            {
                REQUIRE( two.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( two.lineCount == 1 );
                REQUIRE( two.copied == wide );
                REQUIRE( cut.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( cut.lineCount == 1 );
                REQUIRE( cut.length == LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
            }
        }

        WHEN( "The plugin names a Log File no tab shows, an empty path or none" )
        {
            const auto closedCount = countLogLines( *api, handle, closedPath );
            const auto closedRead = readLogLines( *api, handle, closedPath, 1, 1 );
            const auto emptyRead = readLogLines( *api, handle, QString(), 1, 1 );
            const char* text = nullptr;
            uint64_t count = 0;
            const auto noPathRead
                = api->get_log_lines( handle, nullptr, 1, 1, &text, nullptr, nullptr );
            const auto noPathCount = api->get_log_line_count( handle, nullptr, &count );

            THEN( "There is no such Log File, and a missing path is an invalid argument" )
            {
                REQUIRE( closedCount.first == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( closedCount.second == 0 );
                REQUIRE( closedRead.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( closedRead.text == nullptr );
                REQUIRE( emptyRead.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( noPathRead == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noPathCount == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
            }
        }

        WHEN( "The plugin asks with no handle, or nowhere to put the answer" )
        {
            const auto pathUtf8 = path.toUtf8();
            const auto noHandle = readLogLines( *api, nullptr, path, 1, 1 );
            const auto noText = api->get_log_lines( handle, pathUtf8.constData(), 1, 1, nullptr,
                                                    nullptr, nullptr );
            const auto noCount = api->get_log_line_count( handle, pathUtf8.constData(), nullptr );

            THEN( "It is an invalid argument" )
            {
                REQUIRE( noHandle.result == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noText == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noCount == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
            }
        }

        WHEN( "The plugin reads on a thread of its own while the UI thread waits for that thread" )
        {
            // The UI thread is busy: it does nothing but wait for the reads.
            LogLinesRead read;
            std::pair<int, uint64_t> counted;
            std::thread( [ & ] {
                counted = countLogLines( *api, handle, path );
                read = readLogLines( *api, handle, path, 10, 2 );
            } ).join();

            THEN( "Both answer without the UI thread" )
            {
                REQUIRE( counted == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, 3000 } );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.copied == numberedText( 10, 11 ) );
            }
        }

        WHEN( "Two threads of the plugin read one after the other" )
        {
            std::promise<void> firstRead;
            std::promise<void> secondRead;
            QString firstText;
            QString firstTextAfterwards;
            QString secondText;
            const char* firstBuffer = nullptr;
            const char* secondBuffer = nullptr;
            std::thread first( [ & ] {
                const auto read = readLogLines( *api, handle, path, 1, 2 );
                firstBuffer = read.text;
                firstText = read.copied;
                firstRead.set_value();
                secondRead.get_future().wait();
                // Still this thread's text, though the other thread read since.
                firstTextAfterwards = read.textNow();
            } );
            std::thread second( [ & ] {
                firstRead.get_future().wait();
                const auto read = readLogLines( *api, handle, path, 100, 3 );
                secondBuffer = read.text;
                secondText = read.copied;
                secondRead.set_value();
            } );
            first.join();
            second.join();

            THEN( "Each keeps its own text until it reads again" )
            {
                REQUIRE( firstBuffer != secondBuffer );
                REQUIRE( firstText == numberedText( 1, 2 ) );
                REQUIRE( firstTextAfterwards == numberedText( 1, 2 ) );
                REQUIRE( secondText == numberedText( 100, 102 ) );
            }
        }

        WHEN( "The tab of the Log File closes while a thread of the plugin reads it" )
        {
            std::promise<void> reading;
            std::promise<void> closed;
            auto closedFuture = closed.get_future().share();
            // Through the hold the host keeps: the test's own goes.
            heldLogFile.lock()->whileReading = [ &reading, closedFuture ] {
                reading.set_value();
                closedFuture.wait();
            };
            LogLinesRead read;
            std::thread reader( [ & ] { read = readLogLines( *api, handle, path, 1, 2 ); } );
            reading.get_future().wait();
            logFiles->remove( id );
            const auto heldWhileReading = !heldLogFile.expired();
            closed.set_value();
            reader.join();

            THEN( "The read finishes on the Log File's data, and says there is no Log File" )
            {
                REQUIRE( heldWhileReading );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.text == nullptr );
                REQUIRE( heldLogFile.expired() );
            }

            AND_WHEN( "The plugin reads it again" )
            {
                THEN( "There is no Log File" )
                {
                    REQUIRE( countLogLines( *api, handle, path ).first
                             == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                    REQUIRE( readLogLines( *api, handle, path, 1, 1 ).result
                             == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                }
            }
        }

        WHEN( "The plugin registered a log lines callback and the Log Lines change" )
        {
            LogLinesChanges changes;
            api->register_log_lines_callback( handle, &LogLinesChanges::record, &changes );
            host.notifyLogLinesChanged( path, 3010, PluginLogLinesChange::Appended );
            host.notifyLogLinesChanged( path, 12, PluginLogLinesChange::Replaced );

            THEN( "The callback is told the Log File, its count and how it changed" )
            {
                REQUIRE( changes.told
                         == std::vector<LogLinesChanges::Change>{
                             { path, 3010, LOGSQUIRL_LOG_LINES_APPENDED },
                             { path, 12, LOGSQUIRL_LOG_LINES_REPLACED } } );
            }

            AND_WHEN( "The plugin registers another one" )
            {
                LogLinesChanges others;
                api->register_log_lines_callback( handle, &LogLinesChanges::record, &others );
                host.notifyLogLinesChanged( path, 3020, PluginLogLinesChange::Appended );

                THEN( "Only the other one is told" )
                {
                    REQUIRE( changes.told.size() == 2 );
                    REQUIRE( others.told.size() == 1 );
                }
            }

            AND_WHEN( "The plugin removes its callback, from a thread of its own, and another "
                      "change comes" )
            {
                std::thread( [ & ] {
                    api->register_log_lines_callback( handle, nullptr, nullptr );
                } ).join();
                host.notifyLogLinesChanged( path, 3020, PluginLogLinesChange::Appended );

                THEN( "It is not told" )
                {
                    REQUIRE( changes.told.size() == 2 );
                }
            }
        }

        WHEN( "The plugin registered a log lines callback and is unloaded" )
        {
            LogLinesChanges changes;
            api->register_log_lines_callback( handle, &LogLinesChanges::record, &changes );
            host.unloadAll();
            host.notifyLogLinesChanged( path, 3010, PluginLogLinesChange::Appended );

            THEN( "It is not told any more" )
            {
                REQUIRE( changes.told.empty() );
            }
        }

        logFiles->remove( id );
        host.unloadAll();
        probe.library.unload();
    }
}

namespace {

/// What get_log_format_fields handed back, copied as it came.
struct LogFormatRead {
    int result = 0;
    bool hasName = false;
    QString name{};
    QStringList fieldNames{};
    size_t fieldCount = 99;
};

LogFormatRead readLogFormat( const LogSquirlHostApi& api, void* handle, const QString& path )
{
    const auto pathUtf8 = path.toUtf8();
    LogFormatRead read;
    const char* name = "not handed back";
    const char* const* names = nullptr;
    read.result = api.get_log_format_fields( handle, pathUtf8.constData(), &name, &names,
                                             &read.fieldCount );
    read.hasName = name != nullptr;
    read.name = QString::fromUtf8( name );
    for ( size_t field = 0; names != nullptr && field < read.fieldCount; ++field ) {
        read.fieldNames.append( QString::fromUtf8( names[ field ] ) );
    }
    return read;
}

/// What get_log_line_fields handed back, the values copied as they came: none
/// for a Log Line not parsed.
struct LogLineFieldsRead {
    int result = 0;
    const char* const* values = nullptr;
    size_t fieldCount = 99;
    size_t lineCount = 99;
    std::vector<std::optional<QStringList>> lines{};
    // Whether each value of a parsed Log Line is NUL-terminated, and each of
    // one not parsed NULL and of length 0.
    bool asDocumented = true;

    /// The values where the host keeps them, as they are now.
    QString valueNow( size_t line, size_t field ) const
    {
        return QString::fromUtf8( values[ line * fieldCount + field ] );
    }
};

LogLineFieldsRead readLogLineFields( const LogSquirlHostApi& api, void* handle, const QString& path,
                                     uint64_t firstLine, size_t maxLines )
{
    const auto pathUtf8 = path.toUtf8();
    LogLineFieldsRead read;
    const size_t* lengths = nullptr;
    const int* parsed = nullptr;
    read.result
        = api.get_log_line_fields( handle, pathUtf8.constData(), firstLine, maxLines, &read.values,
                                   &lengths, &parsed, &read.fieldCount, &read.lineCount );
    for ( size_t line = 0; line < read.lineCount; ++line ) {
        if ( parsed[ line ] == 0 ) {
            for ( size_t field = 0; field < read.fieldCount; ++field ) {
                const auto at = line * read.fieldCount + field;
                read.asDocumented
                    = read.asDocumented && read.values[ at ] == nullptr && lengths[ at ] == 0;
            }
            read.lines.emplace_back();
            continue;
        }
        QStringList values;
        for ( size_t field = 0; field < read.fieldCount; ++field ) {
            const auto at = line * read.fieldCount + field;
            read.asDocumented = read.asDocumented && parsed[ line ] == 1
                                && read.values[ at ][ lengths[ at ] ] == '\0';
            values.append(
                QString::fromUtf8( read.values[ at ], static_cast<qsizetype>( lengths[ at ] ) ) );
        }
        read.lines.emplace_back( std::move( values ) );
    }
    return read;
}

/// Log Lines "INFO|line 1" up to "INFO|line count", as the views number them.
QStringList numberedFieldLines( int count )
{
    QStringList lines;
    for ( int number = 1; number <= count; ++number ) {
        lines.append( QStringLiteral( "INFO|line %1" ).arg( number ) );
    }
    return lines;
}

} // namespace

SCENARIO( "A plugin reads the fields of Log Lines of an open Log File by path, on any thread, "
          "through the Plugin Host",
          "[pluginhost][pluginlogfields][plugins]" )
{
    GIVEN( "A Plugin Host with the probe plugin loaded, and a Log File under a Log Format with "
           "the fields level and message open" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );
        QTemporaryDir logDir;
        REQUIRE( logDir.isValid() );
        const auto path = logDir.filePath( "app.log" );
        const auto plainPath = logDir.filePath( "plain.log" );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_log_format_fields ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_log_line_fields ) );

        const auto appFormat
            = PluginLogFormatFields{ .name = QStringLiteral( "app_log" ),
                                     .fieldNames
                                     = { QStringLiteral( "level" ), QStringLiteral( "message" ) } };
        auto lines = numberedFieldLines( 3000 );
        lines[ 1 ] = QStringLiteral( "WARN|a\tb\nc" );
        lines[ 2 ] = QStringLiteral( "not parsed" );
        lines[ 3 ] = QStringLiteral( "ERROR|" );
        lines[ 4 ] = QStringLiteral( "DEBUG|é ü" );
        auto logFile = std::make_shared<FakeLogFile>( lines );
        logFile->format = appFormat;
        const std::weak_ptr<FakeLogFile> heldLogFile = logFile;
        const auto& logFiles = host.logFiles();
        const auto id = logFiles->add( path, std::move( logFile ) );
        const auto plainId = logFiles->add( plainPath, std::make_shared<FakeLogFile>( lines ) );

        WHEN( "The plugin asks for the Log Format of the Log File, and of one without" )
        {
            const auto format = readLogFormat( *api, handle, path );
            const auto none = readLogFormat( *api, handle, plainPath );

            THEN( "It gets its name and its fields in order, and that the other has none" )
            {
                REQUIRE( format.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( format.name == QStringLiteral( "app_log" ) );
                REQUIRE( format.fieldCount == 2 );
                REQUIRE( format.fieldNames == QStringList{ "level", "message" } );
                REQUIRE( none.result == LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT );
                REQUIRE_FALSE( none.hasName );
                REQUIRE( none.fieldCount == 0 );
            }
        }

        WHEN( "The plugin reads the fields of the first Log Lines" )
        {
            const auto read = readLogLineFields( *api, handle, path, 1, 5 );

            THEN( "It gets every value, a tab and a line feed too, empty ones as empty, and no "
                  "fields for the Log Line not parsed" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.fieldCount == 2 );
                REQUIRE( read.lineCount == 5 );
                REQUIRE( read.asDocumented );
                REQUIRE( read.lines
                         == std::vector<std::optional<QStringList>>{
                             QStringList{ "INFO", "line 1" }, QStringList{ "WARN", "a\tb\nc" },
                             std::nullopt, QStringList{ "ERROR", "" },
                             QStringList{ "DEBUG", QStringLiteral( "é ü" ) } } );
            }
        }

        WHEN( "The plugin reads the fields of the Log File without a Log Format, past the end, "
              "of line 0 or of one no tab shows" )
        {
            const auto none = readLogLineFields( *api, handle, plainPath, 1, 5 );
            const auto pastEnd = readLogLineFields( *api, handle, path, 3001, 5 );
            const auto zero = readLogLineFields( *api, handle, path, 0, 5 );
            const auto closed
                = readLogLineFields( *api, handle, logDir.filePath( "closed.log" ), 1, 5 );

            THEN( "It is told why it gets nothing" )
            {
                REQUIRE( none.result == LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT );
                REQUIRE( pastEnd.result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( zero.result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( closed.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( readLogFormat( *api, handle, logDir.filePath( "closed.log" ) ).result
                         == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                for ( const auto& nothing : { none, pastEnd, zero, closed } ) {
                    REQUIRE( nothing.values == nullptr );
                    REQUIRE( nothing.fieldCount == 0 );
                    REQUIRE( nothing.lineCount == 0 );
                }
            }
        }

        WHEN( "The plugin asks with no handle, or nowhere to put the answer" )
        {
            const auto pathUtf8 = path.toUtf8();
            const char* const* values = nullptr;
            const char* const* names = nullptr;
            size_t count = 0;
            const auto noHandle = readLogLineFields( *api, nullptr, path, 1, 1 );
            const auto noValues = api->get_log_line_fields(
                handle, pathUtf8.constData(), 1, 1, nullptr, nullptr, nullptr, &count, &count );
            const auto noCounts = api->get_log_line_fields(
                handle, pathUtf8.constData(), 1, 1, &values, nullptr, nullptr, nullptr, nullptr );
            const auto noPath = api->get_log_line_fields( handle, nullptr, 1, 1, &values, nullptr,
                                                          nullptr, &count, &count );
            const auto noNames = api->get_log_format_fields( handle, pathUtf8.constData(), nullptr,
                                                             nullptr, &count );
            const auto noFieldCount = api->get_log_format_fields( handle, pathUtf8.constData(),
                                                                  nullptr, &names, nullptr );

            THEN( "It is an invalid argument" )
            {
                REQUIRE( noHandle.result == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noValues == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noCounts == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noPath == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noNames == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noFieldCount == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
            }
        }

        WHEN( "The plugin asks for none, or for more Log Lines than one call returns" )
        {
            const auto none = readLogLineFields( *api, handle, path, 10, 0 );
            const auto many = readLogLineFields( *api, handle, path, 1001, 2500 );
            const auto rest = readLogLineFields( *api, handle, path, 2001, 2500 );

            THEN( "It gets none, then the first ones that fit and is told there are more" )
            {
                REQUIRE( none.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( none.lineCount == 0 );
                REQUIRE( none.fieldCount == 2 );
                REQUIRE( many.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( many.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( many.lines.back() == QStringList{ "INFO", "line 2000" } );
                REQUIRE( rest.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( rest.lineCount == 1000 );
            }
        }

        WHEN( "The plugin asks for more bytes of values than one call returns" )
        {
            // 600 KiB each: two do not fit into 1 MiB. The third is longer
            // than that, of two-byte characters, the bound inside one.
            const QString wide( 600 * 1024, QChar( 'w' ) );
            const QString wider( LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES / 2 + 10, QChar( 0x00e9 ) );
            const auto widePath = logDir.filePath( "wide.log" );
            auto wideLogFile = std::make_shared<FakeLogFile>(
                QStringList{ "INFO|" + wide, "INFO|" + wide,
                             QStringLiteral( "a|" ) + wider + QStringLiteral( "x" ) } );
            wideLogFile->format = appFormat;
            const auto wideId = logFiles->add( widePath, std::move( wideLogFile ) );
            const auto two = readLogLineFields( *api, handle, widePath, 1, 3 );
            const auto cut = readLogLineFields( *api, handle, widePath, 3, 1 );
            logFiles->remove( wideId );

            THEN( "It gets whole Log Lines, but for a first one whose values are longer, cut at "
                  "a character" )
            {
                REQUIRE( two.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( two.lineCount == 1 );
                REQUIRE( two.lines.front() == QStringList{ "INFO", wide } );
                REQUIRE( cut.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( cut.lineCount == 1 );
                REQUIRE( cut.asDocumented );
                const auto& values = *cut.lines.front();
                REQUIRE( values.front() == QStringLiteral( "a" ) );
                REQUIRE( values.back()
                         == wider.left( ( LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES - 1 ) / 2 ) );
            }
        }

        WHEN( "Two threads of the plugin read one after the other" )
        {
            std::promise<void> firstRead;
            std::promise<void> secondRead;
            QString firstValueAfterwards;
            QString firstNameAfterwards;
            LogLineFieldsRead first;
            LogLineFieldsRead second;
            std::thread firstThread( [ & ] {
                first = readLogLineFields( *api, handle, path, 1, 2 );
                const auto pathUtf8 = path.toUtf8();
                const char* const* names = nullptr;
                size_t count = 0;
                api->get_log_format_fields( handle, pathUtf8.constData(), nullptr, &names, &count );
                firstRead.set_value();
                secondRead.get_future().wait();
                // Still this thread's, though the other thread read since.
                firstValueAfterwards = first.valueNow( 0, 1 );
                firstNameAfterwards = QString::fromUtf8( names[ 1 ] );
            } );
            std::thread secondThread( [ & ] {
                firstRead.get_future().wait();
                second = readLogLineFields( *api, handle, path, 100, 3 );
                readLogFormat( *api, handle, plainPath );
                secondRead.set_value();
            } );
            firstThread.join();
            secondThread.join();

            THEN( "Each keeps its own until it reads again" )
            {
                REQUIRE( first.values != second.values );
                REQUIRE( firstValueAfterwards == QStringLiteral( "line 1" ) );
                REQUIRE( firstNameAfterwards == QStringLiteral( "message" ) );
                REQUIRE( second.lines.front() == QStringList{ "INFO", "line 100" } );
            }
        }

        WHEN( "The tab of the Log File closes while a thread of the plugin reads its fields" )
        {
            std::promise<void> reading;
            std::promise<void> closed;
            auto closedFuture = closed.get_future().share();
            heldLogFile.lock()->whileReading = [ &reading, closedFuture ] {
                reading.set_value();
                closedFuture.wait();
            };
            LogLineFieldsRead read;
            std::thread reader( [ & ] { read = readLogLineFields( *api, handle, path, 1, 2 ); } );
            reading.get_future().wait();
            logFiles->remove( id );
            closed.set_value();
            reader.join();

            THEN( "The read finishes, and says there is no Log File" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.values == nullptr );
                REQUIRE( heldLogFile.expired() );
            }
        }

        logFiles->remove( id );
        logFiles->remove( plainId );
        host.unloadAll();
        probe.library.unload();
    }
}

namespace {

int changeMarks( decltype( LogSquirlHostApi::add_marks ) change, void* handle, const QString& path,
                 const std::vector<uint64_t>& lines )
{
    const auto pathUtf8 = path.toUtf8();
    return change( handle, pathUtf8.constData(), lines.data(), lines.size() );
}

/// What get_marks handed back, and the numbers copied as they came.
struct MarksRead {
    int result = 0;
    const uint64_t* lines = nullptr;
    size_t count = 99;
    std::vector<uint64_t> copied{};
};

MarksRead readMarks( const LogSquirlHostApi& api, void* handle, const QString& path )
{
    const auto pathUtf8 = path.toUtf8();
    // Something to overwrite, so that a NULL handed back is seen.
    static const uint64_t notHandedBack = 0;
    MarksRead read;
    read.lines = &notHandedBack;
    read.result = api.get_marks( handle, pathUtf8.constData(), &read.lines, &read.count );
    if ( read.lines != nullptr ) {
        read.copied.assign( read.lines, read.lines + read.count );
    }
    return read;
}

} // namespace

SCENARIO( "A plugin sets, removes and reads Marks of an open Log File through the Plugin Host",
          "[pluginhost][pluginmarks][plugins]" )
{
    GIVEN( "A Plugin Host with the probe plugin loaded and a Plugin UI Port, and a Log File open" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installProbe( pluginRoot.path() );
        QTemporaryDir logDir;
        REQUIRE( logDir.isValid() );
        const auto path = logDir.filePath( "app.log" );
        const auto closedPath = logDir.filePath( "closed.log" );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );
        PluginHost host( catalog );
        FakePluginUiPort port;
        host.setUiPort( &port );
        REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );

        Probe probe( QStringLiteral( LOGSQUIRL_UI_PORT_PROBE_PATH ) );
        REQUIRE( probe.library.load() );
        const auto* api = probe.hostApi();
        auto* handle = probe.hostHandle();
        REQUIRE( api != nullptr );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), add_marks ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), remove_marks ) );
        REQUIRE( LOGSQUIRL_HOST_API_HAS( probe.hostApiSize(), get_marks ) );

        const auto& logFiles = host.logFiles();
        const auto id = logFiles->add( path, std::make_shared<FakeLogFile>( numberedLines( 30 ) ) );

        WHEN( "The plugin sets Marks on the UI thread, numbered from 1, one of them 0" )
        {
            const auto result = changeMarks( api->add_marks, handle, path, { 5, 0, 2, 30, 31 } );

            THEN( "The port sets them at once, numbered from 0, without the 0; it skips a line "
                  "past the end itself" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( port.marksChanges.size() == 1 );
                REQUIRE( port.marksChanges[ 0 ].added );
                REQUIRE( port.marksChanges[ 0 ].logFile == path );
                REQUIRE( port.marksChanges[ 0 ].logLines
                         == std::vector<std::uint64_t>{ 4, 1, 29, 30 } );
            }
        }

        WHEN( "The plugin removes Marks on the UI thread" )
        {
            const auto result = changeMarks( api->remove_marks, handle, path, { 3 } );

            THEN( "The port removes them at once" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( port.marksChanges.size() == 1 );
                REQUIRE_FALSE( port.marksChanges[ 0 ].added );
                REQUIRE( port.marksChanges[ 0 ].logLines == std::vector<std::uint64_t>{ 2 } );
            }
        }

        WHEN( "The plugin sets and removes Marks on a thread of its own" )
        {
            int added = -100;
            int removed = -100;
            std::thread( [ & ] {
                added = changeMarks( api->add_marks, handle, path, { 7, 8 } );
                removed = changeMarks( api->remove_marks, handle, path, { 7 } );
            } ).join();

            THEN( "It is told at once that they are set, and the port carries them out on the "
                  "host's thread, in order, once the event loop turns" )
            {
                REQUIRE( added == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( removed == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( port.marksChanges.empty() );

                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                REQUIRE( port.marksChanges.size() == 2 );
                REQUIRE( port.marksChanges[ 0 ].added );
                REQUIRE( port.marksChanges[ 0 ].logLines == std::vector<std::uint64_t>{ 6, 7 } );
                REQUIRE( port.marksChanges[ 0 ].on == host.thread() );
                REQUIRE_FALSE( port.marksChanges[ 1 ].added );
                REQUIRE( port.marksChanges[ 1 ].logLines == std::vector<std::uint64_t>{ 6 } );
                REQUIRE( port.marksChanges[ 1 ].on == host.thread() );
            }

            AND_WHEN( "The plugin is unloaded, and loaded again, before the event loop turns" )
            {
                host.unloadPlugin( ProbeId );
                REQUIRE( host.loadPlugin( ProbeId ).isEmpty() );
                QCoreApplication::sendPostedEvents( &host, QEvent::MetaCall );

                THEN( "Nothing is marked: the calls were the unloaded plugin's" )
                {
                    REQUIRE( port.marksChanges.empty() );
                }
            }
        }

        WHEN( "The plugin names a Log File no tab shows, or its tab has closed" )
        {
            const auto closed = changeMarks( api->add_marks, handle, closedPath, { 1 } );
            logFiles->remove( id );
            const auto added = changeMarks( api->add_marks, handle, path, { 1 } );
            const auto removed = changeMarks( api->remove_marks, handle, path, { 1 } );

            THEN( "It is told there is no Log File, and nothing is marked" )
            {
                REQUIRE( closed == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( added == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( removed == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( port.marksChanges.empty() );
            }
        }

        WHEN( "The plugin passes no line, only 0, the most one call takes, and one more" )
        {
            const auto none = api->add_marks( handle, path.toUtf8().constData(), nullptr, 0 );
            const auto zero = changeMarks( api->add_marks, handle, path, { 0 } );
            const std::vector<uint64_t> most( LOGSQUIRL_MARKS_MAX_LINES, 1 );
            const auto atMost = changeMarks( api->add_marks, handle, path, most );
            const std::vector<uint64_t> tooMany( LOGSQUIRL_MARKS_MAX_LINES + 1, 1 );
            const auto added = changeMarks( api->add_marks, handle, path, tooMany );
            const auto removed = changeMarks( api->remove_marks, handle, path, tooMany );

            THEN( "Up to the most it is done, with nothing to mark for no line or 0, and above "
                  "it nothing is done" )
            {
                REQUIRE( none == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( zero == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( atMost == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( added == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( removed == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( port.marksChanges.size() == 1 );
                REQUIRE( port.marksChanges[ 0 ].logLines.size() == LOGSQUIRL_MARKS_MAX_LINES );
            }
        }

        WHEN( "The plugin passes no Log File, or no lines for a count" )
        {
            const uint64_t line = 1;
            const auto noPath = api->add_marks( handle, nullptr, &line, 1 );
            const auto noLines = api->remove_marks( handle, path.toUtf8().constData(), nullptr, 1 );
            const auto noHandle = api->add_marks( nullptr, path.toUtf8().constData(), &line, 1 );

            THEN( "Nothing is done" )
            {
                REQUIRE( noPath == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noLines == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noHandle == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( port.marksChanges.empty() );
            }
        }

        WHEN( "The plugin reads the Marks of a Log File a tab shows" )
        {
            port.marksOf[ path ] = { 0, 4, 29 };
            const auto read = readMarks( *api, handle, path );

            THEN( "It gets them numbered from 1, in order" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.count == 3 );
                REQUIRE( read.copied == std::vector<uint64_t>{ 1, 5, 30 } );
                REQUIRE( port.askedForMarksOf == QStringList{ path } );
            }

            AND_WHEN( "Another plugin call reads other things, and the plugin reads again" )
            {
                port.encodingInFront = QStringLiteral( "UTF-8" );
                const char* encoding = nullptr;
                api->get_log_file_encoding( handle, nullptr, &encoding );

                THEN( "The array is as it was until the plugin reads the Marks again" )
                {
                    REQUIRE( std::vector<uint64_t>( read.lines, read.lines + read.count )
                             == read.copied );

                    port.marksOf[ path ] = { 9 };
                    const auto again = readMarks( *api, handle, path );
                    REQUIRE( again.copied == std::vector<uint64_t>{ 10 } );
                }
            }
        }

        WHEN( "The plugin reads the Marks of a Log File without any" )
        {
            port.marksOf[ path ] = {};
            const auto read = readMarks( *api, handle, path );

            THEN( "It gets none, and no array" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.count == 0 );
                REQUIRE( read.lines == nullptr );
            }
        }

        WHEN( "The plugin reads the Marks of a Log File no tab shows" )
        {
            const auto read = readMarks( *api, handle, closedPath );

            THEN( "It is told there is no Log File" )
            {
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( read.count == 0 );
                REQUIRE( read.lines == nullptr );
            }
        }

        WHEN( "The plugin reads the Marks off the UI thread, or without a place to put them" )
        {
            port.marksOf[ path ] = { 1 };
            MarksRead offThread;
            std::thread( [ & ] { offThread = readMarks( *api, handle, path ); } ).join();
            size_t count = 0;
            const auto noLines
                = api->get_marks( handle, path.toUtf8().constData(), nullptr, &count );
            const uint64_t* lines = nullptr;
            const auto noCount
                = api->get_marks( handle, path.toUtf8().constData(), &lines, nullptr );
            const auto noPath = api->get_marks( handle, nullptr, &lines, &count );

            THEN( "None are read, and the port is not asked" )
            {
                REQUIRE( offThread.result == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( offThread.lines == nullptr );
                REQUIRE( offThread.count == 0 );
                REQUIRE( noLines == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noCount == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( noPath == LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT );
                REQUIRE( port.askedForMarksOf.isEmpty() );
            }
        }

        logFiles->remove( id );
        host.unloadAll();
        probe.library.unload();
    }
}
