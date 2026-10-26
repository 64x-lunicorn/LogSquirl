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

#include "pluginhost.h"

#include "datalocation.h"
#include "log.h"
#include "streamwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace logsquirl::plugins {

// The views hand a context menu item no more Log Lines than it passes on.
static_assert( PluginContextMenuMaxLogLines == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );

// ── Conversions ─────────────────────────────────────────────────────────────

/**
 * What the worker thread of a Conversion shares with the host (#795): the
 * control handed to convert_ex, whose host pointer leads back here, the
 * cancellation flag, the latest progress reported and what the converter
 * returned. Owned by the Conversion and by the worker thread, which may
 * outlive the Conversion's record for a moment.
 */
struct PluginHost::ConversionControl : std::enable_shared_from_this<PluginHost::ConversionControl> {
    ConversionControl( PluginHost* host, ConversionId conversionId )
        : pluginHost( host )
        , id( conversionId )
    {
        table.size = sizeof( LogSquirlConvertControl );
        table.host = this;
        table.report_progress = &PluginHost::hostReportProgress;
        table.is_cancelled = &PluginHost::hostIsCancelled;
    }

    LogSquirlConvertControl table{};
    PluginHost* pluginHost;
    ConversionId id;
    std::atomic<bool> cancelled{ false };
    // The latest progress, as one pair, and whether a report of it is
    // already queued to the host's thread: reports arriving faster than that
    // thread takes them fold into the latest one.
    std::mutex progressMutex{};
    std::pair<std::uint64_t, std::uint64_t> progress{ 0, 0 };
    std::atomic<bool> progressQueued{ false };
    // What the converter returned, once its call has.
    std::atomic<int> returnCode{ 0 };
};

// ── Content checks ──────────────────────────────────────────────────────────

/**
 * What the worker thread of a content check shares with the host (#861),
 * under mutex: the converters to ask in plugin-ID order, which of them have
 * left the check, the one being asked and the one that claimed the file. An
 * unload takes its plugin out and waits, on returned, until a call into it
 * has returned; the handle of a plugin that left is not touched any more.
 */
struct PluginHost::ContentCheckControl {
    struct Converter {
        QString pluginId;
        const PluginHandle* handle = nullptr;
        bool left = false;
    };

    std::mutex mutex{};
    std::condition_variable returned{};
    std::vector<Converter> converters{};
    // The handle of the converter being asked, or nullptr.
    const PluginHandle* calling = nullptr;
    bool cancelled = false;
    QString claimedBy{};
};

// ── Construction / destruction ──────────────────────────────────────────────

PluginHost::PluginHost( const PluginCatalog& catalog, QObject* parent )
    : QObject( parent )
    , catalog_( catalog )
{
}

PluginHost::~PluginHost()
{
    // Nothing is left to wait for: whatever is still loaded goes now, an
    // unload that waited for a call into its plugin included.
    while ( !loaded_.empty() ) {
        const auto pluginId = loaded_.begin()->first;
        const auto wasPending = loaded_.begin()->second->unloadPending;
        unloadNow( pluginId );
        if ( !wasPending ) {
            Q_EMIT pluginUnloaded( pluginId );
        }
    }

    // A content check still under way asks no plugin any more, all of them
    // having left it; it is waited for, and reports nothing (#861).
    for ( auto& check : contentChecks_ ) {
        if ( check->thread.joinable() ) {
            check->thread.join();
        }
    }
    contentChecks_.clear();
}

// ── Loading / unloading ─────────────────────────────────────────────────────

QStringList PluginHost::loadedPluginIds() const
{
    QStringList ids;
    ids.reserve( static_cast<int>( loaded_.size() ) );
    for ( const auto& [ id, ctx ] : loaded_ ) {
        if ( !ctx->unloadPending ) {
            ids.append( id );
        }
    }
    return ids;
}

PluginAutoLoadResult PluginHost::autoLoadPlugins( const PluginAutoLoad& configuration )
{
    if ( !configuration.autoLoad ) {
        return {};
    }

    PluginAutoLoadResult result;
    auto enabledIds = configuration.enabled;

    // First run: until the enabled plugins were chosen, enable every
    // discovered plugin so they are visible immediately. The caller keeps the
    // choice, even an empty one, so this happens once; a user who disabled
    // every plugin gets none back, and a plugin dropped into the plugin folder
    // later is listed but not loaded (#820).
    if ( enabledIds.isEmpty() && !configuration.chosen ) {
        for ( const auto& meta : catalog_.discoveredPlugins() ) {
            enabledIds.append( meta.id() );
        }
        result.enabledOnFirstRun = enabledIds;
        LOG_INFO << "First run: auto-enabled " << enabledIds.size() << " discovered plugin(s)";
    }

    for ( const auto& pluginId : enabledIds ) {
        if ( isLoaded( pluginId ) ) {
            continue;
        }
        if ( !catalog_.findDiscovered( pluginId ) ) {
            LOG_WARNING << "Auto-load: plugin '" << pluginId << "' not found, skipping";
            continue;
        }

        const auto error = loadPlugin( pluginId );
        if ( !error.isEmpty() ) {
            LOG_WARNING << "Auto-load failed for '" << pluginId << "': " << error;
            result.errors.append( QString( "%1: %2" ).arg( pluginId, error ) );
        }
    }

    return result;
}

QString PluginHost::loadPlugin( const QString& pluginId )
{
    if ( const auto it = loaded_.find( pluginId ); it != loaded_.end() ) {
        // Loaded again before a call into it let its unload finish: it was
        // never shut down, so it simply stays.
        if ( it->second->unloadPending ) {
            it->second->unloadPending = false;
            // The Regex Lab answers it was owed meanwhile are delivered now,
            // but not from within whatever loaded it again.
            if ( !it->second->parkedAnswers.empty() ) {
                deliverParkedAnswersLater( *it->second );
            }
            Q_EMIT pluginLoaded( pluginId );
            return {};
        }
        return QStringLiteral( "Plugin already loaded" );
    }

    const auto* meta = catalog_.findDiscovered( pluginId );
    if ( !meta ) {
        return QString( "Plugin '%1' not found in discovered plugins" ).arg( pluginId );
    }

    // Load the shared library
    auto loadResult = PluginLoader::load( *meta );
    if ( !loadResult.has_value() ) {
        auto error = loadResult.error();
        LOG_ERROR << "Failed to load plugin '" << pluginId << "': " << error;
        Q_EMIT pluginError( pluginId, error );
        return error;
    }

    // Create context — PluginHandle has no default ctor so we construct in-place
    auto ctx = std::unique_ptr<PluginContext>(
        new PluginContext{ .handle = std::move( loadResult.value() ), .host = this } );
    ctx->hostApi = buildHostApi();
    ctx->answerContext = std::make_unique<QObject>();
    ctx->loadSerial = ++loadsSoFar_;

    // Create plugin-private config directory, next to the user plugin
    // directory: beside the executable in a portable run (#602).
    ctx->configDir = DataLocation::current().dataDirectory() + "/plugin_config/" + pluginId;
    ctx->configDirUtf8 = ctx->configDir.toUtf8();
    QDir().mkpath( ctx->configDir );

    // Initialise — pass the context pointer as the opaque handle so that
    // host API trampolines can route back to this host
    auto error = ctx->handle.init( &ctx->hostApi, ctx.get() );
    if ( !error.isEmpty() ) {
        LOG_ERROR << "Failed to init plugin '" << pluginId << "': " << error;
        // A plugin may have registered contributions before its init failed.
        ctx->answerContext.reset();
        if ( uiPort_ ) {
            uiPort_->removeContributions( pluginId );
        }
        Q_EMIT pluginError( pluginId, error );
        return error;
    }

    loaded_[ pluginId ] = std::move( ctx );

    // For converter plugins, log the registered extensions
    auto* loadedHandle = pluginHandle( pluginId );
    if ( loadedHandle && loadedHandle->isConverter() ) {
        LOG_INFO << "Converter plugin '" << pluginId
                 << "' registered extensions: " << loadedHandle->converterExtensions();
    }

    Q_EMIT pluginLoaded( pluginId );
    return {};
}

void PluginHost::unloadPlugin( const QString& pluginId )
{
    const auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() || it->second->unloadPending ) {
        return;
    }

    auto& context = *it->second;
    // Nothing calls into the plugin from now on, not even from what its
    // Conversions report as they are cancelled: the one under way is waited
    // for, so the plugin is never shut down under its call (#795).
    context.unloadPending = true;
    cancelConversionsOf( context );
    // Nor does a content check ask it any more, once a call under way has
    // returned (#861).
    leaveContentChecks( context );

    // A call into the plugin is still on the stack, below the event loop
    // this unload came from: shutting the plugin down now would pull its
    // state from under that call, and taking its menu action away would
    // delete the action whose signal is being delivered. It goes once the
    // call has returned.
    if ( context.callsRunning > 0 ) {
        LOG_INFO << "Unloading plugin " << pluginId << " once its running call has returned";
        Q_EMIT pluginUnloaded( pluginId );
        return;
    }

    unloadNow( pluginId );
    Q_EMIT pluginUnloaded( pluginId );
}

void PluginHost::unloadNow( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() ) {
        return;
    }

    LOG_INFO << "Unloading plugin: " << pluginId;

    // A Conversion still under way -- the host is going -- returns first, and
    // nothing its report asks for starts.
    it->second->unloadPending = true;
    cancelConversionsOf( *it->second );
    leaveContentChecks( *it->second );

    // From now on, no answer of the user interface reaches the plugin: a
    // Regex Lab it opened may still answer while it is closed.
    it->second->answerContext.reset();
    it->second->parkedAnswers.clear();

    // A stream that runs is stopped in the plugin first, while the plugin is
    // still initialised: it closes its source before it is shut down (#855).
    stopInPlugin( *it->second );

    // Shut the plugin down while it is still loaded: it unregisters its
    // widgets through the host callbacks. Whatever it left behind is taken
    // away before its context and library go.
    it->second->handle.shutdown();

    // Its stream goes only now: a thread of the plugin's may push until
    // shutdown has returned, and finds a live stream meanwhile (#821).
    endStream( *it->second, pluginId );

    if ( uiPort_ ) {
        uiPort_->removeContributions( pluginId );
    }

    loaded_.erase( it );
}

PluginHost::PluginContext* PluginHost::loadedContext( const QString& pluginId ) const
{
    const auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() || it->second->unloadPending ) {
        return nullptr;
    }
    return it->second.get();
}

bool PluginHost::callIntoPlugin( PluginContext& context, const std::function<void()>& call )
{
    if ( context.unloadPending ) {
        return false;
    }

    ++context.callsRunning;
    call();
    --context.callsRunning;

    if ( context.callsRunning == 0 && context.unloadPending ) {
        // Finished from the event loop, once whatever delivered this call --
        // the signal of a menu action, say -- has returned as well.
        const auto pluginId = context.handle.metadata().id();
        QMetaObject::invokeMethod(
            this,
            [ this, pluginId ] {
                const auto it = loaded_.find( pluginId );
                if ( it != loaded_.end() && it->second->unloadPending
                     && it->second->callsRunning == 0 ) {
                    unloadNow( pluginId );
                }
            },
            Qt::QueuedConnection );
    }
    return true;
}

void PluginHost::answerPlugin( PluginContext& context, std::function<void()> answer )
{
    if ( context.unloadPending || !context.parkedAnswers.empty() ) {
        context.parkedAnswers.push_back( std::move( answer ) );
        return;
    }
    callIntoPlugin( context, answer );
}

void PluginHost::deliverParkedAnswersLater( PluginContext& context )
{
    QMetaObject::invokeMethod(
        this,
        [ this, pluginId = context.handle.metadata().id(), loadSerial = context.loadSerial ] {
            const auto it = loaded_.find( pluginId );
            if ( it == loaded_.end() || it->second->loadSerial != loadSerial ) {
                return;
            }
            // Unloaded again meanwhile: those left wait for the next load.
            auto& parked = it->second->parkedAnswers;
            while ( !parked.empty() && !it->second->unloadPending ) {
                const auto answer = std::move( parked.front() );
                parked.pop_front();
                callIntoPlugin( *it->second, answer );
            }
        },
        Qt::QueuedConnection );
}

void PluginHost::runMenuAction( void* menuActionCall )
{
    const auto* call = static_cast<const MenuActionCall*>( menuActionCall );
    // Removed meanwhile: a trigger queued before unregister_menu_action
    // returned is dropped (#857).
    if ( !call->callback || call->removed.load() ) {
        return;
    }
    call->context->host->callIntoPlugin( *call->context,
                                         [ call ] { call->callback( call->userData ); } );
}

void PluginHost::runContextMenuAction( void* contextMenuActionCall,
                                       const PluginContextMenuContext& context )
{
    const auto* call = static_cast<const ContextMenuActionCall*>( contextMenuActionCall );
    // Removed meanwhile: a choice delivered after unregister_context_menu_action
    // returned is dropped, as a menu action's trigger is (#857, #859).
    if ( !call->callback || call->removed.load() ) {
        return;
    }

    const auto filePath = context.logFile.toUtf8();
    // Numbered as the views number them, from 1.
    std::vector<uint64_t> lineNumbers;
    const auto count
        = std::min<std::size_t>( context.logLines.size(), LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
    lineNumbers.reserve( count );
    for ( std::size_t i = 0; i < count; ++i ) {
        lineNumbers.push_back( context.logLines[ i ] + 1 );
    }
    const auto selectedText = context.selectedText ? context.selectedText->toUtf8() : QByteArray{};
    const auto view = [ &context ] {
        switch ( context.view ) {
        case PluginLogView::Table:
            return LOGSQUIRL_LOG_VIEW_TABLE;
        case PluginLogView::Filtered:
            return LOGSQUIRL_LOG_VIEW_FILTERED;
        case PluginLogView::Text:
            break;
        }
        return LOGSQUIRL_LOG_VIEW_TEXT;
    }();

    const LogSquirlContextMenuContext handed{
        .size = sizeof( LogSquirlContextMenuContext ),
        .file_path = filePath.constData(),
        .view = view,
        .line_numbers = lineNumbers.empty() ? nullptr : lineNumbers.data(),
        .line_number_count = lineNumbers.size(),
        .selected_line_count = std::max<uint64_t>( context.selectedLogLineCount, count ),
        .selected_text = context.selectedText ? selectedText.constData() : nullptr,
        .selected_text_length
        = context.selectedText ? static_cast<std::size_t>( selectedText.size() ) : 0,
    };
    call->context->host->callIntoPlugin(
        *call->context, [ call, &handed ] { call->callback( call->userData, &handed ); } );
}

void PluginHost::unloadAll()
{
    // Collect IDs first to avoid iterator invalidation during signal emission
    const auto ids = loadedPluginIds();
    for ( const auto& id : ids ) {
        unloadPlugin( id );
    }
}

bool PluginHost::isLoaded( const QString& pluginId ) const
{
    return loadedContext( pluginId ) != nullptr;
}

void PluginHost::setUiPort( PluginUiPort* uiPort )
{
    uiPort_ = uiPort;
}

PluginHandle* PluginHost::pluginHandle( const QString& pluginId )
{
    auto* ctx = loadedContext( pluginId );
    return ctx ? &ctx->handle : nullptr;
}

void PluginHost::configurePlugin( const QString& pluginId )
{
    auto* ctx = loadedContext( pluginId );
    if ( ctx && ctx->handle.hasConfigureUi() ) {
        const auto parent = uiPort_ ? uiPort_->configurationParent() : PluginWidgetHandle{};
        callIntoPlugin( *ctx, [ ctx, &parent ] { ctx->handle.configure( parent.widget ); } );
    }
}

void PluginHost::setOpenFileCallback( std::function<void( const QString&, bool )> callback )
{
    openFileCallback_ = std::move( callback );
}

void PluginHost::setActiveFilePathCallback( std::function<QString()> callback )
{
    activeFilePathCallback_ = std::move( callback );
}

void PluginHost::notifyActiveFileChanged( const QString& filePath )
{
    const auto utf8 = filePath.toUtf8();
    // By ID: a plugin's callback may unload another plugin.
    for ( const auto& id : loadedPluginIds() ) {
        auto* ctx = loadedContext( id );
        if ( !ctx ) {
            continue;
        }
        // Taken as one: the plugin may register another from its own thread.
        void ( *callback )( void* user_data, const char* file_path ) = nullptr;
        void* userData = nullptr;
        {
            const std::lock_guard lock( ctx->registrationsMutex );
            callback = ctx->activeFileCallback;
            userData = ctx->activeFileUserData;
        }
        if ( callback ) {
            callIntoPlugin(
                *ctx, [ callback, userData, &utf8 ] { callback( userData, utf8.constData() ); } );
        }
    }
}

void PluginHost::notifyEncodingChanged( const QString& filePath, const QString& encoding )
{
    const auto pathUtf8 = filePath.toUtf8();
    const auto encodingUtf8 = encoding.toUtf8();
    // By ID: a plugin's callback may unload another plugin.
    for ( const auto& id : loadedPluginIds() ) {
        auto* ctx = loadedContext( id );
        if ( !ctx ) {
            continue;
        }
        // Taken as one: the plugin may register another from its own thread.
        LogSquirlEncodingCallbackFn callback = nullptr;
        void* userData = nullptr;
        {
            const std::lock_guard lock( ctx->registrationsMutex );
            callback = ctx->encodingCallback;
            userData = ctx->encodingUserData;
        }
        if ( callback ) {
            callIntoPlugin( *ctx, [ callback, userData, &pathUtf8, &encodingUtf8 ] {
                callback( userData, pathUtf8.constData(), encodingUtf8.constData() );
            } );
        }
    }
}

void PluginHost::notifyLogLinesChanged( const QString& filePath, std::uint64_t lineCount,
                                        PluginLogLinesChange change )
{
    const auto pathUtf8 = filePath.toUtf8();
    const int changed = change == PluginLogLinesChange::Appended ? LOGSQUIRL_LOG_LINES_APPENDED
                                                                 : LOGSQUIRL_LOG_LINES_REPLACED;
    // By ID: a plugin's callback may unload another plugin.
    for ( const auto& id : loadedPluginIds() ) {
        auto* ctx = loadedContext( id );
        if ( !ctx ) {
            continue;
        }
        // Taken as one: the plugin may register another from its own thread.
        LogSquirlLogLinesCallbackFn callback = nullptr;
        void* userData = nullptr;
        {
            const std::lock_guard lock( ctx->registrationsMutex );
            callback = ctx->logLinesCallback;
            userData = ctx->logLinesUserData;
        }
        if ( callback ) {
            callIntoPlugin( *ctx, [ callback, userData, &pathUtf8, lineCount, changed ] {
                callback( userData, pathUtf8.constData(), lineCount, changed );
            } );
        }
    }
}

// ── DataSource (Phase 2) ─────────────────────────────────────────────────────────

QString PluginHost::startDataSource( const QString& pluginId )
{
    auto* loaded = loadedContext( pluginId );
    if ( !loaded ) {
        return QString( "Plugin '%1' is not loaded" ).arg( pluginId );
    }

    auto& ctx = *loaded;
    if ( ctx.handle.metadata().type() != LOGSQUIRL_PLUGIN_DATASOURCE ) {
        return QString( "Plugin '%1' is not a DataSource plugin" ).arg( pluginId );
    }

    // Also while the plugin's start is still running, a modal dialog of its
    // own open: the second start does not reach the plugin (#855).
    if ( ctx.stream || ctx.dataSourceState != DataSourceState::Idle ) {
        return QString( "DataSource '%1' is already running" ).arg( pluginId );
    }

    // Create the file-backed stream writer. It is there before the plugin's
    // start, so that what the plugin pushes from inside it is kept.
    auto stream = std::make_unique<StreamWriter>( ctx.handle.metadata().name() );
    const auto path = stream->filePath();
    {
        const std::lock_guard lock( ctx.streamMutex );
        ctx.stream = std::move( stream );
        ctx.streamEndedByPlugin = false;
    }

    // The plugin opens its device, socket or process, and may ask the user
    // which in a dialog of its own. Its unload, asked for meanwhile, waits
    // until the call has returned, so the context stays.
    ctx.dataSourceState = DataSourceState::Starting;
    int result = LOGSQUIRL_DATASOURCE_STARTED;
    if ( ctx.handle.hasDataSourceStart() ) {
        if ( !callIntoPlugin( ctx, [ &ctx, &result ] {
                 result = ctx.handle.startDataSource( &ctx.dataSourceContext );
             } ) ) {
            result = LOGSQUIRL_DATASOURCE_FAILED;
        }
    }

    if ( result != LOGSQUIRL_DATASOURCE_STARTED ) {
        // Not started: the stream goes without an end, nobody has opened it.
        {
            const std::lock_guard lock( ctx.streamMutex );
            ctx.stream.reset();
        }
        ctx.dataSourceState = DataSourceState::Idle;
        if ( result == LOGSQUIRL_DATASOURCE_CANCELLED ) {
            LOG_INFO << "DataSource '" << pluginId << "' was cancelled as it started";
            return {};
        }
        LOG_WARNING << "DataSource '" << pluginId << "' did not start: " << result;
        return QString( "DataSource '%1' did not start (code %2)" ).arg( pluginId ).arg( result );
    }

    // The stream runs: the plugin is owed its stop, however the stream ends.
    ctx.dataSourceState = DataSourceState::Running;
    if ( ctx.unloadPending ) {
        // Unloaded while it started: its unload stops and ends the stream,
        // and no tab opens for a plugin that counts as unloaded.
        return QString( "Plugin '%1' is not loaded" ).arg( pluginId );
    }

    // The plugin signalled the end of its stream from inside its start: the
    // tab opens all the same, and the stream ends from the event loop, as
    // it would have, had the plugin signalled once start returned (#871).
    // Asked for before the signal, whose slot may unload the plugin.
    bool endedByPlugin = false;
    {
        const std::lock_guard lock( ctx.streamMutex );
        endedByPlugin = ctx.streamEndedByPlugin;
    }
    if ( endedByPlugin ) {
        endStreamThePluginEndedLater( ctx );
    }

    LOG_INFO << "Started DataSource '" << pluginId << "' writing to " << path;
    Q_EMIT dataSourceStarted( pluginId, ctx.handle.metadata().name(), path );
    return {};
}

void PluginHost::stopDataSource( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() || !it->second->stream ) {
        return;
    }
    auto& context = *it->second;
    // A stop asked for while the plugin starts or stops: that call ends as
    // it ends, and a stopping stream ends once the stop has returned.
    if ( context.dataSourceState == DataSourceState::Starting
         || context.dataSourceState == DataSourceState::Stopping ) {
        return;
    }

    LOG_INFO << "Stopping DataSource: " << pluginId;
    // The plugin pushes its last lines and closes its source before the
    // stream ends. Its unload, asked for meanwhile, waits until the call has
    // returned, so the context stays.
    stopInPlugin( context );
    endStream( context, pluginId );
}

void PluginHost::stopInPlugin( PluginContext& context )
{
    if ( context.dataSourceState != DataSourceState::Running ) {
        return;
    }
    context.dataSourceState = DataSourceState::Stopping;
    if ( !context.handle.hasDataSourceStop() ) {
        return;
    }
    const auto stop = [ &context ] { context.handle.stopDataSource( &context.dataSourceContext ); };
    if ( !callIntoPlugin( context, stop ) ) {
        // Its unload is pending, but it is not shut down yet: it is still
        // owed the stop, and gets it before its shutdown.
        ++context.callsRunning;
        stop();
        --context.callsRunning;
    }
}

void PluginHost::endStream( PluginContext& context, const QString& pluginId )
{
    {
        const std::lock_guard lock( context.streamMutex );
        if ( !context.stream ) {
            return;
        }
        context.stream->signalEos();
        // The stream is over, but its tab still reads its file: the file
        // goes with the context, when the plugin is unloaded (#871).
        context.endedStreams.push_back( std::move( context.stream ) );
    }
    context.dataSourceState = DataSourceState::Idle;
    // Said outside the lock: a slot may ask the host about the stream.
    Q_EMIT dataSourceStopped( pluginId );
}

StreamWriter* PluginHost::streamWriter( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() ) {
        return nullptr;
    }
    const std::lock_guard lock( it->second->streamMutex );
    return it->second->stream.get();
}

// ── Converter registry (Phase 4) ───────────────────────────────────────────────

namespace {

// "har", ".har" and "*.har" all name the same extension: a converter declares
// ".har", a file name's suffix is "har".
QString bareExtension( const QString& extension )
{
    auto bare = extension.trimmed().toLower();
    while ( bare.startsWith( '*' ) || bare.startsWith( '.' ) ) {
        bare.remove( 0, 1 );
    }
    return bare;
}

} // namespace

std::vector<PluginHost::PluginContext*> PluginHost::convertersInIdOrder() const
{
    std::vector<PluginContext*> converters;
    for ( const auto& [ id, ctx ] : loaded_ ) {
        if ( !ctx->unloadPending && ctx->handle.isConverter() ) {
            converters.push_back( ctx.get() );
        }
    }
    // Sorted here, not taken from how loaded_ happens to keep them: the order
    // is what the plugin developer guide promises (#861).
    std::ranges::sort( converters, []( const PluginContext* left, const PluginContext* right ) {
        return left->handle.metadata().id() < right->handle.metadata().id();
    } );
    return converters;
}

QString PluginHost::converterForExtension( const QString& extension ) const
{
    const auto ext = bareExtension( extension );
    if ( ext.isEmpty() ) {
        return {};
    }
    for ( const auto* ctx : convertersInIdOrder() ) {
        // Extensions are semicolon-separated, e.g. ".har;.pcap"
        const auto exts = ctx->handle.converterExtensions().split( ';', Qt::SkipEmptyParts );
        for ( const auto& e : exts ) {
            if ( bareExtension( e ) == ext ) {
                return ctx->handle.metadata().id();
            }
        }
    }
    return {};
}

namespace {

// The first bytes of the file a converter is asked about, or nothing when it
// cannot be read (#861).
std::optional<QByteArray> readHead( const QString& filePath )
{
    QFile file( filePath );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return std::nullopt;
    }
    auto head = file.read( LOGSQUIRL_CONVERTER_CLAIMS_HEAD_LENGTH );
    if ( file.error() != QFileDevice::NoError ) {
        return std::nullopt;
    }
    return head;
}

} // namespace

ContentCheckId PluginHost::checkContent( const QString& filePath,
                                         std::function<void( const QString& pluginId )> claimed )
{
    auto control = std::make_shared<ContentCheckControl>();
    for ( const auto* ctx : convertersInIdOrder() ) {
        if ( ctx->handle.claimsByContent() ) {
            control->converters.push_back( ContentCheckControl::Converter{
                .pluginId = ctx->handle.metadata().id(), .handle = &ctx->handle, .left = false } );
        }
    }
    if ( control->converters.empty() ) {
        return 0;
    }

    auto check = std::make_unique<ContentCheck>();
    check->id = ++contentChecksSoFar_;
    check->claimed = std::move( claimed );
    check->control = control;
    LOG_INFO << "Asking " << control->converters.size() << " converter plugin(s) whether they "
             << "claim " << filePath << " by its content";

    // The handles stay while the thread asks them: a plugin's unload takes it
    // out of the check, waiting for a call under way, before its context goes.
    check->thread = std::thread(
        [ this, control, id = check->id, path = QFileInfo( filePath ).absoluteFilePath() ] {
            if ( const auto head = readHead( path ) ) {
                const auto pathUtf8 = path.toUtf8();
                for ( std::size_t next = 0;; ++next ) {
                    const PluginHandle* handle = nullptr;
                    {
                        const std::lock_guard lock( control->mutex );
                        if ( control->cancelled || next >= control->converters.size() ) {
                            break;
                        }
                        if ( control->converters[ next ].left ) {
                            continue;
                        }
                        handle = control->converters[ next ].handle;
                        control->calling = handle;
                    }
                    const bool claims = handle->claims( pathUtf8, *head );
                    bool claimed = false;
                    {
                        // A plugin unloaded during its call claims nothing: the
                        // next one is asked.
                        const std::lock_guard lock( control->mutex );
                        control->calling = nullptr;
                        claimed = claims && !control->converters[ next ].left;
                        if ( claimed ) {
                            control->claimedBy = control->converters[ next ].pluginId;
                        }
                    }
                    control->returned.notify_all();
                    if ( claimed ) {
                        break;
                    }
                }
            }
            QMetaObject::invokeMethod(
                this, [ this, id ] { finishContentCheck( id ); }, Qt::QueuedConnection );
        } );
    const auto id = check->id;
    contentChecks_.push_back( std::move( check ) );
    return id;
}

void PluginHost::cancelContentCheck( ContentCheckId id )
{
    for ( auto& check : contentChecks_ ) {
        if ( check->id == id ) {
            check->claimed = nullptr;
            const std::lock_guard lock( check->control->mutex );
            check->control->cancelled = true;
            return;
        }
    }
}

void PluginHost::leaveContentChecks( const PluginContext& context )
{
    for ( auto& check : contentChecks_ ) {
        auto& control = *check->control;
        std::unique_lock lock( control.mutex );
        for ( auto& converter : control.converters ) {
            if ( converter.handle == &context.handle ) {
                converter.left = true;
            }
        }
        control.returned.wait( lock, [ & ] { return control.calling != &context.handle; } );
    }
}

void PluginHost::finishContentCheck( ContentCheckId id )
{
    const auto it = std::ranges::find_if( contentChecks_,
                                          [ id ]( const auto& check ) { return check->id == id; } );
    if ( it == contentChecks_.end() ) {
        return;
    }
    auto check = std::move( *it );
    contentChecks_.erase( it );
    if ( check->thread.joinable() ) {
        check->thread.join();
    }

    auto claimedBy = [ &check ] {
        const std::lock_guard lock( check->control->mutex );
        return check->control->claimedBy;
    }();
    // Unloaded since it claimed the file: no plugin converts it.
    if ( !claimedBy.isEmpty() && !loadedContext( claimedBy ) ) {
        LOG_INFO << "Plugin " << claimedBy << " claimed a file and was unloaded meanwhile";
        claimedBy.clear();
    }
    // Last: the report may do anything, even unload the plugin.
    if ( check->claimed ) {
        check->claimed( claimedBy );
    }
}

QStringList PluginHost::converterFileFilters() const
{
    QStringList filters;
    for ( const auto& [ id, ctx ] : loaded_ ) {
        if ( ctx->unloadPending || !ctx->handle.isConverter() ) {
            continue;
        }
        const auto exts = ctx->handle.converterExtensions();
        if ( !exts.isEmpty() ) {
            // Build filter like "Plugin Name (*.har *.pcap)"
            auto extList = exts.split( ';', Qt::SkipEmptyParts );
            QStringList wildcards;
            for ( const auto& e : extList ) {
                auto trimmed = e.trimmed();
                if ( !trimmed.startsWith( '*' ) ) {
                    trimmed = "*" + trimmed;
                }
                wildcards << trimmed;
            }
            filters
                << QString( "%1 (%2)" ).arg( ctx->handle.metadata().name(), wildcards.join( ' ' ) );
        }
    }
    return filters;
}

ConversionId PluginHost::startConversion( const QString& pluginId, const QString& inputPath,
                                          const QString& outputPath, ConversionReport report )
{
    auto* ctx = loadedContext( pluginId );
    if ( !ctx || !ctx->handle.isConverter() ) {
        LOG_WARNING << "No converter plugin " << pluginId << " to convert " << inputPath;
        return 0;
    }
    const auto id = ++conversionsSoFar_;
    // Assigned one by one: GCC 13 warns of a designated initializer that
    // leaves members to their defaults (-Wmissing-field-initializers).
    auto conversion = std::make_unique<Conversion>();
    conversion->id = id;
    conversion->inputPath = inputPath;
    conversion->outputPath = outputPath;
    conversion->report = std::move( report );
    ctx->queuedConversions.push_back( std::move( conversion ) );
    startNextConversion( *ctx );
    return id;
}

void PluginHost::cancelConversion( ConversionId id )
{
    for ( auto& [ pluginId, ctx ] : loaded_ ) {
        if ( ctx->runningConversion && ctx->runningConversion->id == id ) {
            // The call returns when it polls, or at its end; it is finished
            // from the event loop then.
            ctx->runningConversion->control->cancelled.store( true );
            return;
        }
        auto& queued = ctx->queuedConversions;
        for ( auto it = queued.begin(); it != queued.end(); ++it ) {
            if ( ( *it )->id == id ) {
                auto conversion = std::move( *it );
                queued.erase( it );
                reportCancelled( *conversion );
                return;
            }
        }
    }
}

void PluginHost::startNextConversion( PluginContext& context )
{
    if ( context.runningConversion || context.queuedConversions.empty() || context.unloadPending ) {
        return;
    }
    context.runningConversion = std::move( context.queuedConversions.front() );
    context.queuedConversions.pop_front();

    auto& conversion = *context.runningConversion;
    conversion.control = std::make_shared<ConversionControl>( this, conversion.id );
    ++context.callsRunning;
    LOG_INFO << "Converting " << conversion.inputPath << " with plugin "
             << context.handle.metadata().id();

    // The call runs on a thread of its own and reports its return to the
    // host's thread. The handle stays: the context goes only after this
    // thread was joined.
    conversion.thread
        = std::thread( [ handle = &context.handle, control = conversion.control,
                         input = conversion.inputPath, output = conversion.outputPath ] {
              const int rc = handle->hasConvertEx()
                                 ? handle->convertEx( input, output, &control->table )
                                 : handle->convert( input, output );
              control->returnCode.store( rc );
              auto* host = control->pluginHost;
              QMetaObject::invokeMethod(
                  host,
                  [ host, id = control->id ] {
                      // Finished from here unless an unload finished it meanwhile.
                      if ( auto* converting = host->contextConverting( id ) ) {
                          host->finishRunningConversion( *converting );
                      }
                  },
                  Qt::QueuedConnection );
          } );

    if ( conversion.report.started ) {
        conversion.report.started();
    }
}

void PluginHost::finishRunningConversion( PluginContext& context )
{
    auto conversion = std::move( context.runningConversion );
    if ( !conversion ) {
        return;
    }
    if ( conversion->thread.joinable() ) {
        conversion->thread.join();
    }
    --context.callsRunning;

    const int rc = conversion->control->returnCode.load();
    ConversionEnd end;
    if ( conversion->control->cancelled.load() ) {
        end = ConversionEnd{ .outcome = ConversionOutcome::Cancelled, .returnCode = rc };
    }
    else if ( rc == 0 ) {
        end = ConversionEnd{ .outcome = ConversionOutcome::Succeeded, .returnCode = 0 };
    }
    else {
        LOG_ERROR << "Converter plugin " << context.handle.metadata().id()
                  << " failed with rc=" << rc << " converting " << conversion->inputPath;
        end = ConversionEnd{ .outcome = ConversionOutcome::Failed, .returnCode = rc };
    }

    startNextConversion( context );

    if ( context.callsRunning == 0 && context.unloadPending ) {
        // As callIntoPlugin finishes a pending unload: from the event loop.
        const auto pluginId = context.handle.metadata().id();
        QMetaObject::invokeMethod(
            this,
            [ this, pluginId ] {
                const auto it = loaded_.find( pluginId );
                if ( it != loaded_.end() && it->second->unloadPending
                     && it->second->callsRunning == 0 ) {
                    unloadNow( pluginId );
                }
            },
            Qt::QueuedConnection );
    }

    // Last: the report may do anything, even unload the plugin.
    if ( conversion->report.finished ) {
        conversion->report.finished( end );
    }
}

void PluginHost::cancelConversionsOf( PluginContext& context )
{
    auto queued = std::move( context.queuedConversions );
    context.queuedConversions.clear();
    if ( context.runningConversion ) {
        context.runningConversion->control->cancelled.store( true );
        // Waits for the call: a convert_ex returns once it polls, a plain
        // convert at its end. Its report comes before the queued ones'.
        finishRunningConversion( context );
    }
    for ( auto& conversion : queued ) {
        reportCancelled( *conversion );
    }
}

void PluginHost::reportCancelled( Conversion& conversion )
{
    if ( conversion.report.finished ) {
        conversion.report.finished(
            ConversionEnd{ .outcome = ConversionOutcome::Cancelled, .returnCode = 0 } );
    }
}

PluginHost::PluginContext* PluginHost::contextConverting( ConversionId id )
{
    for ( auto& [ pluginId, ctx ] : loaded_ ) {
        if ( ctx->runningConversion && ctx->runningConversion->id == id ) {
            return ctx.get();
        }
    }
    return nullptr;
}

void PluginHost::reportConversionProgress( const std::shared_ptr<ConversionControl>& control )
{
    // Cleared before the values are read: a report arriving after the read
    // queues a report of its own.
    control->progressQueued.store( false );
    const auto [ done, total ] = [ & ] {
        const std::lock_guard<std::mutex> lock( control->progressMutex );
        return control->progress;
    }();
    auto* ctx = contextConverting( control->id );
    if ( !ctx || ctx->runningConversion->control != control ) {
        return;
    }
    if ( ctx->runningConversion->report.progress ) {
        ctx->runningConversion->report.progress( done, total );
    }
}

void PluginHost::hostReportProgress( const LogSquirlConvertControl* control, uint64_t done,
                                     uint64_t total )
{
    if ( !control || !control->host ) {
        return;
    }
    auto* conversion = static_cast<ConversionControl*>( control->host );
    {
        const std::lock_guard<std::mutex> lock( conversion->progressMutex );
        conversion->progress = { done, total };
    }
    if ( conversion->progressQueued.exchange( true ) ) {
        return;
    }
    auto* host = conversion->pluginHost;
    QMetaObject::invokeMethod(
        host,
        [ host, shared = conversion->shared_from_this() ] {
            host->reportConversionProgress( shared );
        },
        Qt::QueuedConnection );
}

int PluginHost::hostIsCancelled( const LogSquirlConvertControl* control )
{
    if ( !control || !control->host ) {
        return 0;
    }
    return static_cast<ConversionControl*>( control->host )->cancelled.load() ? 1 : 0;
}

// ── Host API construction ───────────────────────────────────────────────────

LogSquirlHostApi PluginHost::buildHostApi()
{
    LogSquirlHostApi api{};
    api.api_version = LOGSQUIRL_PLUGIN_API_VERSION;

    api.push_line = &PluginHost::hostPushLine;
    api.push_lines = &PluginHost::hostPushLines;
    api.signal_eos = &PluginHost::hostSignalEos;
    api.signal_error = &PluginHost::hostSignalError;
    api.log_message = &PluginHost::hostLogMessage;
    api.get_config_dir = &PluginHost::hostGetConfigDir;
    api.show_notification = &PluginHost::hostShowNotification;
    api.open_file = &PluginHost::hostOpenFile;
    api.register_status_widget = &PluginHost::hostRegisterStatusWidget;
    api.unregister_status_widget = &PluginHost::hostUnregisterStatusWidget;
    api.register_menu_action = &PluginHost::hostRegisterMenuAction;
    api.register_sidebar_tab = &PluginHost::hostRegisterSidebarTab;
    api.unregister_sidebar_tab = &PluginHost::hostUnregisterSidebarTab;
    api.register_footer_widget = &PluginHost::hostRegisterFooterWidget;
    api.unregister_footer_widget = &PluginHost::hostUnregisterFooterWidget;
    api.get_active_file_path = &PluginHost::hostGetActiveFilePath;
    api.register_active_file_callback = &PluginHost::hostRegisterActiveFileCallback;
    api.open_regex_lab = &PluginHost::hostOpenRegexLab;
    api.go_to_log_line = &PluginHost::hostGoToLogLine;
    api.get_selected_log_lines = &PluginHost::hostGetSelectedLogLines;
    api.get_log_file_encoding = &PluginHost::hostGetLogFileEncoding;
    api.register_encoding_callback = &PluginHost::hostRegisterEncodingCallback;
    api.register_menu_action_ex = &PluginHost::hostRegisterMenuActionEx;
    api.unregister_menu_action = &PluginHost::hostUnregisterMenuAction;
    api.get_log_line_count = &PluginHost::hostGetLogLineCount;
    api.get_log_lines = &PluginHost::hostGetLogLines;
    api.register_log_lines_callback = &PluginHost::hostRegisterLogLinesCallback;
    api.register_context_menu_action = &PluginHost::hostRegisterContextMenuAction;
    api.unregister_context_menu_action = &PluginHost::hostUnregisterContextMenuAction;
    api.add_marks = &PluginHost::hostAddMarks;
    api.remove_marks = &PluginHost::hostRemoveMarks;
    api.get_marks = &PluginHost::hostGetMarks;
    api.get_log_format_fields = &PluginHost::hostGetLogFormatFields;
    api.get_log_line_fields = &PluginHost::hostGetLogLineFields;

    return api;
}

// ── Host API trampolines ────────────────────────────────────────────────────
//
// Each trampoline receives a void* handle that is actually a PluginContext*.
// From there we can access the PluginHost and route the call.

// Extract the PluginContext from the opaque handle.
auto PluginHost::contextFromHandle( void* handle ) -> PluginContext*
{
    return static_cast<PluginContext*>( handle );
}

// The pushes run under the context's stream mutex: the host ends the stream
// under it too, so none finds a stream that is going (#821).
void PluginHost::hostPushLine( void* handle, const char* data, size_t len )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    const std::lock_guard lock( ctx->streamMutex );
    if ( !ctx->stream ) {
        return;
    }
    ctx->stream->pushLine( data, len );
}

void PluginHost::hostPushLines( void* handle, const char* const* data, const size_t* lens,
                                size_t count )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    const std::lock_guard lock( ctx->streamMutex );
    if ( !ctx->stream ) {
        return;
    }
    ctx->stream->pushLines( data, lens, count );
}

void PluginHost::hostSignalEos( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    // The plugin's word is final: the stream takes no more lines from now
    // on, also none its stop pushes.
    {
        const std::lock_guard lock( ctx->streamMutex );
        if ( !ctx->stream ) {
            return;
        }
        ctx->stream->signalEos();
        ctx->streamEndedByPlugin = true;
    }
    LOG_INFO << "DataSource EOS from plugin";
    endStreamThePluginEndedLater( *ctx );
}

void PluginHost::endStreamThePluginEndedLater( PluginContext& context )
{
    // Ended on the host's thread, as every other time a data source stops: a
    // plugin may signal from a thread of its own, its stop runs on the UI
    // thread, and a directly connected slot would run on the plugin's.
    auto* host = context.host;
    if ( !host ) {
        return;
    }
    QMetaObject::invokeMethod(
        host,
        [ host, loadSerial = context.loadSerial, pluginId = context.handle.metadata().id() ] {
            host->endStreamThePluginEnded( pluginId, loadSerial );
        },
        Qt::QueuedConnection );
}

void PluginHost::endStreamThePluginEnded( const QString& pluginId, std::uint64_t loadSerial )
{
    // The plugin may be unloaded before the queued call runs, and loaded
    // again: the unload stopped it and said the stream stopped (#843).
    auto* loaded = loadedContext( pluginId );
    if ( !loaded || loaded->loadSerial != loadSerial ) {
        LOG_INFO << "Plugin " << pluginId << " was unloaded; dropping its queued end of stream";
        return;
    }
    auto& context = *loaded;
    {
        // Stopped meanwhile, and maybe started again: that stream runs on.
        const std::lock_guard lock( context.streamMutex );
        if ( !context.stream || !context.streamEndedByPlugin ) {
            return;
        }
    }
    // A start under way, its dialog turning the event loop, asks again once
    // it has returned; a stop under way ends the stream as it returns.
    if ( context.dataSourceState != DataSourceState::Running ) {
        return;
    }
    LOG_INFO << "Ending DataSource the plugin ended: " << pluginId;
    stopInPlugin( context );
    endStream( context, pluginId );
}

void PluginHost::hostSignalError( void* handle, const char* message )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    LOG_ERROR << "Plugin error: " << message;
}

void PluginHost::hostLogMessage( void* handle, int level, const char* message )
{
    Q_UNUSED( handle );
    switch ( static_cast<LogSquirlLogLevel>( level ) ) {
    case LOGSQUIRL_LOG_TRACE:
    case LOGSQUIRL_LOG_DEBUG:
        LOG_DEBUG << "[plugin] " << message;
        break;
    case LOGSQUIRL_LOG_INFO:
        LOG_INFO << "[plugin] " << message;
        break;
    case LOGSQUIRL_LOG_WARNING:
        LOG_WARNING << "[plugin] " << message;
        break;
    case LOGSQUIRL_LOG_ERROR:
    case LOGSQUIRL_LOG_CRITICAL:
        LOG_ERROR << "[plugin] " << message;
        break;
    }
}

const char* PluginHost::hostGetConfigDir( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return "";
    }
    return ctx->configDirUtf8.constData();
}

void PluginHost::hostShowNotification( void* handle, const char* message )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host ) {
        return;
    }
    const auto msg = QString::fromUtf8( message );
    LOG_INFO << "[plugin notification] " << msg;
    Q_EMIT ctx->host->notificationRequested( msg );
}

void PluginHost::hostOpenFile( void* handle, const char* filePath, int follow )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host ) {
        return;
    }
    auto* host = ctx->host;
    if ( QThread::currentThread() == host->thread() ) {
        if ( host->openFileCallback_ ) {
            host->openFileCallback_( QString::fromUtf8( filePath ), follow != 0 );
        }
        return;
    }
    // A plugin may call from a thread of its own; the file opens in a tab,
    // on the UI thread only. The plugin may be unloaded before the queued
    // call runs, and loaded again: then nothing of that load is opened any
    // more.
    QMetaObject::invokeMethod(
        host,
        [ host, loadSerial = ctx->loadSerial, pluginId = ctx->handle.metadata().id(),
          path = QString::fromUtf8( filePath ), following = follow != 0 ] {
            const auto* loaded = host->loadedContext( pluginId );
            if ( !loaded || loaded->loadSerial != loadSerial ) {
                LOG_INFO << "Plugin " << pluginId
                         << " was unloaded; dropping its queued open_file call";
                return;
            }
            if ( host->openFileCallback_ ) {
                host->openFileCallback_( path, following );
            }
        },
        Qt::QueuedConnection );
}

auto PluginHost::uiPortFor( void* handle ) -> std::pair<PluginUiPort*, QString>
{
    const auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || !ctx->host->uiPort_ ) {
        return { nullptr, {} };
    }
    return { ctx->host->uiPort_, ctx->handle.metadata().id() };
}

// The widget trampolines wrap the plugin's void* in a PluginWidgetHandle
// without looking at it; only the port's implementation knows it is a widget.

void PluginHost::hostRegisterStatusWidget( void* handle, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->addStatusWidget( pluginId, PluginWidgetHandle{ qwidgetPtr } );
    }
}

void PluginHost::hostUnregisterStatusWidget( void* handle, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->removeStatusWidget( pluginId, PluginWidgetHandle{ qwidgetPtr } );
    }
}

void PluginHost::hostRegisterMenuAction( void* handle, const char* menuPath, const char* label,
                                         PluginCallbackFn callback, void* userData )
{
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return;
    }
    auto* ctx = contextFromHandle( handle );
    const auto menuPathText = QString::fromUtf8( menuPath );
    const auto labelText = QString::fromUtf8( label );
    auto submenu = pluginSubmenuNames( menuPathText );
    MenuActionCall* call = nullptr;
    {
        const std::lock_guard lock( ctx->registrationsMutex );
        // A call for a submenu and label the plugin has an item of adds
        // nothing, and so keeps no record either (#870).
        if ( hasLiveMenuAction( *ctx, submenu, labelText ) ) {
            return;
        }
        // Kept for its submenu and label also without a callback:
        // register_menu_action_ex adds no second item of them.
        call = &ctx->menuActionCalls.emplace_back();
        call->context = ctx;
        call->callback = callback;
        call->userData = userData;
        call->label = labelText;
        call->submenu = std::move( submenu );
    }
    if ( !callback ) {
        port->addMenuAction( pluginId, menuPathText, labelText, nullptr, userData );
        return;
    }
    port->addMenuAction( pluginId, menuPathText, labelText, &PluginHost::runMenuAction, call );
}

int PluginHost::nextRegistrationId( PluginContext& context )
{
    if ( context.registrationIdsSoFar == std::numeric_limits<int>::max() ) {
        return -1;
    }
    return ++context.registrationIdsSoFar;
}

bool PluginHost::hasLiveMenuAction( const PluginContext& context, const QStringList& submenu,
                                    const QString& label )
{
    return std::ranges::any_of( context.menuActionCalls, [ & ]( const auto& other ) {
        return !other.removed.load() && other.label == label && other.submenu == submenu;
    } );
}

int PluginHost::hostRegisterMenuActionEx( void* handle, const char* menuPath, const char* label,
                                          PluginCallbackFn callback, void* userData )
{
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port || label == nullptr || *label == '\0' || callback == nullptr ) {
        return -1;
    }
    auto* ctx = contextFromHandle( handle );
    const auto menuPathText = QString::fromUtf8( menuPath );
    const auto labelText = QString::fromUtf8( label );
    const auto submenu = pluginSubmenuNames( menuPathText );
    MenuActionCall* call = nullptr;
    int id = -1;
    {
        const std::lock_guard lock( ctx->registrationsMutex );
        // A plugin's items of one submenu have different labels: the Plugin
        // UI Port shows a second one of the same label there not at all (#870).
        id = hasLiveMenuAction( *ctx, submenu, labelText ) ? -1 : nextRegistrationId( *ctx );
        if ( id < 0 ) {
            return -1;
        }
        call = &ctx->menuActionCalls.emplace_back();
        call->context = ctx;
        call->callback = callback;
        call->userData = userData;
        call->label = labelText;
        call->submenu = submenu;
        call->id = id;
        ctx->menuActionsById.emplace( id, call );
    }
    port->addMenuAction( pluginId, menuPathText, labelText, &PluginHost::runMenuAction, call );
    return id;
}

void PluginHost::hostUnregisterMenuAction( void* handle, int id )
{
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return;
    }
    auto* ctx = contextFromHandle( handle );
    MenuActionCall* call = nullptr;
    {
        const std::lock_guard lock( ctx->registrationsMutex );
        const auto it = ctx->menuActionsById.find( id );
        if ( it == ctx->menuActionsById.end() ) {
            return;
        }
        call = it->second;
        ctx->menuActionsById.erase( it );
        // From here on no trigger reaches the plugin, one queued already
        // included; the record stays for such a trigger to find.
        call->removed.store( true );
    }
    port->removeMenuAction( pluginId, call );
}

void PluginHost::hostRegisterSidebarTab( void* handle, const char* label, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->addSidebarTab( pluginId, QString::fromUtf8( label ),
                             PluginWidgetHandle{ qwidgetPtr } );
    }
}

void PluginHost::hostUnregisterSidebarTab( void* handle, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->removeSidebarTab( pluginId, PluginWidgetHandle{ qwidgetPtr } );
    }
}

void PluginHost::hostRegisterFooterWidget( void* handle, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->addFooterWidget( pluginId, PluginWidgetHandle{ qwidgetPtr } );
    }
}

void PluginHost::hostUnregisterFooterWidget( void* handle, void* qwidgetPtr )
{
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->removeFooterWidget( pluginId, PluginWidgetHandle{ qwidgetPtr } );
    }
}

const char* PluginHost::hostGetActiveFilePath( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host ) {
        return "";
    }
    // Checked first: the tab in front is only read, and the cache only
    // written, on the UI thread. Not dispatched with a blocking call: a
    // worker waiting for the UI thread while it waits for the worker would
    // deadlock. The answer is the one without a file.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " asked for the active file path off the UI thread; none answered";
        return "";
    }
    if ( !ctx->host->activeFilePathCallback_ ) {
        return "";
    }
    // The plugin's own: another plugin's call leaves it as it is (#857).
    ctx->activeFilePathUtf8 = ctx->host->activeFilePathCallback_().toUtf8();
    return ctx->activeFilePathUtf8.constData();
}

void PluginHost::hostRegisterActiveFileCallback(
    void* handle, void ( *callback )( void* user_data, const char* file_path ), void* user_data )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    const std::lock_guard lock( ctx->registrationsMutex );
    ctx->activeFileCallback = callback;
    ctx->activeFileUserData = user_data;
}

int PluginHost::hostOpenRegexLab( void* handle, const char* pattern, int flags,
                                  LogSquirlRegexLabCallbackFn callback, void* userData )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host ) {
        return -1;
    }
    // A window opens on the UI thread only, and it is there the answer comes.
    // Checked first: the answer context is only touched on that thread.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " opened the Regex Lab off the UI thread; not opened";
        return -1;
    }
    if ( !ctx->answerContext || !callback ) {
        return -1;
    }
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return -1;
    }

    const PluginPattern opened{ .pattern = QString::fromUtf8( pattern ? pattern : "" ),
                                .matchesCase = ( flags & LOGSQUIRL_REGEX_LAB_MATCH_CASE ) != 0 };
    // Called through the answer context only, so never after the plugin is
    // shut down. An answer that comes while the plugin's unload is pending
    // is kept, the pattern with it, in case the plugin stays (#801).
    auto answer = [ ctx, callback, userData ]( const std::optional<PluginPattern>& applied ) {
        ctx->host->answerPlugin( *ctx, [ applied, callback, userData ] {
            if ( !applied ) {
                callback( userData, LOGSQUIRL_REGEX_LAB_CANCELLED, nullptr, 0 );
                return;
            }
            const auto appliedUtf8 = applied->pattern.toUtf8();
            callback( userData, LOGSQUIRL_REGEX_LAB_APPLIED, appliedUtf8.constData(),
                      applied->matchesCase ? LOGSQUIRL_REGEX_LAB_MATCH_CASE : 0 );
        } );
    };
    return port->openRegexLab( pluginId, opened, ctx->answerContext.get(), std::move( answer ) )
               ? 0
               : -1;
}

namespace {

/// The selected Log Lines as get_selected_log_lines hands them over: whole
/// lines joined by '\n' up to the byte bound, a first line longer than that
/// cut at a character.
struct SelectedLogLinesText {
    QByteArray text;
    size_t lineCount = 0;
    bool truncated = false;
};

/// UTF-8 text longer than maxBytes cut to at most that many, not in the
/// middle of a character: back to its first byte.
void cutAtCharacter( QByteArray& utf8, qsizetype maxBytes )
{
    if ( utf8.size() <= maxBytes ) {
        return;
    }
    auto cut = maxBytes;
    while ( cut > 0 && ( static_cast<unsigned char>( utf8[ cut ] ) & 0xC0 ) == 0x80 ) {
        --cut;
    }
    utf8.truncate( cut );
}

SelectedLogLinesText selectedLogLinesText( const QStringList& lines, bool more )
{
    constexpr auto MaxBytes = static_cast<qsizetype>( LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
    SelectedLogLinesText result;
    result.truncated = more;
    for ( const auto& line : lines ) {
        auto utf8 = line.toUtf8();
        const qsizetype separator = result.lineCount > 0 ? 1 : 0;
        if ( result.text.size() + separator + utf8.size() > MaxBytes ) {
            result.truncated = true;
            if ( result.lineCount == 0 ) {
                cutAtCharacter( utf8, MaxBytes );
                result.text = std::move( utf8 );
                result.lineCount = 1;
            }
            break;
        }
        if ( separator != 0 ) {
            result.text.append( '\n' );
        }
        result.text.append( utf8 );
        ++result.lineCount;
    }
    return result;
}

/// What a port or a Log File read for a plugin, as the plugin gets it: at
/// most LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES Log Lines, whole ones but for
/// a first one cut, and truncated when there were more of those asked for.
/// read holds at most one Log Line more than the bound, which says there are.
SelectedLogLinesText logLinesTextFor( PluginSelectedLogLines read )
{
    auto& lines = read.lines;
    bool more = read.more || read.lastCut;
    if ( lines.size() > LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES ) {
        lines.resize( LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
        more = true;
    }
    // Only a first Log Line comes cut: one cut after whole ones is left out.
    else if ( read.lastCut && lines.size() > 1 ) {
        lines.removeLast();
    }
    return selectedLogLinesText( lines, more );
}

/// Nothing is returned unless the text is.
void clearLogLinesOutputs( const char** text, size_t* length, size_t* lineCount )
{
    if ( text != nullptr ) {
        *text = nullptr;
    }
    if ( length != nullptr ) {
        *length = 0;
    }
    if ( lineCount != nullptr ) {
        *lineCount = 0;
    }
}

/// The fields of Log Lines a Log File read for a plugin cut to what the
/// plugin gets: at most LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES Log Lines
/// whose values are at most LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES together,
/// whole Log Lines but for a first one whose values are cut. read holds at
/// most one Log Line more than the bound, which says there are more. Returns
/// whether fewer Log Lines are left than were asked for and there are.
bool boundLogLineFields( PluginLogLineFields& read )
{
    constexpr auto MaxBytes = static_cast<qsizetype>( LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
    const auto fieldCount = read.fieldCount;
    auto lines = read.parsed.size();
    bool truncated = read.more;
    if ( lines > LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES ) {
        lines = LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES;
        truncated = true;
    }
    qsizetype bytes = 0;
    for ( size_t line = 0; line < lines; ++line ) {
        const auto first = read.values.begin() + static_cast<std::ptrdiff_t>( line * fieldCount );
        const auto lineBytes = std::accumulate(
            first, first + static_cast<std::ptrdiff_t>( fieldCount ), qsizetype{ 0 },
            []( qsizetype sum, const QByteArray& value ) { return sum + value.size(); } );
        if ( bytes + lineBytes <= MaxBytes ) {
            bytes += lineBytes;
            continue;
        }
        truncated = true;
        if ( line > 0 ) {
            lines = line;
            break;
        }
        // A first Log Line too long for the bound: its values as far as they
        // fit, each cut at a character, and those after the cut empty.
        auto left = MaxBytes;
        for ( auto value = first; value != first + static_cast<std::ptrdiff_t>( fieldCount );
              ++value ) {
            cutAtCharacter( *value, left );
            left -= value->size();
        }
        lines = 1;
        break;
    }
    read.parsed.resize( lines );
    read.values.resize( lines * fieldCount );
    return truncated;
}

} // namespace

int PluginHost::hostGoToLogLine( void* handle, uint64_t lineNumber )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    // Checked first: the views are only touched on the UI thread.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " went to a Log Line off the UI thread; not done";
        return LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD;
    }
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    // The plugin counts as the view's line numbers do, from 1; the Log File
    // from 0.
    if ( lineNumber == 0 ) {
        return LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    }
    switch ( port->goToLogLine( lineNumber - 1 ) ) {
    case PluginLogLineJump::Shown:
        return LOGSQUIRL_LOG_LINES_OK;
    case PluginLogLineJump::OutOfRange:
        return LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    case PluginLogLineJump::NoLogFile:
        break;
    }
    return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
}

int PluginHost::hostGetSelectedLogLines( void* handle, const char** text, size_t* length,
                                         size_t* lineCount )
{
    clearLogLinesOutputs( text, length, lineCount );

    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || text == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    // Checked first: the views and the cached text are only touched on the
    // UI thread.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " read the selected Log Lines off the UI thread; not read";
        return LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD;
    }
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }

    // One line more than returned says whether there are more; the port
    // reads no more than the byte bound lets through.
    auto selected = port->selectedLogLines( LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES + 1,
                                            LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
    if ( !selected ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    if ( selected->lines.isEmpty() ) {
        return LOGSQUIRL_LOG_LINES_NO_SELECTION;
    }

    auto result = logLinesTextFor( std::move( *selected ) );
    ctx->selectedLogLinesUtf8 = std::move( result.text );
    *text = ctx->selectedLogLinesUtf8.constData();
    if ( length != nullptr ) {
        *length = static_cast<size_t>( ctx->selectedLogLinesUtf8.size() );
    }
    if ( lineCount != nullptr ) {
        *lineCount = result.lineCount;
    }
    return result.truncated ? LOGSQUIRL_LOG_LINES_TRUNCATED : LOGSQUIRL_LOG_LINES_OK;
}

int PluginHost::hostGetLogFileEncoding( void* handle, const char* filePath, const char** encoding )
{
    // Nothing is returned unless the name is.
    if ( encoding != nullptr ) {
        *encoding = nullptr;
    }

    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || encoding == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    // Checked first: the tabs and the cached name are only touched on the UI
    // thread.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " asked for a Log File's encoding off the UI thread; none answered";
        return LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD;
    }
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }

    // NULL and "" both name the tab in front.
    const auto name = port->logFileEncoding( QString::fromUtf8( filePath ? filePath : "" ) );
    if ( !name ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    ctx->logFileEncodingUtf8 = name->toUtf8();
    *encoding = ctx->logFileEncodingUtf8.constData();
    return LOGSQUIRL_LOG_LINES_OK;
}

void PluginHost::hostRegisterEncodingCallback( void* handle, LogSquirlEncodingCallbackFn callback,
                                               void* userData )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    const std::lock_guard lock( ctx->registrationsMutex );
    ctx->encodingCallback = callback;
    ctx->encodingUserData = userData;
}

// The open Log Files are found on the plugin's thread, never through the UI
// thread: a worker waiting for it while it waits for the worker would
// deadlock. What is found is held to the end of the call, so a tab closing
// meanwhile takes nothing away under the read; it answers as a closed one.

int PluginHost::hostGetLogLineCount( void* handle, const char* filePath, uint64_t* count )
{
    if ( count != nullptr ) {
        *count = 0;
    }
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || count == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    const auto logFile = ctx->host->logFiles_->find( QString::fromUtf8( filePath ) );
    if ( !logFile ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    const auto lineCount = logFile->source().lineCount();
    if ( !logFile->isOpen() ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    *count = lineCount;
    return LOGSQUIRL_LOG_LINES_OK;
}

int PluginHost::hostGetLogLines( void* handle, const char* filePath, uint64_t firstLine,
                                 size_t maxLines, const char** text, size_t* length,
                                 size_t* lineCount )
{
    clearLogLinesOutputs( text, length, lineCount );

    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || text == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    const auto logFile = ctx->host->logFiles_->find( QString::fromUtf8( filePath ) );
    if ( !logFile ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    // The plugin counts as the views' line numbers do, from 1; the Log File
    // from 0.
    if ( firstLine == 0 ) {
        return LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    }

    // One line more than returned says whether there are more; the source
    // reads no more than the byte bound lets through.
    const auto asked = std::min<size_t>( maxLines, LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES + 1 );
    auto read = logFile->source().readLogLines( firstLine - 1, asked,
                                                LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
    if ( !logFile->isOpen() ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    if ( !read ) {
        return LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    }

    auto result = logLinesTextFor( std::move( *read ) );
    // The calling thread's own: another thread of the plugin's keeps its text.
    auto& buffer = ctx->logLinesUtf8.ofThisThread();
    buffer = std::move( result.text );
    *text = buffer.constData();
    if ( length != nullptr ) {
        *length = static_cast<size_t>( buffer.size() );
    }
    if ( lineCount != nullptr ) {
        *lineCount = result.lineCount;
    }
    return result.truncated ? LOGSQUIRL_LOG_LINES_TRUNCATED : LOGSQUIRL_LOG_LINES_OK;
}

void PluginHost::hostRegisterLogLinesCallback( void* handle, LogSquirlLogLinesCallbackFn callback,
                                               void* userData )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    const std::lock_guard lock( ctx->registrationsMutex );
    ctx->logLinesCallback = callback;
    ctx->logLinesUserData = userData;
}

int PluginHost::hostRegisterContextMenuAction( void* handle, const char* menuPath,
                                               const char* label,
                                               LogSquirlContextMenuCallbackFn callback,
                                               void* userData )
{
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port || label == nullptr || *label == '\0' || callback == nullptr ) {
        return -1;
    }
    auto* ctx = contextFromHandle( handle );
    ContextMenuActionCall* call = nullptr;
    int id = -1;
    {
        // The id from the counter every registration with an id shares (#857).
        const std::lock_guard lock( ctx->registrationsMutex );
        id = nextRegistrationId( *ctx );
        if ( id < 0 ) {
            return -1;
        }
        call = &ctx->contextMenuActionCalls.emplace_back();
        call->context = ctx;
        call->callback = callback;
        call->userData = userData;
        call->id = id;
        ctx->contextMenuActionsById.emplace( id, call );
    }
    port->addContextMenuAction( pluginId, QString::fromUtf8( menuPath ), QString::fromUtf8( label ),
                                &PluginHost::runContextMenuAction, call );
    return id;
}

void PluginHost::hostUnregisterContextMenuAction( void* handle, int id )
{
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return;
    }
    auto* ctx = contextFromHandle( handle );
    ContextMenuActionCall* call = nullptr;
    {
        const std::lock_guard lock( ctx->registrationsMutex );
        const auto it = ctx->contextMenuActionsById.find( id );
        if ( it == ctx->contextMenuActionsById.end() ) {
            return;
        }
        call = it->second;
        ctx->contextMenuActionsById.erase( it );
        // From here on no choice of the item reaches the plugin, one already
        // waiting for the event loop included; the record stays for it.
        call->removed.store( true );
    }
    port->removeContextMenuAction( pluginId, call );
}

// The Marks are the tabs' own, so they are set on the UI thread only; the
// plugin is told at once whether its Log File is open, found as the Log Lines
// are, without waiting for the UI thread (#866).
int PluginHost::changeMarks( void* handle, const char* filePath, const uint64_t* lines,
                             size_t count, MarksChange change )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || ( lines == nullptr && count > 0 )
         || count > LOGSQUIRL_MARKS_MAX_LINES ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    auto path = QString::fromUtf8( filePath );
    if ( !ctx->host->logFiles_->find( path ) ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }

    // The plugin counts as the views' line numbers do, from 1; the Log File
    // from 0. A line of 0 is skipped, as one past the end is by the tabs.
    std::vector<std::uint64_t> logLines;
    logLines.reserve( count );
    for ( size_t i = 0; i < count; ++i ) {
        if ( lines[ i ] > 0 ) {
            logLines.push_back( lines[ i ] - 1 );
        }
    }
    if ( logLines.empty() ) {
        return LOGSQUIRL_LOG_LINES_OK;
    }

    auto* host = ctx->host;
    const auto carryOut
        = [ host, change ]( const QString& logFile, const std::vector<std::uint64_t>& marked ) {
              if ( !host->uiPort_ ) {
                  return;
              }
              if ( change == MarksChange::Add ) {
                  host->uiPort_->addMarks( logFile, marked );
              }
              else {
                  host->uiPort_->removeMarks( logFile, marked );
              }
          };
    if ( QThread::currentThread() == host->thread() ) {
        carryOut( path, logLines );
        return LOGSQUIRL_LOG_LINES_OK;
    }
    // Queued in the order the plugin's thread called; the plugin may be
    // unloaded before the call runs, and loaded again: then nothing of that
    // load is marked any more.
    QMetaObject::invokeMethod(
        host,
        [ host, carryOut, loadSerial = ctx->loadSerial, pluginId = ctx->handle.metadata().id(),
          path = std::move( path ), logLines = std::move( logLines ) ] {
            const auto* loaded = host->loadedContext( pluginId );
            if ( !loaded || loaded->loadSerial != loadSerial ) {
                LOG_INFO << "Plugin " << pluginId
                         << " was unloaded; dropping its queued change of Marks";
                return;
            }
            carryOut( path, logLines );
        },
        Qt::QueuedConnection );
    return LOGSQUIRL_LOG_LINES_OK;
}

int PluginHost::hostAddMarks( void* handle, const char* filePath, const uint64_t* lines,
                              size_t count )
{
    return changeMarks( handle, filePath, lines, count, MarksChange::Add );
}

int PluginHost::hostRemoveMarks( void* handle, const char* filePath, const uint64_t* lines,
                                 size_t count )
{
    return changeMarks( handle, filePath, lines, count, MarksChange::Remove );
}

int PluginHost::hostGetMarks( void* handle, const char* filePath, const uint64_t** lines,
                              size_t* count )
{
    // Nothing is returned unless the Marks are.
    if ( lines != nullptr ) {
        *lines = nullptr;
    }
    if ( count != nullptr ) {
        *count = 0;
    }
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || lines == nullptr || count == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    // Checked first: the tabs and the cached array are only touched on the UI
    // thread.
    if ( QThread::currentThread() != ctx->host->thread() ) {
        LOG_WARNING << "Plugin " << ctx->handle.metadata().id()
                    << " read the Marks off the UI thread; none read";
        return LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD;
    }
    const auto [ port, pluginId ] = uiPortFor( handle );
    if ( !port ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    auto marks = port->marks( QString::fromUtf8( filePath ) );
    if ( !marks ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }

    // Numbered from 1, as the plugin counts.
    ctx->marks = std::move( *marks );
    for ( auto& line : ctx->marks ) {
        ++line;
    }
    *lines = ctx->marks.empty() ? nullptr : ctx->marks.data();
    *count = ctx->marks.size();
    return LOGSQUIRL_LOG_LINES_OK;
}

// The fields of an open Log File are read as its Log Lines are (#867): found
// on the plugin's thread, held to the end of the call, and kept per thread.

int PluginHost::hostGetLogFormatFields( void* handle, const char* filePath, const char** formatName,
                                        const char* const** fieldNames, size_t* fieldCount )
{
    // Nothing is returned unless the fields are.
    if ( formatName != nullptr ) {
        *formatName = nullptr;
    }
    if ( fieldNames != nullptr ) {
        *fieldNames = nullptr;
    }
    if ( fieldCount != nullptr ) {
        *fieldCount = 0;
    }
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || fieldNames == nullptr
         || fieldCount == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    const auto logFile = ctx->host->logFiles_->find( QString::fromUtf8( filePath ) );
    if ( !logFile ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    const auto format = logFile->source().logFormat();
    if ( !logFile->isOpen() ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    if ( !format ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT;
    }

    // The calling thread's own: another thread of the plugin's keeps its names.
    auto& buffer = ctx->logFormatFields.ofThisThread();
    buffer.name = format->name.toUtf8();
    buffer.fieldNames.clear();
    for ( const auto& name : format->fieldNames ) {
        buffer.fieldNames.push_back( name.toUtf8() );
    }
    buffer.namePointers.clear();
    for ( const auto& name : buffer.fieldNames ) {
        buffer.namePointers.push_back( name.constData() );
    }
    if ( formatName != nullptr ) {
        *formatName = buffer.name.constData();
    }
    *fieldNames = buffer.namePointers.empty() ? nullptr : buffer.namePointers.data();
    *fieldCount = buffer.namePointers.size();
    return LOGSQUIRL_LOG_LINES_OK;
}

int PluginHost::hostGetLogLineFields( void* handle, const char* filePath, uint64_t firstLine,
                                      size_t maxLines, const char* const** values,
                                      const size_t** lengths, const int** parsed,
                                      size_t* fieldCount, size_t* lineCount )
{
    // Nothing is returned unless the values are.
    if ( values != nullptr ) {
        *values = nullptr;
    }
    if ( lengths != nullptr ) {
        *lengths = nullptr;
    }
    if ( parsed != nullptr ) {
        *parsed = nullptr;
    }
    if ( fieldCount != nullptr ) {
        *fieldCount = 0;
    }
    if ( lineCount != nullptr ) {
        *lineCount = 0;
    }
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->host || filePath == nullptr || values == nullptr || fieldCount == nullptr
         || lineCount == nullptr ) {
        return LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT;
    }
    const auto logFile = ctx->host->logFiles_->find( QString::fromUtf8( filePath ) );
    if ( !logFile ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    // Numbered from 1 by the plugin, from 0 by the Log File.
    if ( firstLine == 0 ) {
        return LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    }

    // One Log Line more than returned says whether there are more.
    const auto asked = std::min<size_t>( maxLines, LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES + 1 );
    auto read = logFile->source().readLogLineFields( firstLine - 1, asked,
                                                     LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES );
    if ( !logFile->isOpen() ) {
        return LOGSQUIRL_LOG_LINES_NO_LOG_FILE;
    }
    if ( !read ) {
        return read.error() == PluginLogLineFieldsUnread::NoLogFormat
                   ? LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT
                   : LOGSQUIRL_LOG_LINES_OUT_OF_RANGE;
    }
    const auto truncated = boundLogLineFields( *read );

    // The calling thread's own: another thread of the plugin's keeps its
    // values. Every value follows the one before, with its NUL, in one text.
    auto& buffer = ctx->logLineFields.ofThisThread();
    const auto fields = read->fieldCount;
    buffer.text.clear();
    buffer.valueLengths.clear();
    for ( const auto& value : read->values ) {
        buffer.text.append( value );
        buffer.text.append( '\0' );
        buffer.valueLengths.push_back( static_cast<size_t>( value.size() ) );
    }
    buffer.valuePointers.clear();
    buffer.parsed.clear();
    const char* next = buffer.text.constData();
    for ( size_t line = 0; line < read->parsed.size(); ++line ) {
        const bool lineParsed = read->parsed[ line ];
        buffer.parsed.push_back( lineParsed ? 1 : 0 );
        for ( size_t field = 0; field < fields; ++field ) {
            const auto length = buffer.valueLengths[ line * fields + field ];
            buffer.valuePointers.push_back( lineParsed ? next : nullptr );
            next += length + 1;
        }
    }

    if ( !buffer.valuePointers.empty() ) {
        *values = buffer.valuePointers.data();
        if ( lengths != nullptr ) {
            *lengths = buffer.valueLengths.data();
        }
    }
    if ( parsed != nullptr && !buffer.parsed.empty() ) {
        *parsed = buffer.parsed.data();
    }
    *fieldCount = fields;
    *lineCount = buffer.parsed.size();
    return truncated ? LOGSQUIRL_LOG_LINES_TRUNCATED : LOGSQUIRL_LOG_LINES_OK;
}

} // namespace logsquirl::plugins
