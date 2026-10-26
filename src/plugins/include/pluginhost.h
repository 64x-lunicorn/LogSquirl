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
#include "pluginlogfiles.h"
#include "pluginuiport.h"
#include "streamwriter.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

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
    /**
     * Whether the enabled plugins were chosen, by the user or by a first run
     * the caller kept. Until then an empty list means a first run, which
     * enables every plugin in the catalog; afterwards it means none (#820).
     */
    bool chosen = false;
};

/** What loading the enabled plugins at start-up came to. */
struct PluginAutoLoadResult {
    /** One message per plugin that failed to load (empty if all loaded). */
    QStringList errors;
    /**
     * Set on a first run, when no plugin was chosen yet and the host enabled
     * every plugin in the catalog: those IDs, possibly none, for the caller to
     * keep as the chosen enabled plugins so a first run happens only once.
     */
    std::optional<QStringList> enabledOnFirstRun;
};

/** Names a Conversion the Plugin Host started; 0 names none. */
using ConversionId = std::uint64_t;

/** Names a content check the Plugin Host started (#861); 0 names none. */
using ContentCheckId = std::uint64_t;

/** How a Conversion ended. */
enum class ConversionOutcome {
    Succeeded, ///< The converter returned 0: the output is the Log File.
    Failed,    ///< The converter returned non-zero.
    Cancelled  ///< Cancelled, or its plugin was unloaded; what it wrote is discarded.
};

struct ConversionEnd {
    ConversionOutcome outcome = ConversionOutcome::Failed;
    /** What the converter returned: 0 when it succeeded; 0 when it never ran. */
    int returnCode = 0;
};

/**
 * What the caller of PluginHost::startConversion() hears of its Conversion,
 * each on the host's thread: started once its call into the plugin begins
 * (within startConversion() when nothing of the plugin's is running, else
 * later, from the event loop); progress as the converter reports it, done of
 * total units, a total of 0 meaning unknown (a plain convert reports none);
 * finished once, last. Each may be empty.
 */
struct ConversionReport {
    std::function<void()> started;
    std::function<void( std::uint64_t done, std::uint64_t total )> progress;
    std::function<void( const ConversionEnd& end )> finished;
};

/**
 * The Plugin Host: loads the plugins the Plugin Catalog lists and serves them.
 *
 * Loads and initialises plugin libraries, shuts them down again, and provides
 * the LogSquirlHostApi callbacks that bridge plugin calls into the host
 * application: data-source streams, converters, the active file, opening
 * files, notifications, the Regex Lab and the Log Lines of the tab in front.
 *
 * What plugins contribute to the user interface goes to the PluginUiPort set
 * with setUiPort(); the host itself knows no widgets.
 *
 * Must be created on the main (GUI) thread.  Host API callbacks may be called
 * from any thread: the PluginUiPort is called on the plugin's thread and moves
 * widget work to the GUI thread itself, signals use queued connections, and
 * open_file, add_marks and remove_marks are queued to the GUI thread (#866).
 * The functions documented as UI-thread-only (the Regex Lab, the Log Lines of
 * the tab in front, the active file path, the Marks read) refuse a call from
 * another thread.  A Conversion runs the
 * converter on a worker thread of the host's and reports on the GUI thread
 * (#795), and asks the converters whether they claim a file by its content
 * on a worker thread too (#861).  The Log Lines of an open Log File, and
 * their fields (#867), are read on the plugin's thread, through the Log Files
 * the UI adds to logFiles() (#858).
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
     *
     * Asked for while the host's call into that plugin is still running -- a
     * menu action of the plugin's that shows a modal dialog, say -- the
     * plugin counts as unloaded at once, and nothing calls into it any more;
     * it is shut down once that call has returned (#691). Its Conversions
     * are cancelled first, the one under way waited for (#795), and so is a
     * content check's call into it under way (#861). A stream of
     * the plugin's that runs is stopped in the plugin before the plugin is
     * shut down, and ends after (#855).
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
     * MainWindow connects this to its own loadFile slot. It is called on the
     * host's thread only: at once when the plugin calls from there, else from
     * the event loop, unless the plugin was unloaded meanwhile.
     */
    void setOpenFileCallback( std::function<void( const QString&, bool )> callback );

    /**
     * Set the callback used by get_active_file_path host API.
     * MainWindow connects this to return the current tab's file path. It is
     * called on the host's thread only.
     */
    void setActiveFilePathCallback( std::function<QString()> callback );

    /**
     * Notify all plugins that registered an active-file callback
     * that the focused log file has changed.
     */
    void notifyActiveFileChanged( const QString& filePath );

    /**
     * Notify all plugins that registered an encoding callback that a Log
     * File is read in another Encoding now (#803). On the host's thread only.
     */
    void notifyEncodingChanged( const QString& filePath, const QString& encoding );

    /**
     * The Log Files open in every window, by path, which plugins read on any
     * thread (#858). The UI adds each when its tab opens and removes it when
     * the tab closes. Shared, so that a tab outliving the host lets go of an
     * id the host no longer knows.
     */
    const std::shared_ptr<PluginLogFiles>& logFiles() const
    {
        return logFiles_;
    }

    /**
     * Notify all plugins that registered a log lines callback that the Log
     * Lines of an open Log File changed: lineCount is how many it has now
     * (#858). On the host's thread only.
     */
    void notifyLogLinesChanged( const QString& filePath, std::uint64_t lineCount,
                                PluginLogLinesChange change );

    // ── DataSource (Phase 2) ─────────────────────────────────────────

    /**
     * Start a DataSource plugin.  Creates a temp-file-backed stream, calls
     * the plugin's logsquirl_datasource_start, if it exports it, as a call
     * into the plugin, and emits dataSourceStarted() so MainWindow can open
     * the stream once the plugin let it start (#855). A start the plugin
     * refuses frees the stream again, and nothing is emitted. A start while
     * the plugin's stream runs, or is starting, is refused without calling
     * the plugin.
     * @return Empty string when the stream started or when the plugin said
     *         the user cancelled; an error message when it did not start for
     *         another reason.
     */
    QString startDataSource( const QString& pluginId );

    /**
     * Stop a running DataSource plugin: calls its logsquirl_datasource_stop,
     * if it exports it, then closes its stream. Nothing while the plugin's
     * start or stop is still running.
     */
    void stopDataSource( const QString& pluginId );

    /** Return the StreamWriter for an active DataSource (nullptr if none). */
    StreamWriter* streamWriter( const QString& pluginId );

    // ── Converter registry (Phase 4) ────────────────────────────────

    /**
     * Return the plugin ID for the converter that handles the given extension:
     * of several, the one with the smallest ID (#861). Returns an empty string
     * if no converter is registered for it.
     */
    QString converterForExtension( const QString& extension ) const;

    /** Return all file-dialog filters contributed by converter plugins. */
    QStringList converterFileFilters() const;

    /**
     * Starts a content check (#861): on a worker thread of the host's, never
     * this one, reads the first LOGSQUIRL_CONVERTER_CLAIMS_HEAD_LENGTH bytes of
     * filePath and asks the loaded converters that export
     * logsquirl_converter_claims, one after the other in plugin-ID order,
     * whether they convert it. The caller hears, through claimed, on the
     * host's thread, the ID of the first that claims it; an empty one when
     * none does, the head could not be read, or the plugin that claimed it was
     * unloaded meanwhile. A plugin unloaded during the check is not asked any
     * more, and its unload waits for its call under way.
     * @return The check's ID, or 0 when no loaded converter exports the
     *         check: then nothing is reported.
     */
    ContentCheckId checkContent( const QString& filePath,
                                 std::function<void( const QString& pluginId )> claimed );

    /**
     * Cancels a content check: its caller hears nothing of it, and no plugin
     * is asked any more. A call under way is not waited for. Nothing for a
     * check that has finished.
     */
    void cancelContentCheck( ContentCheckId id );

    /**
     * Starts a Conversion: the converter plugin writes inputPath as plain
     * text to outputPath on a worker thread of the host's, never on this
     * one (#795). A plugin converts one file at a time: a Conversion started
     * while one of the plugin's runs waits its turn; those of different
     * plugins run at the same time. The caller hears of it through report,
     * on the host's thread. A converter exporting convert_ex is called with
     * it and hears a cancellation through it; a plain convert runs to its
     * end and its result is discarded when cancelled. The caller removes
     * outputPath when the Conversion did not succeed.
     * @return The Conversion's ID, or 0 when the plugin is not loaded or is
     *         no converter: then nothing is reported.
     */
    ConversionId startConversion( const QString& pluginId, const QString& inputPath,
                                  const QString& outputPath, ConversionReport report );

    /**
     * Cancels a Conversion: one waiting its turn finishes cancelled at once;
     * one under way once its call has returned, which this does not wait
     * for. Nothing for a Conversion that has finished.
     */
    void cancelConversion( ConversionId id );

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

    struct PluginContext;

    /**
     * What the worker thread of a Conversion shares with the host: the control
     * handed to convert_ex, the cancellation flag and the latest progress.
     */
    struct ConversionControl;

    /** A Conversion asked for: queued until its turn, then under way. */
    struct Conversion {
        ConversionId id = 0;
        QString inputPath;
        QString outputPath;
        ConversionReport report;
        // Set while it is under way.
        std::shared_ptr<ConversionControl> control;
        std::thread thread;
    };

    /**
     * What the worker thread of a content check shares with the host: the
     * plugins to ask, the one being asked and the one that claimed the file.
     */
    struct ContentCheckControl;

    /** A content check under way (#861). */
    struct ContentCheck {
        ContentCheckId id = 0;
        std::function<void( const QString& pluginId )> claimed;
        std::shared_ptr<ContentCheckControl> control;
        std::thread thread;
    };

    /**
     * A menu action's callback as the plugin registered it. The host hands
     * the Plugin UI Port its own callback with this as the user data, so that
     * the plugin's runs as a call into the plugin (#691). Created in place
     * and never moved: its address names the action.
     */
    struct MenuActionCall {
        PluginContext* context = nullptr;
        PluginCallbackFn callback = nullptr;
        void* userData = nullptr;
        QString label{};
        // The id register_menu_action_ex returned; 0 for an action of
        // register_menu_action, which no id removes (#857).
        int id = 0;
        // Set by unregister_menu_action, from any thread: a trigger of the
        // action that comes later, queued or not, does not reach the plugin.
        std::atomic<bool> removed{ false };
    };

    /**
     * A context menu item's callback as the plugin registered it (#859),
     * handed to the Plugin UI Port as a menu action's is: created in place
     * and never moved, its address names the item.
     */
    struct ContextMenuActionCall {
        PluginContext* context = nullptr;
        LogSquirlContextMenuCallbackFn callback = nullptr;
        void* userData = nullptr;
        // The id register_context_menu_action returned.
        int id = 0;
        // Set by unregister_context_menu_action, from any thread: a choice of
        // the item delivered later does not reach the plugin.
        std::atomic<bool> removed{ false };
    };

    /**
     * What the host returns to a plugin that may call on any thread (#858):
     * each thread of the plugin's has a buffer of its own, valid until the
     * plugin calls the same function on that thread again or is unloaded. A
     * buffer stays until the plugin is unloaded; a thread that reuses the id
     * of one that ended reuses its buffer.
     */
    template <typename Buffer>
    class PerThread {
    public:
        /** The calling thread's buffer, as it was made on its first call. */
        Buffer& ofThisThread()
        {
            const std::lock_guard lock( mutex_ );
            return buffers_[ std::this_thread::get_id() ];
        }

    private:
        // Held to find or add a buffer, not while one is written: a buffer
        // is only ever touched by its own thread, and a map's elements stay
        // where they are as others are added.
        std::mutex mutex_;
        std::map<std::thread::id, Buffer> buffers_;
    };
    using PerThreadText = PerThread<QByteArray>;

    /** What get_log_format_fields hands a plugin's thread (#867). */
    struct LogFormatFieldsBuffer {
        QByteArray name;
        // Each NUL-terminated, where namePointers point.
        std::vector<QByteArray> fieldNames;
        std::vector<const char*> namePointers;
    };

    /** What get_log_line_fields hands a plugin's thread (#867). */
    struct LogLineFieldsBuffer {
        // Every value, each followed by a NUL, where valuePointers point.
        QByteArray text;
        std::vector<const char*> valuePointers;
        std::vector<size_t> valueLengths;
        std::vector<int> parsed;
    };

    /** Where a plugin's data source is between its start and its stop (#855). */
    enum class DataSourceState {
        Idle,     ///< No stream.
        Starting, ///< The stream exists; logsquirl_datasource_start is running.
        Running,  ///< The stream runs; logsquirl_datasource_stop is owed.
        Stopping  ///< logsquirl_datasource_stop is running or has returned; the stream ends next.
    };

    // Per-plugin context stored alongside the handle
    struct PluginContext {
        PluginHandle handle;
        LogSquirlHostApi hostApi{};
        QString configDir{};
        QByteArray configDirUtf8{}; ///< Cached UTF-8 so get_config_dir ptr stays valid
        // Non-null while this plugin runs as a data source. The push
        // callbacks read and use it on the plugin's threads, the host ends it
        // on its own: each under streamMutex, so a push under way has
        // returned before the stream goes, and a later one finds none (#821).
        std::mutex streamMutex{};
        std::unique_ptr<StreamWriter> stream{};
        // Where the data source is between the plugin's start and stop, so
        // that stop is called once for every start that let the stream run,
        // before the stream ends (#855). UI thread only.
        DataSourceState dataSourceState = DataSourceState::Idle;
        // What start and stop are handed, valid during their call.
        LogSquirlDataSourceContext dataSourceContext{ sizeof( LogSquirlDataSourceContext ) };
        // Back-pointer to the owning PluginHost (for static trampolines)
        PluginHost* host = nullptr;
        // Which load of the plugin this is, from any thread: a call queued to
        // the host's thread is dropped when the plugin was unloaded meanwhile,
        // also when it was loaded again since.
        std::uint64_t loadSerial = 0;
        // Active-file-change callback registered by plugin (optional), under
        // registrationsMutex: it may register from any thread (#869).
        void ( *activeFileCallback )( void* user_data, const char* file_path ) = nullptr;
        void* activeFileUserData = nullptr;
        // Encoding-change callback registered by plugin (optional, #803),
        // under registrationsMutex as the one above.
        LogSquirlEncodingCallbackFn encodingCallback = nullptr;
        void* encodingUserData = nullptr;
        // What the user interface answers the plugin through is connected
        // with this context, on the host's thread: once it is gone, nothing
        // calls into the plugin any more. It goes before the plugin is shut
        // down (#662).
        std::unique_ptr<QObject> answerContext{};
        // Cached UTF-8 so the get_selected_log_lines text stays valid until
        // the plugin's next call of it (#663).
        QByteArray selectedLogLinesUtf8{};
        // Cached UTF-8 so the get_log_file_encoding name stays valid until
        // the plugin's next call of it (#803).
        QByteArray logFileEncodingUtf8{};
        // Cached UTF-8 so the get_active_file_path path stays valid until
        // the plugin's next call of it, whatever other plugins ask (#857).
        // UI thread only.
        QByteArray activeFilePathUtf8{};
        // The host's calls into the plugin, nested ones on the UI thread and
        // a Conversion under way on its worker thread included: one of them
        // may run an event loop -- a modal dialog -- in which the user unloads
        // the plugin. Its unload then waits until the outermost one has
        // returned (#691); a Conversion under way is cancelled and waited for
        // first (#795). UI thread only.
        int callsRunning = 0;
        bool unloadPending = false;
        // The Regex Lab answers that came while the unload was pending, in
        // the order they came: delivered from the event loop if the plugin
        // is loaded again meanwhile, dropped once it is shut down (#801).
        // While any are parked, a later answer is parked behind them. UI
        // thread only.
        std::deque<std::function<void()>> parkedAnswers{};
        // The plugin's Conversions: at most one under way, the others in the
        // order asked. UI thread only.
        std::deque<std::unique_ptr<Conversion>> queuedConversions{};
        std::unique_ptr<Conversion> runningConversion{};
        // What the plugin registered from any thread, under
        // registrationsMutex. The menu actions' addresses are the user data
        // of the host's callbacks, so they stay until the context goes, also
        // once removed: a trigger still queued then finds them marked.
        std::mutex registrationsMutex{};
        std::list<MenuActionCall> menuActionCalls{};
        // The ids handed out so far, by every registration that returns one
        // (#857): the next one is one more, so none is handed out twice.
        int registrationIdsSoFar = 0;
        // The menu actions of register_menu_action_ex not removed yet, by id.
        std::map<int, MenuActionCall*> menuActionsById{};
        // The context menu items, kept as the menu actions are (#859), and
        // those not removed yet by id.
        std::list<ContextMenuActionCall> contextMenuActionCalls{};
        std::map<int, ContextMenuActionCall*> contextMenuActionsById{};
        // Log-lines-change callback registered by plugin (optional, #858),
        // under registrationsMutex: it may register from any thread.
        LogSquirlLogLinesCallbackFn logLinesCallback = nullptr;
        void* logLinesUserData = nullptr;
        // The get_log_lines text, one per thread of the plugin's (#858).
        PerThreadText logLinesUtf8{};
        // What get_log_format_fields and get_log_line_fields return, one per
        // thread of the plugin's (#867).
        PerThread<LogFormatFieldsBuffer> logFormatFields{};
        PerThread<LogLineFieldsBuffer> logLineFields{};
        // The get_marks array, valid until the plugin's next call of it
        // (#866). UI thread only.
        std::vector<std::uint64_t> marks{};
    };

    /** Whether add_marks or remove_marks is carried out. */
    enum class MarksChange { Add, Remove };

    /**
     * add_marks and remove_marks (#866): checks the call and has the Plugin
     * UI Port carry it out on the host's thread, at once when called there,
     * else from the event loop unless the plugin was unloaded meanwhile.
     */
    static int changeMarks( void* handle, const char* filePath, const uint64_t* lines, size_t count,
                            MarksChange change );

    /**
     * The next id for a registration of the plugin, greater than 0; negative
     * once all are used up. Under the context's registrationsMutex.
     */
    static int nextRegistrationId( PluginContext& context );

    /** The loaded plugin's context, or nullptr when it is not loaded or is being unloaded. */
    PluginContext* loadedContext( const QString& pluginId ) const;

    /**
     * The loaded converters, not being unloaded, in plugin-ID order: the
     * order a file is offered to them in (#861).
     */
    std::vector<PluginContext*> convertersInIdOrder() const;

    /**
     * Takes the plugin out of every content check under way: none asks it
     * any more, and a call into it under way has returned when this returns.
     */
    void leaveContentChecks( const PluginContext& context );

    /** Finishes the content check id, whose worker has returned: reports it last. */
    void finishContentCheck( ContentCheckId id );

    /**
     * Runs call as a call into the plugin, unless the plugin is being
     * unloaded: then it returns false and runs nothing. An unload asked for
     * meanwhile is finished once the outermost such call has returned.
     */
    bool callIntoPlugin( PluginContext& context, const std::function<void()>& call );

    /**
     * Delivers a Regex Lab answer as a call into the plugin, or parks it
     * while the plugin's unload is pending or earlier answers are parked.
     */
    void answerPlugin( PluginContext& context, std::function<void()> answer );

    /** Delivers the parked answers from the event loop, unless the plugin goes meanwhile. */
    void deliverParkedAnswersLater( PluginContext& context );

    /** Shuts the plugin down and lets its context go. */
    void unloadNow( const QString& pluginId );

    /**
     * Ends the plugin's stream, if one runs, once a push under way has
     * returned: signals end-of-stream, frees it and says it stopped.
     */
    void endStream( PluginContext& context, const QString& pluginId );

    /**
     * Calls the plugin's logsquirl_datasource_stop, if its stream runs and it
     * exports it, as a call into the plugin; also while the plugin's unload
     * is pending, since it is not shut down yet and is owed the stop (#855).
     */
    void stopInPlugin( PluginContext& context );

    /** Starts the plugin's next queued Conversion, unless one is under way. */
    void startNextConversion( PluginContext& context );

    /**
     * Finishes the Conversion under way, whose call has returned: joins its
     * thread, reports its end last, and starts the next one.
     */
    void finishRunningConversion( PluginContext& context );

    /**
     * Cancels every Conversion of the plugin and waits for the one under
     * way to return; each is reported as cancelled.
     */
    void cancelConversionsOf( PluginContext& context );

    /** Reports a Conversion that never ran, or whose result is discarded, as cancelled. */
    static void reportCancelled( Conversion& conversion );

    /** The context whose Conversion under way is id, or nullptr. */
    PluginContext* contextConverting( ConversionId id );

    /** The Conversion under way with control, if it still is, on the host's thread. */
    void reportConversionProgress( const std::shared_ptr<ConversionControl>& control );

    /** The callback the host hands the Plugin UI Port for a plugin's menu action. */
    static void runMenuAction( void* menuActionCall );

    /**
     * The callback the host hands the Plugin UI Port for a plugin's context
     * menu item, on the UI thread: what was selected, as the plugin reads it.
     */
    static void runContextMenuAction( void* contextMenuActionCall,
                                      const PluginContextMenuContext& context );

    /** Extract a PluginContext from the opaque handle passed through host API. */
    static PluginContext* contextFromHandle( void* handle );

    /**
     * The Plugin UI Port and plugin ID for a host callback, or a null port
     * when the handle is unusable or no port is set.
     */
    static std::pair<PluginUiPort*, QString> uiPortFor( void* handle );

    const PluginCatalog& catalog_;
    std::map<QString, std::unique_ptr<PluginContext>> loaded_;
    std::uint64_t loadsSoFar_ = 0;
    ConversionId conversionsSoFar_ = 0;
    // The content checks under way, UI thread only (#861).
    std::list<std::unique_ptr<ContentCheck>> contentChecks_;
    ContentCheckId contentChecksSoFar_ = 0;

    PluginUiPort* uiPort_ = nullptr;

    std::function<void( const QString&, bool )> openFileCallback_;
    std::function<QString()> activeFilePathCallback_;

    const std::shared_ptr<PluginLogFiles> logFiles_ = std::make_shared<PluginLogFiles>();

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
    static int hostOpenRegexLab( void* handle, const char* pattern, int flags,
                                 LogSquirlRegexLabCallbackFn callback, void* userData );
    static int hostGoToLogLine( void* handle, uint64_t lineNumber );
    static int hostGetSelectedLogLines( void* handle, const char** text, size_t* length,
                                        size_t* lineCount );
    static int hostGetLogFileEncoding( void* handle, const char* filePath, const char** encoding );
    static void hostRegisterEncodingCallback( void* handle, LogSquirlEncodingCallbackFn callback,
                                              void* userData );
    static int hostRegisterMenuActionEx( void* handle, const char* menuPath, const char* label,
                                         PluginCallbackFn callback, void* userData );
    static void hostUnregisterMenuAction( void* handle, int id );
    static int hostGetLogLineCount( void* handle, const char* filePath, uint64_t* count );
    static int hostGetLogLines( void* handle, const char* filePath, uint64_t firstLine,
                                size_t maxLines, const char** text, size_t* length,
                                size_t* lineCount );
    static void hostRegisterLogLinesCallback( void* handle, LogSquirlLogLinesCallbackFn callback,
                                              void* userData );
    static int hostRegisterContextMenuAction( void* handle, const char* menuPath, const char* label,
                                              LogSquirlContextMenuCallbackFn callback,
                                              void* userData );
    static void hostUnregisterContextMenuAction( void* handle, int id );
    static int hostAddMarks( void* handle, const char* filePath, const uint64_t* lines,
                             size_t count );
    static int hostRemoveMarks( void* handle, const char* filePath, const uint64_t* lines,
                                size_t count );
    static int hostGetMarks( void* handle, const char* filePath, const uint64_t** lines,
                             size_t* count );
    static int hostGetLogFormatFields( void* handle, const char* filePath, const char** formatName,
                                       const char* const** fieldNames, size_t* fieldCount );
    static int hostGetLogLineFields( void* handle, const char* filePath, uint64_t firstLine,
                                     size_t maxLines, const char* const** values,
                                     const size_t** lengths, const int** parsed, size_t* fieldCount,
                                     size_t* lineCount );

    // The functions of the LogSquirlConvertControl handed to convert_ex,
    // called on the Conversion's worker thread.
    static void hostReportProgress( const LogSquirlConvertControl* control, uint64_t done,
                                    uint64_t total );
    static int hostIsCancelled( const LogSquirlConvertControl* control );
};

} // namespace logsquirl::plugins
