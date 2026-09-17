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
#include "plugincatalog.h"
#include "pluginloader.h"
#include "pluginuiport.h"
#include "streamwriter.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace logsquirl::plugins {

/**
 * Which plugins to load at start-up, as the caller's configuration says.
 * The Plugin Host is handed it and reads no settings of its own.
 */
struct PluginAutoLoad {
    /** Whether enabled plugins are loaded at start-up at all. */
    bool autoLoad = true;
    /** The IDs of the enabled plugins; none yet on a first run. */
    QStringList enabled;
};

/** What loading the enabled plugins at start-up came to. */
struct PluginAutoLoadResult {
    /** One message per plugin that failed to load (empty if all loaded). */
    QStringList errors;
    /**
     * Set on a first run, when no plugin was enabled yet and the host enabled
     * every plugin in the catalog: those IDs, for the caller to keep as the
     * enabled plugins so a first run happens only once.
     */
    std::optional<QStringList> enabledOnFirstRun;
};

/**
 * The Plugin Host: loads the plugins the Plugin Catalog lists and serves them.
 *
 * Loads and initialises plugin libraries, shuts them down again, and provides
 * the LogSquirlHostApi callbacks that bridge plugin calls into the host
 * application: data-source streams, converters, the active file, opening
 * files and notifications.
 *
 * What plugins contribute to the user interface goes to the PluginUiPort set
 * with setUiPort(); the host itself knows no widgets.
 *
 * Must be created on the main (GUI) thread.  Host API callbacks may be called
 * from any thread: the PluginUiPort is called on the plugin's thread and moves
 * widget work to the GUI thread itself, signals use queued connections.
 */
class PluginHost : public QObject {
    Q_OBJECT

public:
    /**
     * Create a host that loads plugins from the given catalog, looking them up
     * by id. The catalog must outlive the host.
     */
    explicit PluginHost( const PluginCatalog& catalog, QObject* parent = nullptr );

    /** The host keeps a reference to the catalog, so a temporary one is refused. */
    explicit PluginHost( const PluginCatalog&& catalog, QObject* parent = nullptr ) = delete;

    ~PluginHost() override;

    PluginHost( const PluginHost& ) = delete;
    PluginHost& operator=( const PluginHost& ) = delete;

    /** Return the set of currently loaded (initialised) plugin IDs. */
    QStringList loadedPluginIds() const;

    /**
     * Set the Plugin UI Port the host callbacks show plugin contributions
     * through. Set it before loading plugins; it must outlive the host's
     * loaded plugins. Without a port, UI contributions are ignored.
     */
    void setUiPort( PluginUiPort* uiPort );

    /** The Plugin UI Port set with setUiPort(), or nullptr. */
    PluginUiPort* uiPort() const
    {
        return uiPort_;
    }

    /**
     * Load and initialise a plugin the catalog lists, by its ID.
     * @return Empty string on success, error message on failure.
     */
    QString loadPlugin( const QString& pluginId );

    /**
     * Shut down and unload a plugin by its ID. What the plugin still
     * contributes to the user interface is removed through the port.
     */
    void unloadPlugin( const QString& pluginId );

    /** Shut down and unload all plugins. */
    void unloadAll();

    /**
     * Load the enabled plugins, unless auto-load is off.
     * Skips IDs that are not in the catalog or already loaded. When no plugin
     * is enabled yet, enables every plugin in the catalog first and hands
     * those IDs back for the caller to keep.
     */
    PluginAutoLoadResult autoLoadPlugins( const PluginAutoLoad& configuration );

    /** Check whether a plugin is currently loaded and initialised. */
    bool isLoaded( const QString& pluginId ) const;

    /** Get the PluginHandle for a loaded plugin (nullptr if not loaded). */
    PluginHandle* pluginHandle( const QString& pluginId );

    /**
     * Open a plugin's configuration dialog on the parent the Plugin UI Port
     * chooses.
     * @param pluginId  The plugin to configure.
     */
    void configurePlugin( const QString& pluginId );

    /**
     * Set the callback used by open_file host API.
     * MainWindow connects this to its own loadFile slot.
     */
    void setOpenFileCallback( std::function<void( const QString&, bool )> callback );

    /**
     * Set the callback used by get_active_file_path host API.
     * MainWindow connects this to return the current tab's file path.
     */
    void setActiveFilePathCallback( std::function<QString()> callback );

    /**
     * Notify all plugins that registered an active-file callback
     * that the focused log file has changed.
     */
    void notifyActiveFileChanged( const QString& filePath );

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
    int runConverter( const QString& pluginId, const QString& inputPath,
                      const QString& outputPath );

Q_SIGNALS:
    /** Emitted when a plugin is loaded and initialised successfully. */
    void pluginLoaded( const QString& pluginId );

    /** Emitted when a plugin encounters an error. */
    void pluginError( const QString& pluginId, const QString& errorMessage );

    /** Emitted when a plugin is unloaded. */
    void pluginUnloaded( const QString& pluginId );

    /** Emitted when a user-visible notification is requested. */
    void notificationRequested( const QString& message );

    /** Emitted when a DataSource plugin stream is ready to be opened. */
    void dataSourceStarted( const QString& pluginId, const QString& displayName,
                            const QString& filePath );

    /** Emitted when a DataSource plugin stream has ended. */
    void dataSourceStopped( const QString& pluginId );

private:
    /** Build a LogSquirlHostApi struct for a specific plugin instance. */
    LogSquirlHostApi buildHostApi();

    // Per-plugin context stored alongside the handle
    struct PluginContext {
        PluginHandle handle;
        LogSquirlHostApi hostApi;
        QString configDir;
        QByteArray configDirUtf8; ///< Cached UTF-8 so get_config_dir ptr stays valid
        // Non-null when this plugin is running as a DataSource
        std::unique_ptr<StreamWriter> stream;
        // Back-pointer to the owning PluginHost (for static trampolines)
        PluginHost* host = nullptr;
        // Active-file-change callback registered by plugin (optional)
        void ( *activeFileCallback )( void* user_data, const char* file_path ) = nullptr;
        void* activeFileUserData = nullptr;
    };

    /** Extract a PluginContext from the opaque handle passed through host API. */
    static PluginContext* contextFromHandle( void* handle );

    /**
     * The Plugin UI Port and plugin ID for a host callback, or a null port
     * when the handle is unusable or no port is set.
     */
    static std::pair<PluginUiPort*, QString> uiPortFor( void* handle );

    const PluginCatalog& catalog_;
    std::map<QString, std::unique_ptr<PluginContext>> loaded_;

    PluginUiPort* uiPort_ = nullptr;

    std::function<void( const QString&, bool )> openFileCallback_;
    std::function<QString()> activeFilePathCallback_;
    QByteArray activeFilePathUtf8_; ///< Cached for get_active_file_path pointer stability

    // ── Static host API trampolines ──────────────────────────────────
    // These are the actual C function pointers stored in LogSquirlHostApi.
    // The void* handle is a PluginContext* which routes back to this host.
    static void hostPushLine( void* handle, const char* data, size_t len );
    static void hostPushLines( void* handle, const char* const* data, const size_t* lens,
                               size_t count );
    static void hostSignalEos( void* handle );
    static void hostSignalError( void* handle, const char* message );
    static void hostLogMessage( void* handle, int level, const char* message );
    static const char* hostGetConfigDir( void* handle );
    static void hostShowNotification( void* handle, const char* message );
    static void hostOpenFile( void* handle, const char* filePath, int follow );
    static void hostRegisterStatusWidget( void* handle, void* qwidgetPtr );
    static void hostUnregisterStatusWidget( void* handle, void* qwidgetPtr );
    static void hostRegisterMenuAction( void* handle, const char* menuPath, const char* label,
                                        PluginCallbackFn callback, void* userData );
    static void hostRegisterSidebarTab( void* handle, const char* label, void* qwidgetPtr );
    static void hostUnregisterSidebarTab( void* handle, void* qwidgetPtr );
    static void hostRegisterFooterWidget( void* handle, void* qwidgetPtr );
    static void hostUnregisterFooterWidget( void* handle, void* qwidgetPtr );
    static const char* hostGetActiveFilePath( void* handle );
    static void hostRegisterActiveFileCallback( void* handle,
                                                void ( *callback )( void* user_data,
                                                                    const char* file_path ),
                                                void* user_data );
};

} // namespace logsquirl::plugins
