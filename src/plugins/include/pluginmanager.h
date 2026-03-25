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

#pragma once

#include "logsquirl_plugin_api.h"
#include "pluginloader.h"
#include "pluginmetadata.h"
#include "streamwriter.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

#if LOGSQUIRL_HAS_LUA
#include "luapluginwrapper.h"
#endif

namespace logsquirl::plugins {

/// C-style callback function pointer used in the plugin host API.
using PluginCallbackFn = void ( * )( void* );

/**
 * Central manager for plugin discovery, loading, lifecycle, and host API.
 *
 * Scans platform-specific plugin directories for plugin.json manifests,
 * loads and initialises plugins, and provides the LogSquirlHostApi callbacks
 * that bridge plugin calls into the host application.
 *
 * Must be created on the main (GUI) thread.  Host API callbacks are safe to
 * call from any thread — they use queued connections internally to dispatch
 * to the main thread when needed.
 */
class PluginManager : public QObject {
    Q_OBJECT

  public:
    explicit PluginManager( QObject* parent = nullptr );
    ~PluginManager() override;

    PluginManager( const PluginManager& ) = delete;
    PluginManager& operator=( const PluginManager& ) = delete;

    /** Return the platform-specific plugin search directories. */
    static QStringList defaultPluginDirectories();

    /**
     * Scan all plugin directories for plugin.json manifests.
     * Populates the discovered-plugins list without loading any libraries.
     */
    void discoverPlugins();

    /**
     * Scan a single directory for plugin.json manifests.
     * Results are merged into the existing discovered list.
     */
    void discoverPluginsIn( const QString& directory );

    /** Return metadata for all discovered plugins. */
    const std::vector<PluginMetadata>& discoveredPlugins() const { return discovered_; }

    /** Return the set of currently loaded (initialised) plugin IDs. */
    QStringList loadedPluginIds() const;

    /**
     * Load and initialise a plugin by its ID.
     * @return Empty string on success, error message on failure.
     */
    QString loadPlugin( const QString& pluginId );

    /** Shut down and unload a plugin by its ID. */
    void unloadPlugin( const QString& pluginId );

    /** Shut down and unload all plugins. */
    void unloadAll();

    /**
     * Load all plugins listed in Configuration::enabledPlugins().
     * Skips IDs that are not discovered or already loaded.
     * @return List of error messages (empty if all loaded successfully).
     */
    QStringList autoLoadPlugins();

    /** Check whether a plugin is currently loaded and initialised. */
    bool isLoaded( const QString& pluginId ) const;

    /** Get the PluginHandle for a loaded plugin (nullptr if not loaded). */
    PluginHandle* pluginHandle( const QString& pluginId );

    /**
     * Open a plugin's configuration dialog.
     * @param pluginId      The plugin to configure.
     * @param parentWidget  Parent widget for the dialog.
     */
    void configurePlugin( const QString& pluginId, QWidget* parentWidget );

    /**
     * Set the callback used by open_file host API.
     * MainWindow connects this to its own loadFile slot.
     */
    void setOpenFileCallback( std::function<void( const QString&, bool )> callback );

    // ── DataSource (Phase 2) ─────────────────────────────────────────

    /**
     * Start a DataSource plugin.  Creates a temp-file-backed stream and
     * emits dataSourceStarted() so MainWindow can open it.
     * @return Empty string on success, error message on failure.
     */
    QString startDataSource( const QString& pluginId );

    /** Stop a running DataSource plugin by closing its stream. */
    void stopDataSource( const QString& pluginId );

    /** Return the StreamWriter for an active DataSource (nullptr if none). */
    StreamWriter* streamWriter( const QString& pluginId );

    // ── Converter registry (Phase 4) ────────────────────────────────

    /**
     * Return the plugin ID for the converter that handles the given extension.
     * Returns an empty string if no converter is registered for it.
     */
    QString converterForExtension( const QString& extension ) const;

    /** Return all file-dialog filters contributed by converter plugins. */
    QStringList converterFileFilters() const;

    /**
     * Run a converter plugin.
     * @return 0 on success, non-zero on failure.
     */
    int runConverter( const QString& pluginId,
                      const QString& inputPath,
                      const QString& outputPath );

  Q_SIGNALS:
    /** Emitted when a plugin is loaded and initialised successfully. */
    void pluginLoaded( const QString& pluginId );

    /** Emitted when a plugin encounters an error. */
    void pluginError( const QString& pluginId, const QString& errorMessage );

    /** Emitted when a plugin is unloaded. */
    void pluginUnloaded( const QString& pluginId );

    /** Emitted when a plugin registers a status bar widget. */
    void statusWidgetAdded( const QString& pluginId, QWidget* widget );

    /** Emitted when a plugin unregisters a status bar widget. */
    void statusWidgetRemoved( const QString& pluginId, QWidget* widget );

    /** Emitted when a plugin registers a menu action. */
    void menuActionAdded( const QString& pluginId,
                          const QString& menuPath,
                          const QString& label,
                          PluginCallbackFn callback,
                          void* userData );

    /** Emitted when a user-visible notification is requested. */
    void notificationRequested( const QString& message );

    /** Emitted when a DataSource plugin stream is ready to be opened. */
    void dataSourceStarted( const QString& pluginId,
                            const QString& displayName,
                            const QString& filePath );

    /** Emitted when a DataSource plugin stream has ended. */
    void dataSourceStopped( const QString& pluginId );

  private:
    /** Build a LogSquirlHostApi struct for a specific plugin instance. */
    LogSquirlHostApi buildHostApi();

    /** Find discovered metadata by plugin ID. */
    const PluginMetadata* findDiscovered( const QString& pluginId ) const;

    // Per-plugin context stored alongside the handle
    struct PluginContext {
        PluginHandle handle;
        LogSquirlHostApi hostApi;
        QString configDir;
        QByteArray configDirUtf8; ///< Cached UTF-8 so get_config_dir ptr stays valid
        // Non-null when this plugin is running as a DataSource
        std::unique_ptr<StreamWriter> stream;
        // Back-pointer to the owning PluginManager (for static trampolines)
        PluginManager* manager = nullptr;
#if LOGSQUIRL_HAS_LUA
        // Lua script wrapper — non-null for Lua-based plugins
        std::unique_ptr<LuaPluginWrapper> luaWrapper;
#endif
    };

    /** Extract a PluginContext from the opaque handle passed through host API. */
    static PluginContext* contextFromHandle( void* handle );

#if LOGSQUIRL_HAS_LUA
    /** Load a Lua-based plugin (library field ends with .lua). */
    QString loadLuaPlugin( const QString& pluginId, const PluginMetadata& meta );
#endif

    std::vector<PluginMetadata> discovered_;
    std::map<QString, std::unique_ptr<PluginContext>> loaded_;

    std::function<void( const QString&, bool )> openFileCallback_;

    // ── Static host API trampolines ──────────────────────────────────
    // These are the actual C function pointers stored in LogSquirlHostApi.
    // The void* handle is a PluginContext* which routes back to this manager.
    static void hostPushLine( void* handle, const char* data, size_t len );
    static void hostPushLines( void* handle, const char* const* data,
                               const size_t* lens, size_t count );
    static void hostSignalEos( void* handle );
    static void hostSignalError( void* handle, const char* message );
    static void hostLogMessage( void* handle, int level, const char* message );
    static const char* hostGetConfigDir( void* handle );
    static void hostShowNotification( void* handle, const char* message );
    static void hostOpenFile( void* handle, const char* filePath, int follow );
    static void hostRegisterStatusWidget( void* handle, void* qwidgetPtr );
    static void hostUnregisterStatusWidget( void* handle, void* qwidgetPtr );
    static void hostRegisterMenuAction( void* handle, const char* menuPath,
                                        const char* label,
                                        PluginCallbackFn callback,
                                        void* userData );
};

} // namespace logsquirl::plugins
