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

#include "log.h"
#include "streamwriter.h"

#include <QDir>
#include <QStandardPaths>

namespace logsquirl::plugins {

// ── Construction / destruction ──────────────────────────────────────────────

PluginHost::PluginHost( const PluginCatalog& catalog, QObject* parent )
    : QObject( parent )
    , catalog_( catalog )
{
}

PluginHost::~PluginHost()
{
    unloadAll();
}

// ── Loading / unloading ─────────────────────────────────────────────────────

QStringList PluginHost::loadedPluginIds() const
{
    QStringList ids;
    ids.reserve( static_cast<int>( loaded_.size() ) );
    for ( const auto& [ id, _ ] : loaded_ ) {
        ids.append( id );
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

    // First run: if no plugins have been explicitly configured yet, enable
    // all discovered plugins by default so they are visible immediately. The
    // caller keeps them, so this only triggers once.
    if ( enabledIds.isEmpty() && !catalog_.discoveredPlugins().empty() ) {
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
    if ( loaded_.contains( pluginId ) ) {
        return QStringLiteral( "Plugin already loaded" );
    }

    const auto* meta = catalog_.findDiscovered( pluginId );
    if ( !meta ) {
        return QString( "Plugin '%1' not found in discovered plugins" ).arg( pluginId );
    }

    // Load the shared library
    auto loadResult = PluginLoader::load( *meta );
    if ( !loadResult.has_value() ) {
        const auto error = loadResult.error();
        LOG_ERROR << "Failed to load plugin '" << pluginId << "': " << error;
        Q_EMIT pluginError( pluginId, error );
        return error;
    }

    // Create context — PluginHandle has no default ctor so we construct in-place
    auto ctx = std::unique_ptr<PluginContext>(
        new PluginContext{ std::move( loadResult.value() ), {}, {}, {}, nullptr, this } );
    ctx->hostApi = buildHostApi();

    // Create plugin-private config directory
    ctx->configDir = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                     + "/plugin_config/" + pluginId;
    ctx->configDirUtf8 = ctx->configDir.toUtf8();
    QDir().mkpath( ctx->configDir );

    // Initialise — pass the context pointer as the opaque handle so that
    // host API trampolines can route back to this host
    const auto error = ctx->handle.init( &ctx->hostApi, ctx.get() );
    if ( !error.isEmpty() ) {
        LOG_ERROR << "Failed to init plugin '" << pluginId << "': " << error;
        // A plugin may have registered contributions before its init failed.
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
    auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() ) {
        return;
    }

    LOG_INFO << "Unloading plugin: " << pluginId;

    // Stop any active data source stream
    if ( it->second->stream ) {
        it->second->stream->signalEos();
        it->second->stream.reset();
        Q_EMIT dataSourceStopped( pluginId );
    }

    // Shut the plugin down while it is still loaded: it unregisters its
    // widgets through the host callbacks. Whatever it left behind is taken
    // away before its context and library go.
    it->second->handle.shutdown();
    if ( uiPort_ ) {
        uiPort_->removeContributions( pluginId );
    }

    loaded_.erase( it );
    Q_EMIT pluginUnloaded( pluginId );
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
    return loaded_.contains( pluginId );
}

void PluginHost::setUiPort( PluginUiPort* uiPort )
{
    uiPort_ = uiPort;
}

PluginHandle* PluginHost::pluginHandle( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it != loaded_.end() ) {
        return &it->second->handle;
    }
    return nullptr;
}

void PluginHost::configurePlugin( const QString& pluginId )
{
    auto* handle = pluginHandle( pluginId );
    if ( handle && handle->hasConfigureUi() ) {
        const auto parent = uiPort_ ? uiPort_->configurationParent() : PluginWidgetHandle{};
        handle->configure( parent.widget );
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
    for ( auto& [ id, ctx ] : loaded_ ) {
        if ( ctx->activeFileCallback ) {
            ctx->activeFileCallback( ctx->activeFileUserData, utf8.constData() );
        }
    }
}

// ── DataSource (Phase 2) ─────────────────────────────────────────────────────────

QString PluginHost::startDataSource( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it == loaded_.end() ) {
        return QString( "Plugin '%1' is not loaded" ).arg( pluginId );
    }

    auto& ctx = *it->second;
    if ( ctx.handle.metadata().type() != LOGSQUIRL_PLUGIN_DATASOURCE ) {
        return QString( "Plugin '%1' is not a DataSource plugin" ).arg( pluginId );
    }

    if ( ctx.stream ) {
        return QString( "DataSource '%1' is already running" ).arg( pluginId );
    }

    // Create the file-backed stream writer
    ctx.stream = std::make_unique<StreamWriter>( ctx.handle.metadata().name() );
    const auto path = ctx.stream->filePath();

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

    LOG_INFO << "Stopping DataSource: " << pluginId;
    it->second->stream->signalEos();
    it->second->stream.reset();
    Q_EMIT dataSourceStopped( pluginId );
}

StreamWriter* PluginHost::streamWriter( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it != loaded_.end() && it->second->stream ) {
        return it->second->stream.get();
    }
    return nullptr;
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

QString PluginHost::converterForExtension( const QString& extension ) const
{
    const auto ext = bareExtension( extension );
    if ( ext.isEmpty() ) {
        return {};
    }
    for ( const auto& [ id, ctx ] : loaded_ ) {
        if ( !ctx->handle.isConverter() ) {
            continue;
        }
        // Extensions are semicolon-separated, e.g. ".har;.pcap"
        const auto exts = ctx->handle.converterExtensions().split( ';', Qt::SkipEmptyParts );
        for ( const auto& e : exts ) {
            if ( bareExtension( e ) == ext ) {
                return id;
            }
        }
    }
    return {};
}

QStringList PluginHost::converterFileFilters() const
{
    QStringList filters;
    for ( const auto& [ id, ctx ] : loaded_ ) {
        if ( !ctx->handle.isConverter() ) {
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

int PluginHost::runConverter( const QString& pluginId, const QString& inputPath,
                              const QString& outputPath )
{
    auto* handle = pluginHandle( pluginId );
    if ( !handle || !handle->isConverter() ) {
        return -1;
    }
    return handle->convert( inputPath, outputPath );
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

void PluginHost::hostPushLine( void* handle, const char* data, size_t len )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->pushLine( data, len );
}

void PluginHost::hostPushLines( void* handle, const char* const* data, const size_t* lens,
                                size_t count )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->pushLines( data, lens, count );
}

void PluginHost::hostSignalEos( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->signalEos();
    LOG_INFO << "DataSource EOS from plugin";
    if ( ctx->host ) {
        const auto pluginId = ctx->handle.metadata().id();
        Q_EMIT ctx->host->dataSourceStopped( pluginId );
    }
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
    if ( ctx->host->openFileCallback_ ) {
        ctx->host->openFileCallback_( QString::fromUtf8( filePath ), follow != 0 );
    }
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
    if ( const auto [ port, pluginId ] = uiPortFor( handle ); port ) {
        port->addMenuAction( pluginId, QString::fromUtf8( menuPath ), QString::fromUtf8( label ),
                             callback, userData );
    }
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
    if ( !ctx || !ctx->host || !ctx->host->activeFilePathCallback_ ) {
        return "";
    }
    // Cache the UTF-8 bytes so the returned pointer remains valid
    ctx->host->activeFilePathUtf8_ = ctx->host->activeFilePathCallback_().toUtf8();
    return ctx->host->activeFilePathUtf8_.constData();
}

void PluginHost::hostRegisterActiveFileCallback(
    void* handle, void ( *callback )( void* user_data, const char* file_path ), void* user_data )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    ctx->activeFileCallback = callback;
    ctx->activeFileUserData = user_data;
}

} // namespace logsquirl::plugins
