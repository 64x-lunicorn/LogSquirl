/*
 * LogSquirl Plugin API — Stable C ABI for external plugins.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ---------------------------------------------------------------------------
 *
 * This header is the ONLY file a plugin needs to include.  It is intentionally
 * released under the MIT license so that proprietary and open-source plugins
 * alike can be built without any GPL obligations.
 *
 * The ABI is pure C: no C++ types, no Qt types, no templates cross the
 * boundary.  All strings are UTF-8 encoded, NUL-terminated `const char*`.
 * Opaque handles (`void*`) are used for host–plugin communication.  Every
 * function uses the platform's default C calling convention.
 *
 * The API grows compatibly (docs/adr/0017): new host functions are appended
 * to LogSquirlHostApi, and a plugin learns which ones the running host offers
 * from the size of the table the host passes to logsquirl_plugin_init_ex().
 *
 * See docs/plugin-sdk.md for the full developer guide.
 */

#ifndef LOGSQUIRL_PLUGIN_API_H
#define LOGSQUIRL_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────────────── */

/**
 * Current plugin API version.  Bumped on incompatible changes only: a host
 * function added to LogSquirlHostApi keeps it, see LOGSQUIRL_HOST_API_HAS.
 */
#define LOGSQUIRL_PLUGIN_API_VERSION 1

/* ── Plugin type ─────────────────────────────────────────────────────────── */

/** Describes the capability a plugin provides. */
typedef enum {
    LOGSQUIRL_PLUGIN_DATASOURCE = 0, /**< Streams log lines from external source */
    LOGSQUIRL_PLUGIN_CONVERTER = 1,  /**< Converts files into plain-text logs    */
    LOGSQUIRL_PLUGIN_UI = 2          /**< Adds UI elements (toolbar, status bar) */
} LogSquirlPluginType;

/* ── Log levels (mirroring spdlog) ───────────────────────────────────────── */

typedef enum {
    LOGSQUIRL_LOG_TRACE = 0,
    LOGSQUIRL_LOG_DEBUG = 1,
    LOGSQUIRL_LOG_INFO = 2,
    LOGSQUIRL_LOG_WARNING = 3,
    LOGSQUIRL_LOG_ERROR = 4,
    LOGSQUIRL_LOG_CRITICAL = 5
} LogSquirlLogLevel;

/* ── Plugin info (returned by plugin, read by host) ──────────────────────── */

/**
 * Static metadata about a plugin.  Returned by logsquirl_plugin_get_info().
 * All strings must remain valid for the lifetime of the plugin (typically
 * stored as static constants).
 */
typedef struct {
    const char* id;          /**< Reverse-domain identifier, e.g. "io.github.logsquirl.logcat" */
    const char* name;        /**< Human-readable display name                                  */
    const char* version;     /**< SemVer string, e.g. "1.2.0"                                  */
    const char* description; /**< One-line description                                         */
    const char* author;      /**< Author / organisation                                        */
    const char* license;     /**< SPDX license identifier, e.g. "MIT" or "GPL-3.0-or-later"    */
    int type;                /**< One of LogSquirlPluginType                                   */
    int api_version;         /**< Must equal LOGSQUIRL_PLUGIN_API_VERSION                      */
} LogSquirlPluginInfo;

/* ── Regex Lab ───────────────────────────────────────────────────────────── */

/** Flag for open_regex_lab and its callback: the pattern matches case. */
#define LOGSQUIRL_REGEX_LAB_MATCH_CASE 0x1

/** How the user left a Regex Lab a plugin opened. */
typedef enum {
    LOGSQUIRL_REGEX_LAB_CANCELLED = 0, /**< Cancel, or the window was closed   */
    LOGSQUIRL_REGEX_LAB_APPLIED = 1    /**< Apply: the pattern the user tested */
} LogSquirlRegexLabResult;

/**
 * Called once with the answer of a Regex Lab a plugin opened, on the UI
 * thread.
 * @param user_data  The user_data passed to open_regex_lab, unchanged.
 * @param result     One of LogSquirlRegexLabResult.
 * @param pattern    The applied pattern (UTF-8), valid during the call only;
 *                   NULL when cancelled.
 * @param flags      LOGSQUIRL_REGEX_LAB_MATCH_CASE when the applied pattern
 *                   matches case; 0 when cancelled.
 */
typedef void ( *LogSquirlRegexLabCallbackFn )( void* user_data, int result, const char* pattern,
                                               int flags );

/* ── Log Lines ───────────────────────────────────────────────────────────── */

/**
 * What go_to_log_line and get_selected_log_lines return.  Not negative is
 * success; negative says why nothing was done.
 */
typedef enum {
    LOGSQUIRL_LOG_LINES_OK = 0,                /**< Done                                     */
    LOGSQUIRL_LOG_LINES_TRUNCATED = 1,         /**< Done, with the first selected lines only */
    LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD = -1, /**< Called off the UI thread                 */
    LOGSQUIRL_LOG_LINES_NO_LOG_FILE = -2,      /**< The tab in front shows no Log File       */
    LOGSQUIRL_LOG_LINES_OUT_OF_RANGE = -3,     /**< The Log File has no line of that number  */
    LOGSQUIRL_LOG_LINES_NO_SELECTION = -4,     /**< No Log Line is selected                  */
    LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT = -5  /**< A NULL handle or text                    */
} LogSquirlLogLinesResult;

/** At most this many selected Log Lines are returned by get_selected_log_lines. */
#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES 1000

/** At most this many bytes (1 MiB, without the NUL) are returned by get_selected_log_lines. */
#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES 1048576

/* ── Host API (provided by host, called by plugin) ───────────────────────── */

/**
 * Function-pointer table supplied by the host to the plugin during init.
 * The plugin stores this pointer and calls through it to interact with the
 * host.  The `handle` parameter in every call is the opaque handle that
 * was passed to logsquirl_plugin_init() — it identifies the plugin instance.
 *
 * **Lifetime**: the struct is valid from init until shutdown.
 * **Thread safety**: all functions are safe to call from any thread, unless
 * their comment says they must be called on the UI thread.
 * **Growth**: functions added later are appended at the end, below "Added
 * later"; nothing is reordered, removed or changed in size, and only function
 * pointers are appended.  A host older than a function passes a shorter
 * table: check LOGSQUIRL_HOST_API_HAS before calling one of them.  Keep the
 * pointer the host passes; never copy *api (a copy reads the whole struct of
 * your header, past the end of an older host's table), and never read a
 * member LOGSQUIRL_HOST_API_HAS does not report.
 */
typedef struct {
    /** API struct version (== LOGSQUIRL_PLUGIN_API_VERSION). */
    int api_version;

    /* ── DataSource callbacks ─────────────────────────────────────────── */

    /** Push a single log line (UTF-8). */
    void ( *push_line )( void* handle, const char* data, size_t len );

    /** Push multiple log lines in one call. */
    void ( *push_lines )( void* handle, const char* const* data, const size_t* lens, size_t count );

    /** Signal that the data source has reached end-of-stream. */
    void ( *signal_eos )( void* handle );

    /** Signal an error in the data source. */
    void ( *signal_error )( void* handle, const char* message );

    /* ── General utilities ────────────────────────────────────────────── */

    /** Write a message to the host log. */
    void ( *log_message )( void* handle, int level, const char* message );

    /**
     * Return the plugin-private configuration directory (UTF-8 path).
     * The host creates the directory if it does not exist.
     * The returned string is valid until the next call to get_config_dir.
     */
    const char* ( *get_config_dir )( void* handle );

    /** Show a transient notification to the user. */
    void ( *show_notification )( void* handle, const char* message );

    /**
     * Open a file in the main viewer.
     * @param file_path  Absolute UTF-8 path to the file.
     * @param follow     Non-zero to open in follow/tail mode.
     */
    void ( *open_file )( void* handle, const char* file_path, int follow );

    /* ── UI extension callbacks ───────────────────────────────────────── */

    /**
     * Register a QWidget* for the status bar area.
     * The plugin creates and owns the widget; the host parents it.
     * Cast your QWidget* to void* before passing.
     */
    void ( *register_status_widget )( void* handle, void* qwidget_ptr );

    /** Remove a previously registered status bar widget. */
    void ( *unregister_status_widget )( void* handle, void* qwidget_ptr );

    /**
     * Add a custom action to the host's Plugins menu.
     * @param menu_path  Slash-separated path of submenus of the Plugins menu the
     *                   item goes into, e.g. "My Plugin/Sub" for
     *                   Plugins > My Plugin > Sub. An empty path (or "Plugins")
     *                   puts the item directly into the Plugins menu.
     * @param label      Menu item label.
     * @param callback   Function called when the item is triggered.
     * @param user_data  Passed back to callback unchanged.
     */
    void ( *register_menu_action )( void* handle, const char* menu_path, const char* label,
                                    void ( *callback )( void* user_data ), void* user_data );

    /**
     * Register a QWidget* as a new tab in the host sidebar.
     * The plugin creates and owns the widget; the host parents it.
     * The sidebar is shown automatically when a tab is added.
     * @param label         Tab label (UTF-8).
     * @param qwidget_ptr   Cast of a QWidget* to void*.
     */
    void ( *register_sidebar_tab )( void* handle, const char* label, void* qwidget_ptr );

    /** Remove a previously registered sidebar tab. */
    void ( *unregister_sidebar_tab )( void* handle, void* qwidget_ptr );

    /**
     * Register a QWidget* for the footer area (bottom of the window).
     * The plugin creates and owns the widget; the host parents it.
     */
    void ( *register_footer_widget )( void* handle, void* qwidget_ptr );

    /** Remove a previously registered footer widget. */
    void ( *unregister_footer_widget )( void* handle, void* qwidget_ptr );

    /* ── Active file queries ───────────────────────────────────────── */

    /**
     * Return the file path of the currently active (focused) log tab.
     * Returns an empty string if no file is open.
     * The returned pointer is valid until the next host API call.
     */
    const char* ( *get_active_file_path )( void* handle );

    /**
     * Register a callback invoked whenever the active log file changes
     * (e.g. when the user switches tabs or opens a new file).
     * @param callback   Function called with the new file path (UTF-8).
     * @param user_data  Passed back to callback unchanged.
     */
    void ( *register_active_file_callback )( void* handle,
                                             void ( *callback )( void* user_data,
                                                                 const char* file_path ),
                                             void* user_data );

    /* ── Added later: check LOGSQUIRL_HOST_API_HAS before calling ─────── */

    /**
     * Open the Regex Lab for a pattern, so the user tests it on Log Lines
     * of the tab in front or on pasted text and applies or cancels it
     * (LogSquirl 26.11).  The Lab reads the pattern as a Perl-compatible
     * regular expression, as Qt's QRegularExpression does; the user may
     * change the pattern and whether it matches case.  Returns at once.
     *
     * The callback is called once, on the UI thread, with the applied
     * pattern or with LOGSQUIRL_REGEX_LAB_CANCELLED -- also when the window
     * the Lab belongs to closes.  It is never called once the plugin is shut
     * down: a Lab still open then is closed.  Each call opens a Lab of its own.
     *
     * Call on the UI thread only.
     * @param pattern    The pattern to test (UTF-8); NULL is an empty one.
     * @param flags      LOGSQUIRL_REGEX_LAB_MATCH_CASE, or 0 to ignore case.
     * @param callback   Receives the answer; must not be NULL.
     * @param user_data  Passed back to callback unchanged.
     * @return 0 when the Lab opened; non-zero when it did not (not on the UI
     *         thread, no window, NULL callback), and callback is never called.
     */
    int ( *open_regex_lab )( void* handle, const char* pattern, int flags,
                             LogSquirlRegexLabCallbackFn callback, void* user_data );

    /**
     * Go to a Log Line of the tab in front, as Go to line does (LogSquirl
     * 26.11): the line is selected and scrolled into view in the Presentation
     * shown -- the Text View or the Table View -- and the Filtered View
     * selects the nearest line it shows.
     *
     * Call on the UI thread only.
     * @param line_number  The line number the view shows: the first Log Line
     *                     is 1, as in Go to line.  0 is out of range.
     * @return LOGSQUIRL_LOG_LINES_OK when the line is shown;
     *         LOGSQUIRL_LOG_LINES_OUT_OF_RANGE when the Log File has no such
     *         line (yet); LOGSQUIRL_LOG_LINES_NO_LOG_FILE without an open Log
     *         File; LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD off the UI thread;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle.
     */
    int ( *go_to_log_line )( void* handle, uint64_t line_number );

    /**
     * Read the text of the Log Lines selected in the tab in front (LogSquirl
     * 26.11): those of the view the user was last in -- the Filtered View or
     * the Presentation shown -- in the order of the Log File.  A selection
     * within a Log Line counts as that whole Log Line.
     *
     * The text is UTF-8, the Log Lines separated by '\n', with no '\n' after
     * the last one, and NUL-terminated.  It holds whole Log Lines, at most
     * LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES of them and at most
     * LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES bytes; a first Log Line longer
     * than that is cut at a character.  When it holds less than is selected,
     * the result is LOGSQUIRL_LOG_LINES_TRUNCATED.  The text is owned by the
     * host and valid until the plugin calls get_selected_log_lines again or
     * is shut down.
     *
     * Call on the UI thread only.
     * @param text        Receives the text; NULL when the result is negative.
     *                    Must not be NULL.
     * @param length      Receives the length of the text in bytes, without the
     *                    NUL, or 0; may be NULL.
     * @param line_count  Receives how many Log Lines the text holds, or 0;
     *                    may be NULL.
     * @return LOGSQUIRL_LOG_LINES_OK or LOGSQUIRL_LOG_LINES_TRUNCATED with the
     *         text; LOGSQUIRL_LOG_LINES_NO_SELECTION, _NO_LOG_FILE,
     *         _NOT_ON_UI_THREAD or _INVALID_ARGUMENT without.
     */
    int ( *get_selected_log_lines )( void* handle, const char** text, size_t* length,
                                     size_t* line_count );

} LogSquirlHostApi;

/**
 * The size of the LogSquirlHostApi of hosts that know no later function:
 * what a plugin assumes when the host calls logsquirl_plugin_init().
 */
#define LOGSQUIRL_HOST_API_BASE_SIZE offsetof( LogSquirlHostApi, open_regex_lab )

/** The size of a member of LogSquirlHostApi, in C and in C++. */
#ifdef __cplusplus
#define LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) sizeof( LogSquirlHostApi::member )
#else
#define LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) sizeof( ( (LogSquirlHostApi*)0 )->member )
#endif

/**
 * Non-zero when a host API table of size bytes holds the whole member,
 * i.e. the running host offers it.  size is what the host passed to
 * logsquirl_plugin_init_ex(), or LOGSQUIRL_HOST_API_BASE_SIZE.
 */
#define LOGSQUIRL_HOST_API_HAS( size, member )                                                     \
    ( offsetof( LogSquirlHostApi, member ) + LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) <= ( size ) )

/* ── Plugin-exported entry points ────────────────────────────────────────── */

/*
 * Every plugin shared library must export the following C functions.
 * Use the LOGSQUIRL_PLUGIN_EXPORT macro to ensure correct visibility.
 */

#ifdef _WIN32
#define LOGSQUIRL_PLUGIN_EXPORT __declspec( dllexport )
#else
#define LOGSQUIRL_PLUGIN_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

/**
 * Return static plugin metadata.  Called before init.
 * The returned pointer must remain valid for the lifetime of the library.
 */
typedef const LogSquirlPluginInfo* ( *LogSquirlPluginGetInfoFn )( void );

/**
 * Initialise the plugin.
 * @param api     Pointer to the host API function table (valid until shutdown).
 * @param handle  Opaque handle — pass back to every host API call.
 * @return 0 on success, non-zero on failure.
 */
typedef int ( *LogSquirlPluginInitFn )( const LogSquirlHostApi* api, void* handle );

/**
 * Initialise the plugin, knowing which host functions there are (optional).
 * A host that knows this entry point calls it instead of init; an older host
 * calls init.  A plugin that exports it still exports init: the host refuses
 * a plugin without init, which an older host could not load.
 * @param api       Pointer to the host API function table (valid until shutdown).
 * @param handle    Opaque handle — pass back to every host API call.
 * @param api_size  The size of the table in bytes: see LOGSQUIRL_HOST_API_HAS.
 * @return 0 on success, non-zero on failure.
 */
typedef int ( *LogSquirlPluginInitExFn )( const LogSquirlHostApi* api, void* handle,
                                          size_t api_size );

/** Shut down the plugin.  Release all resources. */
typedef void ( *LogSquirlPluginShutdownFn )( void );

/**
 * Open a configuration dialog (optional — may be NULL).
 * @param parent_widget  Cast of a QWidget* the plugin can use as dialog parent.
 */
typedef void ( *LogSquirlPluginConfigureFn )( void* parent_widget );

/* ── FileConverter-specific entry points (optional) ──────────────────────── */

/**
 * Return supported file extensions, semicolon-separated.
 * Example: ".har;.pcap;.etl"
 */
typedef const char* ( *LogSquirlConverterGetExtsFn )( void );

/**
 * Convert a file from a proprietary format to plain-text logs.
 * @param input_path   Absolute UTF-8 path to source file.
 * @param output_path  Absolute UTF-8 path where plain text must be written.
 * @return 0 on success, non-zero on failure.
 */
typedef int ( *LogSquirlConverterConvertFn )( const char* input_path, const char* output_path );

/* ── Canonical exported symbol names ─────────────────────────────────────── */

/*
 * Plugins should define their entry points with these exact names:
 *
 *   LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void );
 *   LOGSQUIRL_PLUGIN_EXPORT int  logsquirl_plugin_init( const LogSquirlHostApi*, void* );
 *   LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void );
 *   LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget );  // optional
 *
 * A plugin that calls host functions added later also exports (optional):
 *   LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi*, void*, size_t );
 *
 * FileConverter plugins additionally export:
 *   LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void );
 *   LOGSQUIRL_PLUGIN_EXPORT int         logsquirl_converter_convert( const char*, const char* );
 */

#define LOGSQUIRL_PLUGIN_ENTRY_GET_INFO "logsquirl_plugin_get_info"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT "logsquirl_plugin_init"
#define LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN "logsquirl_plugin_shutdown"
#define LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE "logsquirl_plugin_configure"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT_EX "logsquirl_plugin_init_ex"
#define LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS "logsquirl_converter_get_extensions"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT "logsquirl_converter_convert"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOGSQUIRL_PLUGIN_API_H */
