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

#include "pluginmanager.h"

#include "configuration.h"
#include "log.h"
#include "streamwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QWidget>

#if LOGSQUIRL_HAS_LUA
#include "luapluginwrapper.h"
#endif

namespace logsquirl::plugins {

// ── Construction / destruction ──────────────────────────────────────────────

PluginManager::PluginManager( QObject* parent )
    : QObject( parent )
{
}

PluginManager::~PluginManager()
{
    unloadAll();
}

// ── Platform-specific plugin directories ────────────────────────────────────

QStringList PluginManager::defaultPluginDirectories()
{
    QStringList dirs;
    const auto appDir = QCoreApplication::applicationDirPath();

#if defined( Q_OS_MACOS )
    // Inside .app bundle: Contents/PlugIns/
    dirs << QDir( appDir + "/../PlugIns" ).absolutePath();
    // User-installed plugins
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                + "/plugins";
#elif defined( Q_OS_WIN )
    dirs << appDir + "/plugins";
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                + "/plugins";
#else
    // Linux / other Unix
    dirs << appDir + "/plugins";
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                + "/plugins";
#endif

    return dirs;
}

// ── Discovery ───────────────────────────────────────────────────────────────

void PluginManager::discoverPlugins()
{
    discovered_.clear();
    for ( const auto& dir : defaultPluginDirectories() ) {
        discoverPluginsIn( dir );
    }
    LOG_INFO << "Plugin discovery complete: " << discovered_.size() << " plugin(s) found";
}

void PluginManager::discoverPluginsIn( const QString& directory )
{
    const QDir dir( directory );
    if ( !dir.exists() ) {
        LOG_DEBUG << "Plugin directory does not exist: " << directory;
        return;
    }

    LOG_INFO << "Scanning for plugins in: " << directory;

    // Look for plugin.json in immediate subdirectories
    QDirIterator it( directory, QDir::Dirs | QDir::NoDotAndDotDot );
    while ( it.hasNext() ) {
        const auto subDir = it.next();
        const auto manifestPath = QDir( subDir ).filePath( "plugin.json" );

        if ( !QFile::exists( manifestPath ) ) {
            continue;
        }

        auto result = PluginMetadata::fromJsonFile( manifestPath );
        if ( !result.has_value() ) {
            LOG_WARNING << "Skipping invalid plugin manifest: " << result.error();
            continue;
        }

        // Check for duplicate IDs — keep the first one found
        const auto& meta = result.value();
        bool duplicate = false;
        for ( const auto& existing : discovered_ ) {
            if ( existing.id() == meta.id() ) {
                LOG_WARNING << "Duplicate plugin ID '" << meta.id()
                            << "' — keeping first found at " << existing.directory();
                duplicate = true;
                break;
            }
        }

        if ( !duplicate ) {
            LOG_INFO << "Discovered plugin: " << meta.id() << " v" << meta.version()
                     << " at " << meta.directory();
            discovered_.push_back( std::move( result.value() ) );
        }
    }
}

// ── Loading / unloading ─────────────────────────────────────────────────────

QStringList PluginManager::loadedPluginIds() const
{
    QStringList ids;
    ids.reserve( static_cast<int>( loaded_.size() ) );
    for ( const auto& [id, _] : loaded_ ) {
        ids.append( id );
    }
    return ids;
}

QStringList PluginManager::autoLoadPlugins()
{
    const auto& config = Configuration::get();
    if ( !config.pluginsAutoLoad() ) {
        return {};
    }

    QStringList errors;
    const auto enabledIds = config.enabledPlugins();

    for ( const auto& pluginId : enabledIds ) {
        if ( isLoaded( pluginId ) ) {
            continue;
        }
        if ( !findDiscovered( pluginId ) ) {
            LOG_WARNING << "Auto-load: plugin '" << pluginId << "' not found, skipping";
            continue;
        }

        const auto error = loadPlugin( pluginId );
        if ( !error.isEmpty() ) {
            LOG_WARNING << "Auto-load failed for '" << pluginId << "': " << error;
            errors.append( QString( "%1: %2" ).arg( pluginId, error ) );
        }
    }

    return errors;
}

QString PluginManager::loadPlugin( const QString& pluginId )
{
    if ( loaded_.contains( pluginId ) ) {
        return QStringLiteral( "Plugin already loaded" );
    }

    const auto* meta = findDiscovered( pluginId );
    if ( !meta ) {
        return QString( "Plugin '%1' not found in discovered plugins" ).arg( pluginId );
    }

#if LOGSQUIRL_HAS_LUA
    // Lua-based plugins: library field ends with .lua
    if ( meta->library().endsWith( ".lua", Qt::CaseInsensitive ) ) {
        return loadLuaPlugin( pluginId, *meta );
    }
#endif

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
    // host API trampolines can route back to this manager
    const auto error = ctx->handle.init( &ctx->hostApi, ctx.get() );
    if ( !error.isEmpty() ) {
        LOG_ERROR << "Failed to init plugin '" << pluginId << "': " << error;
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

void PluginManager::unloadPlugin( const QString& pluginId )
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

#if LOGSQUIRL_HAS_LUA
    // Shut down Lua wrapper if this is a script-based plugin
    if ( it->second->luaWrapper ) {
        it->second->luaWrapper->shutdown();
        it->second->luaWrapper.reset();
    }
#endif

    // PluginHandle destructor calls shutdown
    loaded_.erase( it );
    Q_EMIT pluginUnloaded( pluginId );
}

void PluginManager::unloadAll()
{
    // Collect IDs first to avoid iterator invalidation during signal emission
    const auto ids = loadedPluginIds();
    for ( const auto& id : ids ) {
        unloadPlugin( id );
    }
}

bool PluginManager::isLoaded( const QString& pluginId ) const
{
    return loaded_.contains( pluginId );
}

PluginHandle* PluginManager::pluginHandle( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it != loaded_.end() ) {
        return &it->second->handle;
    }
    return nullptr;
}

void PluginManager::configurePlugin( const QString& pluginId, QWidget* parentWidget )
{
    auto* handle = pluginHandle( pluginId );
    if ( handle && handle->hasConfigureUi() ) {
        handle->configure( static_cast<void*>( parentWidget ) );
    }
}

void PluginManager::setOpenFileCallback(
    std::function<void( const QString&, bool )> callback )
{
    openFileCallback_ = std::move( callback );
}

#if LOGSQUIRL_HAS_LUA
QString PluginManager::loadLuaPlugin( const QString& pluginId, const PluginMetadata& meta )
{
    const auto scriptPath = meta.libraryPath(); // e.g. /path/to/plugin/script.lua

    auto wrapper = LuaPluginWrapper::load( scriptPath );
    if ( !wrapper ) {
        const auto error = QString( "Failed to load Lua script '%1'" ).arg( scriptPath );
        LOG_ERROR << error;
        Q_EMIT pluginError( pluginId, error );
        return error;
    }

    // Create context with a script-only PluginHandle (no library loaded).
    // Re-parse the metadata to get an owned copy for the handle.
    auto metaResult = PluginMetadata::fromJsonFile(
        QDir( meta.directory() ).filePath( "plugin.json" ) );
    if ( !metaResult.has_value() ) {
        return metaResult.error();
    }

    auto ctx = std::unique_ptr<PluginContext>(
        new PluginContext{ PluginHandle::createScriptHandle( std::move( metaResult.value() ) ),
                           {}, {}, {}, nullptr, this } );
    ctx->hostApi = buildHostApi();

    ctx->configDir = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
                     + "/plugin_config/" + pluginId;
    ctx->configDirUtf8 = ctx->configDir.toUtf8();
    QDir().mkpath( ctx->configDir );

    // Initialise the Lua plugin via its wrapper
    const auto rc = wrapper->init( &ctx->hostApi, ctx.get() );
    if ( rc != 0 ) {
        const auto error = wrapper->lastError();
        LOG_ERROR << "Lua plugin init failed: " << error;
        Q_EMIT pluginError( pluginId, error );
        return error;
    }

    ctx->luaWrapper = std::move( wrapper );
    loaded_[ pluginId ] = std::move( ctx );

    LOG_INFO << "Loaded Lua plugin: " << pluginId;
    Q_EMIT pluginLoaded( pluginId );
    return {};
}
#endif

// ── DataSource (Phase 2) ─────────────────────────────────────────────────────────

QString PluginManager::startDataSource( const QString& pluginId )
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

void PluginManager::stopDataSource( const QString& pluginId )
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

StreamWriter* PluginManager::streamWriter( const QString& pluginId )
{
    auto it = loaded_.find( pluginId );
    if ( it != loaded_.end() && it->second->stream ) {
        return it->second->stream.get();
    }
    return nullptr;
}

// ── Converter registry (Phase 4) ───────────────────────────────────────────────

QString PluginManager::converterForExtension( const QString& extension ) const
{
    const auto ext = extension.toLower();
    for ( const auto& [id, ctx] : loaded_ ) {
        if ( !ctx->handle.isConverter() ) {
            continue;
        }
        // Extensions are semicolon-separated, e.g. ".har;.pcap"
        const auto exts = ctx->handle.converterExtensions().split(
            ';', Qt::SkipEmptyParts );
        for ( const auto& e : exts ) {
            if ( e.trimmed().toLower() == ext ) {
                return id;
            }
        }
    }
    return {};
}

QStringList PluginManager::converterFileFilters() const
{
    QStringList filters;
    for ( const auto& [id, ctx] : loaded_ ) {
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
            filters << QString( "%1 (%2)" )
                           .arg( ctx->handle.metadata().name(), wildcards.join( ' ' ) );
        }
    }
    return filters;
}

int PluginManager::runConverter( const QString& pluginId,
                                 const QString& inputPath,
                                 const QString& outputPath )
{
    auto* handle = pluginHandle( pluginId );
    if ( !handle || !handle->isConverter() ) {
        return -1;
    }
    return handle->convert( inputPath, outputPath );
}

// ── Host API construction ───────────────────────────────────────────────────

LogSquirlHostApi PluginManager::buildHostApi()
{
    LogSquirlHostApi api{};
    api.api_version = LOGSQUIRL_PLUGIN_API_VERSION;

    api.push_line = &PluginManager::hostPushLine;
    api.push_lines = &PluginManager::hostPushLines;
    api.signal_eos = &PluginManager::hostSignalEos;
    api.signal_error = &PluginManager::hostSignalError;
    api.log_message = &PluginManager::hostLogMessage;
    api.get_config_dir = &PluginManager::hostGetConfigDir;
    api.show_notification = &PluginManager::hostShowNotification;
    api.open_file = &PluginManager::hostOpenFile;
    api.register_status_widget = &PluginManager::hostRegisterStatusWidget;
    api.unregister_status_widget = &PluginManager::hostUnregisterStatusWidget;
    api.register_menu_action = &PluginManager::hostRegisterMenuAction;

    return api;
}

const PluginMetadata* PluginManager::findDiscovered( const QString& pluginId ) const
{
    for ( const auto& meta : discovered_ ) {
        if ( meta.id() == pluginId ) {
            return &meta;
        }
    }
    return nullptr;
}

// ── Host API trampolines ────────────────────────────────────────────────────
//
// Each trampoline receives a void* handle that is actually a PluginContext*.
// From there we can access the PluginManager and route the call.

// Extract the PluginContext from the opaque handle.
auto PluginManager::contextFromHandle( void* handle ) -> PluginContext*
{
    return static_cast<PluginContext*>( handle );
}

void PluginManager::hostPushLine( void* handle, const char* data, size_t len )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->pushLine( data, len );
}

void PluginManager::hostPushLines( void* handle, const char* const* data,
                                   const size_t* lens, size_t count )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->pushLines( data, lens, count );
}

void PluginManager::hostSignalEos( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->stream ) {
        return;
    }
    ctx->stream->signalEos();
    LOG_INFO << "DataSource EOS from plugin";
    if ( ctx->manager ) {
        const auto pluginId = ctx->handle.metadata().id();
        Q_EMIT ctx->manager->dataSourceStopped( pluginId );
    }
}

void PluginManager::hostSignalError( void* handle, const char* message )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return;
    }
    LOG_ERROR << "Plugin error: " << message;
}

void PluginManager::hostLogMessage( void* handle, int level, const char* message )
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

const char* PluginManager::hostGetConfigDir( void* handle )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx ) {
        return "";
    }
    return ctx->configDirUtf8.constData();
}

void PluginManager::hostShowNotification( void* handle, const char* message )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->manager ) {
        return;
    }
    const auto msg = QString::fromUtf8( message );
    LOG_INFO << "[plugin notification] " << msg;
    Q_EMIT ctx->manager->notificationRequested( msg );
}

void PluginManager::hostOpenFile( void* handle, const char* filePath, int follow )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->manager ) {
        return;
    }
    if ( ctx->manager->openFileCallback_ ) {
        ctx->manager->openFileCallback_(
            QString::fromUtf8( filePath ), follow != 0 );
    }
}

void PluginManager::hostRegisterStatusWidget( void* handle, void* qwidgetPtr )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->manager ) {
        return;
    }
    auto* widget = static_cast<QWidget*>( qwidgetPtr );
    const auto pluginId = ctx->handle.metadata().id();
    Q_EMIT ctx->manager->statusWidgetAdded( pluginId, widget );
}

void PluginManager::hostUnregisterStatusWidget( void* handle, void* qwidgetPtr )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->manager ) {
        return;
    }
    auto* widget = static_cast<QWidget*>( qwidgetPtr );
    const auto pluginId = ctx->handle.metadata().id();
    Q_EMIT ctx->manager->statusWidgetRemoved( pluginId, widget );
}

void PluginManager::hostRegisterMenuAction( void* handle, const char* menuPath,
                                            const char* label,
                                            PluginCallbackFn callback,
                                            void* userData )
{
    auto* ctx = contextFromHandle( handle );
    if ( !ctx || !ctx->manager ) {
        return;
    }
    const auto pluginId = ctx->handle.metadata().id();
    Q_EMIT ctx->manager->menuActionAdded(
        pluginId,
        QString::fromUtf8( menuPath ),
        QString::fromUtf8( label ),
        callback,
        userData );
}

} // namespace logsquirl::plugins
