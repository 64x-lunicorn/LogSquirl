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
 * What go_to_log_line, get_selected_log_lines, get_log_file_encoding,
 * get_log_line_count, get_log_lines, add_marks, remove_marks and get_marks
 * return.  Not negative is success; negative says why nothing was done.
 */
typedef enum {
    LOGSQUIRL_LOG_LINES_OK = 0,                /**< Done                                     */
    LOGSQUIRL_LOG_LINES_TRUNCATED = 1,         /**< Done, with the first selected lines only */
    LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD = -1, /**< Called off the UI thread                 */
    LOGSQUIRL_LOG_LINES_NO_LOG_FILE = -2,      /**< No Log File in front, or none named open */
    LOGSQUIRL_LOG_LINES_OUT_OF_RANGE = -3,     /**< The Log File has no line of that number  */
    LOGSQUIRL_LOG_LINES_NO_SELECTION = -4,     /**< No Log Line is selected                  */
    LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT = -5  /**< A NULL handle or text                    */
} LogSquirlLogLinesResult;

/**
 * At most this many selected Log Lines are returned by get_selected_log_lines,
 * and at most this many Log Lines by get_log_lines.
 */
#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES 1000

/**
 * At most this many bytes (1 MiB, without the NUL) are returned by
 * get_selected_log_lines, and by get_log_lines.
 */
#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES 1048576

/** At most this many line numbers are taken by one call of add_marks or remove_marks. */
#define LOGSQUIRL_MARKS_MAX_LINES 10000

/** How the Log Lines of an open Log File changed (see register_log_lines_callback). */
typedef enum {
    /**
     * Log Lines were added at the end: what was read before stays as it was,
     * but for the last Log Line, which may have grown longer.
     */
    LOGSQUIRL_LOG_LINES_APPENDED = 0,
    /** The Log File was loaded anew, or reads differently: read again what you keep. */
    LOGSQUIRL_LOG_LINES_REPLACED = 1
} LogSquirlLogLinesChange;

/**
 * Called on the UI thread when the Log Lines of an open Log File change (see
 * register_log_lines_callback).
 * @param user_data   The user_data passed to register_log_lines_callback, unchanged.
 * @param file_path   The Log File (UTF-8), as get_active_file_path names it;
 *                    valid during the call only.
 * @param line_count  How many Log Lines it has now, as get_log_line_count
 *                    counts them.
 * @param change      One of LogSquirlLogLinesChange.
 */
typedef void ( *LogSquirlLogLinesCallbackFn )( void* user_data, const char* file_path,
                                               uint64_t line_count, int change );

/* ── Encoding ────────────────────────────────────────────────────────────── */

/**
 * Called on the UI thread when the host reads an open Log File in another
 * text encoding (see register_encoding_callback).
 * @param user_data  The user_data passed to register_encoding_callback, unchanged.
 * @param file_path  The Log File (UTF-8), as get_active_file_path names it;
 *                   valid during the call only.
 * @param encoding   The name of the encoding it is read in now, as
 *                   get_log_file_encoding returns it; valid during the call only.
 */
typedef void ( *LogSquirlEncodingCallbackFn )( void* user_data, const char* file_path,
                                               const char* encoding );

/* ── Context menu ────────────────────────────────────────────────────────── */

/** The view of Log Lines a context menu was opened in (LogSquirl 26.12). */
typedef enum {
    LOGSQUIRL_LOG_VIEW_TEXT = 0,    /**< The Text View, the Log File's Log Lines as text  */
    LOGSQUIRL_LOG_VIEW_TABLE = 1,   /**< The Table View, a Row per Log Line               */
    LOGSQUIRL_LOG_VIEW_FILTERED = 2 /**< The Filtered View, the Log Lines a Search shows */
} LogSquirlLogView;

/**
 * What the host hands an item of the context menu when the user chooses it
 * (see register_context_menu_action, LogSquirl 26.12): what was selected as
 * the menu opened.  The host owns it and everything it points to: valid
 * during the call only, so keep no pointer to it, and never copy the struct.
 * It grows by appending members, and size says how far the running host
 * filled it: check LOGSQUIRL_CONTEXT_MENU_CONTEXT_HAS before reading a member
 * added later.  The members below are always there.
 */
typedef struct LogSquirlContextMenuContext {
    /** sizeof( LogSquirlContextMenuContext ) in the host's build: how far the host filled it. */
    size_t size;

    /** The Log File (UTF-8), as get_active_file_path names it. */
    const char* file_path;

    /** One of LogSquirlLogView: the view the menu was opened in. */
    int view;

    /**
     * The numbers of the selected Log Lines, in the order of the Log File, as
     * the views and go_to_log_line number them: the first Log Line is 1.  In
     * the Filtered View too, they are the Log File's numbers.  At most
     * LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES of them, the first ones selected.
     * A right-click on a Log Line outside the selection selects that Log Line
     * first.  NULL, with a count of 0, when no Log Line is selected, as when
     * the menu opens below the last Log Line with none selected.
     */
    const uint64_t* line_numbers;
    size_t line_number_count;

    /**
     * How many Log Lines were selected: more than line_number_count when the
     * list holds only the first of them.
     */
    uint64_t selected_line_count;

    /**
     * The text selected within one Log Line or one cell of the Table View (a
     * word, an id), UTF-8 and NUL-terminated; NULL when whole Log Lines are
     * selected.  Read the text of whole Log Lines with get_log_lines.
     */
    const char* selected_text;
    /** The length of selected_text in bytes, without the NUL; 0 when it is NULL. */
    size_t selected_text_length;
} LogSquirlContextMenuContext;

/**
 * Non-zero when the context the host passed holds the whole member, i.e. the
 * running host filled it; the members above are always there.
 */
#define LOGSQUIRL_CONTEXT_MENU_CONTEXT_HAS( context, member )                                      \
    ( offsetof( LogSquirlContextMenuContext, member )                                              \
          + sizeof( ( (const LogSquirlContextMenuContext*)0 )->member )                            \
      <= ( context )->size )

/**
 * Called on the UI thread when the user chooses an item a plugin added to the
 * context menu (see register_context_menu_action).
 * @param user_data  The user_data passed to register_context_menu_action, unchanged.
 * @param context    What was selected as the menu opened; valid during the call only.
 */
typedef void ( *LogSquirlContextMenuCallbackFn )( void* user_data,
                                                  const LogSquirlContextMenuContext* context );

/* ── Host API (provided by host, called by plugin) ───────────────────────── */

/**
 * Function-pointer table supplied by the host to the plugin during init.
 * The plugin stores this pointer and calls through it to interact with the
 * host.  The `handle` parameter in every call is the opaque handle that
 * was passed to logsquirl_plugin_init() — it identifies the plugin instance.
 *
 * **Lifetime**: the struct is valid from init until shutdown.
 * **Thread safety**: all functions are safe to call from any thread, except
 * open_regex_lab, go_to_log_line, get_selected_log_lines,
 * get_log_file_encoding, get_active_file_path and get_marks, which must be
 * called on the UI thread; a call that changes the user interface, open_file,
 * add_marks and remove_marks included, is dispatched to the UI thread by the
 * host.
 * **Strings**: a string the plugin passes is copied before the call returns.
 * A string the host returns is the host's: it is valid until the plugin calls
 * the same function again or is shut down, whatever other plugins call
 * meanwhile; that of get_log_lines until the plugin calls it again on the
 * same thread.  A string the host passes to a callback is valid during the
 * call only.  Each function below says which of these its strings are.
 * **Registrations**: each register_* function says whether a second call
 * replaces the first registration or adds another, and how a registration is
 * removed.  Whatever a plugin still has registered is removed when it is
 * unloaded.  A registration that returns an id returns one greater than 0,
 * never handed out twice while the plugin is loaded, or a negative value
 * when nothing was registered; the matching unregister_* function does
 * nothing for an id it does not know, or no longer knows.
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
     * The host owns the path: it is valid until the plugin calls
     * get_config_dir again or is shut down.
     */
    const char* ( *get_config_dir )( void* handle );

    /** Show a transient notification to the user. */
    void ( *show_notification )( void* handle, const char* message );

    /**
     * Open a file in the main viewer.  May be called from any thread: the
     * file opens on the UI thread, at once when called there, else once the
     * host's event loop turns; a call queued by a plugin that is unloaded
     * before then is dropped.
     * @param file_path  Absolute UTF-8 path to the file.
     * @param follow     Non-zero to open in follow/tail mode.
     */
    void ( *open_file )( void* handle, const char* file_path, int follow );

    /* ── UI extension callbacks ───────────────────────────────────────── */

    /**
     * Register a QWidget* for the status bar area.
     * The plugin creates and owns the widget; the host parents it.
     * Cast your QWidget* to void* before passing.  Each widget is registered
     * once: registering another adds it, registering the same one again does
     * nothing.  unregister_status_widget removes it.
     */
    void ( *register_status_widget )( void* handle, void* qwidget_ptr );

    /** Remove a previously registered status bar widget. */
    void ( *unregister_status_widget )( void* handle, void* qwidget_ptr );

    /**
     * Add a custom action to the host's Plugins menu.  Each call adds an
     * item, but for a label the plugin already has an item of: then it adds
     * nothing.  The item goes away when the plugin is unloaded, not before:
     * an item to be removed earlier is registered with
     * register_menu_action_ex.
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
     * The sidebar is shown automatically when a tab is added.  Each widget is
     * registered once: registering another adds a tab, registering the same
     * one again does nothing.  unregister_sidebar_tab removes it.
     * @param label         Tab label (UTF-8).
     * @param qwidget_ptr   Cast of a QWidget* to void*.
     */
    void ( *register_sidebar_tab )( void* handle, const char* label, void* qwidget_ptr );

    /** Remove a previously registered sidebar tab. */
    void ( *unregister_sidebar_tab )( void* handle, void* qwidget_ptr );

    /**
     * Register a QWidget* for the footer area (bottom of the window).
     * The plugin creates and owns the widget; the host parents it.  Each
     * widget is registered once: registering another adds it, registering
     * the same one again does nothing.  unregister_footer_widget removes it.
     */
    void ( *register_footer_widget )( void* handle, void* qwidget_ptr );

    /** Remove a previously registered footer widget. */
    void ( *unregister_footer_widget )( void* handle, void* qwidget_ptr );

    /* ── Active file queries ───────────────────────────────────────── */

    /**
     * Return the file path of the currently active (focused) log tab.
     * Returns an empty string if no file is open.
     * The host owns the path: it is valid until the plugin calls
     * get_active_file_path again or is shut down.
     * Call on the UI thread only: called on another thread, it returns an
     * empty string, as without a file.
     */
    const char* ( *get_active_file_path )( void* handle );

    /**
     * Register a callback invoked, on the UI thread, whenever the active log
     * file changes (e.g. when the user switches tabs or opens a new file).
     * A plugin has one such callback: registering another replaces it, and a
     * NULL callback removes it.
     * @param callback   Function called with the new file path (UTF-8),
     *                   valid during the call only.
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
     * the result is LOGSQUIRL_LOG_LINES_TRUNCATED.  The host owns the text: it
     * is valid until the plugin calls get_selected_log_lines again or is shut
     * down.
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

    /**
     * Read which text encoding LogSquirl reads a Log File in (LogSquirl
     * 26.12): the one the user chose for its tab, else the one detected from
     * a byte order mark or guessed from the bytes, else the locale's.  A
     * plugin that reads the Log File itself reads it in this encoding, so its
     * Log Lines are those the views show.
     *
     * The name is the encoding's IANA name, e.g. "UTF-8", "UTF-16LE",
     * "UTF-16BE", "ISO-8859-1" or "windows-1252", NUL-terminated.  The host
     * owns the name: it is valid until the plugin calls get_log_file_encoding
     * again or is shut down.
     *
     * The encoding changes when the user chooses another one for the tab,
     * and may change when a load of the Log File detects another one: the
     * callback of register_encoding_callback tells of every change.  Ask
     * again in the active-file callback for the new tab in front.
     *
     * Call on the UI thread only.
     * @param file_path  The Log File (UTF-8 path), as get_active_file_path
     *                   names it; NULL or "" for the tab in front.
     * @param encoding   Receives the name; NULL when the result is negative.
     *                   Must not be NULL.
     * @return LOGSQUIRL_LOG_LINES_OK with the name;
     *         LOGSQUIRL_LOG_LINES_NO_LOG_FILE when the tab in front shows no
     *         Log File, or no tab shows the one named;
     *         LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD off the UI thread;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle or
     *         encoding.
     */
    int ( *get_log_file_encoding )( void* handle, const char* file_path, const char** encoding );

    /**
     * Register a callback invoked, on the UI thread, whenever LogSquirl reads
     * an open Log File in another text encoding (LogSquirl 26.12): the user
     * chose one for its tab, or a load detected another one.  It is called
     * for the Log File of any tab, in front or not, with the new encoding's
     * name: once for each tab, so twice for a Log File two windows show
     * when both read it in another encoding.  A plugin has one such
     * callback: registering another replaces it, and a NULL callback removes
     * it.
     * @param callback   Function called with the Log File and the encoding.
     * @param user_data  Passed back to callback unchanged.
     */
    void ( *register_encoding_callback )( void* handle, LogSquirlEncodingCallbackFn callback,
                                          void* user_data );

    /**
     * Add an item to the host's Plugins menu, as register_menu_action does,
     * and return an id that removes it again with unregister_menu_action
     * (LogSquirl 26.12).  Each call adds an item of its own; a plugin's items
     * have different labels.  May be called from any thread: the id is
     * returned at once, also when the item appears on the UI thread later.
     * @param menu_path  As for register_menu_action; NULL is an empty path.
     * @param label      Menu item label; must not be NULL or empty.
     * @param callback   Function called, on the UI thread, when the item is
     *                   triggered; must not be NULL.
     * @param user_data  Passed back to callback unchanged.
     * @return The item's id, greater than 0; negative when no item was added:
     *         a host without a user interface, a NULL handle, a NULL or empty
     *         label, a NULL callback, or a label the plugin already has an
     *         item of.
     */
    int ( *register_menu_action_ex )( void* handle, const char* menu_path, const char* label,
                                      void ( *callback )( void* user_data ), void* user_data );

    /**
     * Remove an item register_menu_action_ex added, from every window
     * (LogSquirl 26.12).  A submenu the host created for it goes too once
     * nothing is left in it.  Once the call returns, the item's callback is
     * not called again: a trigger still queued is dropped, and only a call
     * of it already running on the UI thread finishes.  An id the plugin
     * was not given, or one already removed, does nothing.  May be called
     * from any thread, from the item's own callback too.
     * @param id  What register_menu_action_ex returned.
     */
    void ( *unregister_menu_action )( void* handle, int id );

    /**
     * Count the Log Lines of an open Log File (LogSquirl 26.12): those
     * LogSquirl has indexed so far, so fewer while the Log File still loads.
     *
     * May be called from any thread; it never waits for the UI thread.
     * @param file_path  The Log File (UTF-8 path), as get_active_file_path or
     *                   the active-file callback names it, open in a tab of
     *                   any window, in front or not.
     * @param count      Receives the count; 0 when the result is negative.
     *                   Must not be NULL.
     * @return LOGSQUIRL_LOG_LINES_OK with the count;
     *         LOGSQUIRL_LOG_LINES_NO_LOG_FILE when no tab shows that Log File,
     *         also when its tab closes during the call;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle,
     *         file_path or count.
     */
    int ( *get_log_line_count )( void* handle, const char* file_path, uint64_t* count );

    /**
     * Read the text of Log Lines of an open Log File, from first_line on
     * (LogSquirl 26.12): as the views show them, in the Log File's encoding,
     * and for a converted or decompressed Log File the text LogSquirl shows,
     * not the bytes of the original file.
     *
     * The text is UTF-8, the Log Lines separated by '\n', with no '\n' after
     * the last one, and NUL-terminated.  It holds whole Log Lines, at most
     * max_lines and LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES of them and at most
     * LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES bytes; a first Log Line longer
     * than that is cut at a character.  When it holds fewer Log Lines than
     * max_lines and the Log File has, the result is
     * LOGSQUIRL_LOG_LINES_TRUNCATED: read on from first_line + line_count.
     * The host owns the text: it is valid until the plugin calls get_log_lines
     * again on the same thread, or is shut down, so each thread of the plugin
     * keeps its own.
     *
     * May be called from any thread; it never waits for the UI thread.  The
     * host keeps the Log File's data for the duration of the call.
     * @param file_path   The Log File, as for get_log_line_count.
     * @param first_line  The number of the first Log Line, as the views and
     *                    go_to_log_line number them: the first Log Line is 1.
     * @param max_lines   At most this many Log Lines; 0 reads none.
     * @param text        Receives the text; NULL when the result is negative.
     *                    Must not be NULL.
     * @param length      Receives the length of the text in bytes, without the
     *                    NUL, or 0; may be NULL.
     * @param line_count  Receives how many Log Lines the text holds, or 0;
     *                    may be NULL.
     * @return LOGSQUIRL_LOG_LINES_OK or LOGSQUIRL_LOG_LINES_TRUNCATED with the
     *         text; LOGSQUIRL_LOG_LINES_OUT_OF_RANGE for a first_line of 0 or
     *         past the last Log Line indexed so far;
     *         LOGSQUIRL_LOG_LINES_NO_LOG_FILE when no tab shows that Log File,
     *         also when its tab closes during the call;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle,
     *         file_path or text.
     */
    int ( *get_log_lines )( void* handle, const char* file_path, uint64_t first_line,
                            size_t max_lines, const char** text, size_t* length,
                            size_t* line_count );

    /**
     * Register a callback invoked, on the UI thread, whenever the Log Lines of
     * an open Log File change (LogSquirl 26.12), for the Log File of any tab,
     * in front or not:
     * - LOGSQUIRL_LOG_LINES_APPENDED: Log Lines were added at the end, the
     *   file grew -- followed or not.  What was read before stays as it was,
     *   but for the last Log Line, which may have grown longer.
     * - LOGSQUIRL_LOG_LINES_REPLACED: the Log File was loaded anew -- reloaded,
     *   truncated, or its first load finished -- or reads differently: another
     *   encoding, or another way of decoding its text.  What was read before
     *   may differ: read again.
     * Calls may be coalesced: one call may stand for several changes.  A
     * plugin has one such callback: registering another replaces it, and a
     * NULL callback removes it.
     * @param callback   Function called with the Log File, its Log Line count
     *                   after the change and the change.
     * @param user_data  Passed back to callback unchanged.
     */
    void ( *register_log_lines_callback )( void* handle, LogSquirlLogLinesCallbackFn callback,
                                           void* user_data );

    /**
     * Add an item to the context menu of the Log Lines (LogSquirl 26.12): that
     * of the Text View, the Table View and the Filtered View, in every window
     * and every tab, those opened later included.  The items of all plugins
     * sit below a separator at the end of the menu, in the order they were
     * registered; without any, there is no separator.  An item is always
     * shown and enabled: the host does not call the plugin as the menu opens.
     * Each call adds an item of its own.  unregister_context_menu_action
     * removes it, and so does unloading the plugin.  May be called from any
     * thread: the id is returned at once.
     * @param menu_path  Slash-separated path of submenus of that section the
     *                   item goes into, e.g. "My Plugin/Decode"; an empty
     *                   path, or NULL, puts it into the section itself.
     * @param label      Menu item label (UTF-8); must not be NULL or empty.
     * @param callback   Function called, on the UI thread, once the user chose
     *                   the item and the menu has closed, with what was
     *                   selected as it opened; must not be NULL.  It is not
     *                   called when the view the menu was opened in, or its
     *                   tab, closed meanwhile.
     * @param user_data  Passed back to callback unchanged.
     * @return The item's id, greater than 0; negative when no item was added:
     *         a host without a user interface, a NULL handle, a NULL or empty
     *         label, or a NULL callback.
     */
    int ( *register_context_menu_action )( void* handle, const char* menu_path, const char* label,
                                           LogSquirlContextMenuCallbackFn callback,
                                           void* user_data );

    /**
     * Remove an item register_context_menu_action added (LogSquirl 26.12):
     * a context menu opened later does not show it.  Once the call returns,
     * the item's callback is not called again: a choice of it still queued is
     * dropped, and only a call of it already running on the UI thread
     * finishes.  An id the plugin was not given, or one already removed,
     * does nothing.  May be called from any thread, from the item's own
     * callback too.
     * @param id  What register_context_menu_action returned.
     */
    void ( *unregister_context_menu_action )( void* handle, int id );

    /**
     * Mark Log Lines of an open Log File (LogSquirl 26.12): an analysis marks
     * what it found, and the user walks through it with Next Mark and
     * Previous Mark.  A Mark a plugin sets is an ordinary Mark: the bullet
     * margin and the Filtered View show it, the Session keeps it, and the
     * user removes it like any other.  It is set in every tab that shows the
     * Log File, in every window; each tab keeps its own Marks, as it does for
     * the user's.
     *
     * May be called from any thread: the host carries the call out on the UI
     * thread, at once when called there, else once the host's event loop
     * turns; a call queued by a plugin that is unloaded before then is
     * dropped, and a tab that closes meanwhile marks nothing.  Calls of one
     * thread are carried out in the order they were made.  A line of 0, or
     * past the last Log Line indexed when the call is carried out, is
     * skipped, and so is a line marked already: neither fails the call.
     * @param file_path  The Log File (UTF-8 path), as for get_log_line_count.
     * @param lines      The numbers of the Log Lines, as the views and
     *                   go_to_log_line number them: the first Log Line is 1.
     *                   In any order; copied before the call returns.  May be
     *                   NULL when count is 0.
     * @param count      How many numbers lines holds; at most
     *                   LOGSQUIRL_MARKS_MAX_LINES.
     * @return LOGSQUIRL_LOG_LINES_OK when the Marks are set, or will be;
     *         LOGSQUIRL_LOG_LINES_NO_LOG_FILE when no tab shows that Log File;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle or
     *         file_path, NULL lines with a count, or a count above
     *         LOGSQUIRL_MARKS_MAX_LINES: then nothing is marked.
     */
    int ( *add_marks )( void* handle, const char* file_path, const uint64_t* lines, size_t count );

    /**
     * Remove Marks of an open Log File (LogSquirl 26.12): the user's as well
     * as those a plugin set, for the host knows no difference.  As add_marks
     * does: in every tab that shows the Log File, from any thread, carried
     * out on the UI thread, with the same limits and results.  A line that is
     * not marked, of 0 or past the end is skipped.
     * @param file_path  The Log File (UTF-8 path), as for get_log_line_count.
     * @param lines      The numbers of the Log Lines, the first Log Line is 1;
     *                   may be NULL when count is 0.
     * @param count      At most LOGSQUIRL_MARKS_MAX_LINES.
     * @return As for add_marks.
     */
    int ( *remove_marks )( void* handle, const char* file_path, const uint64_t* lines,
                           size_t count );

    /**
     * Read the Marks of an open Log File (LogSquirl 26.12): the user's and
     * those plugins set, as numbers of Log Lines, the first Log Line being 1,
     * in the order of the Log File and each once.  When several tabs show
     * the Log File, a Log Line marked in any of them.  A Mark that add_marks
     * set from another thread is read once the host has carried that call
     * out.  The host owns the array: it is valid until the plugin calls
     * get_marks again or is shut down.
     *
     * Call on the UI thread only.
     * @param file_path  The Log File (UTF-8 path), as for get_log_line_count.
     * @param lines      Receives the array; NULL when there are no Marks or
     *                   the result is negative.  Must not be NULL.
     * @param count      Receives how many numbers the array holds, or 0.
     *                   Must not be NULL.
     * @return LOGSQUIRL_LOG_LINES_OK with the Marks, none included;
     *         LOGSQUIRL_LOG_LINES_NO_LOG_FILE when no tab shows that Log File;
     *         LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD off the UI thread;
     *         LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT for a NULL handle,
     *         file_path, lines or count.
     */
    int ( *get_marks )( void* handle, const char* file_path, const uint64_t** lines,
                        size_t* count );

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
 * Called on a host worker thread, never on the UI thread, and never while
 * another conversion of the same plugin runs: a plugin's conversions run one
 * after the other, those of different plugins at the same time.  The host
 * API may be called from inside, under its thread rules.
 * @param input_path   Absolute UTF-8 path to source file.
 * @param output_path  Absolute UTF-8 path where plain text must be written.
 * @return 0 on success, non-zero on failure.
 */
typedef int ( *LogSquirlConverterConvertFn )( const char* input_path, const char* output_path );

/**
 * What the host hands a conversion that reports progress and can be cancelled
 * (see LogSquirlConverterConvertExFn).  The host owns it: it is valid for the
 * duration of the convert_ex call, and its functions are called on the
 * thread convert_ex runs on only, each with the control itself as the first
 * argument.  It grows by appending members, and size says how many of them
 * the running host filled: check LOGSQUIRL_CONVERT_CONTROL_HAS before reading
 * a member added later.  Keep the pointer the host passes and never copy the
 * struct.
 */
typedef struct LogSquirlConvertControl {
    /** sizeof( LogSquirlConvertControl ) in the host's build: how far the host filled it. */
    size_t size;

    /** The host's own; never dereference or interpret it. */
    void* host;

    /**
     * Report how far the conversion is: done of total units of the plugin's
     * choosing (bytes, lines, records).  A total of 0 means unknown; the host
     * then shows the conversion as busy, without a share done.
     */
    void ( *report_progress )( const struct LogSquirlConvertControl* control, uint64_t done,
                               uint64_t total );

    /**
     * Non-zero once the user cancelled the conversion: return as soon as
     * possible then, with any code; what was written is discarded.
     */
    int ( *is_cancelled )( const struct LogSquirlConvertControl* control );
} LogSquirlConvertControl;

/**
 * Non-zero when the control the host passed holds the whole member, i.e.
 * the running host filled it; the members above are always there.
 */
#define LOGSQUIRL_CONVERT_CONTROL_HAS( control, member )                                           \
    ( offsetof( LogSquirlConvertControl, member )                                                  \
          + sizeof( ( (const LogSquirlConvertControl*)0 )->member )                                \
      <= ( control )->size )

/**
 * Convert a file, reporting progress and polling for cancellation through
 * control (optional).  A host that knows this entry point calls it instead of
 * convert, on a host worker thread and under the same rules; an older host
 * calls convert, so a plugin that exports convert_ex still exports convert.
 * A plugin may export either one, or both.
 * @param input_path   Absolute UTF-8 path to source file.
 * @param output_path  Absolute UTF-8 path where plain text must be written.
 * @param control      Progress out, cancellation in; valid during the call.
 * @return 0 on success, non-zero on failure or once cancelled.
 */
typedef int ( *LogSquirlConverterConvertExFn )( const char* input_path, const char* output_path,
                                                const LogSquirlConvertControl* control );

/* ── DataSource-specific entry points (optional) ─────────────────────────── */

/**
 * What the host hands logsquirl_datasource_start and logsquirl_datasource_stop
 * (LogSquirl 26.12).  The host owns it: it is valid during the call only, so
 * keep no pointer to it, and never copy the struct.  It grows by appending
 * members, and size says how far the running host filled it: read a member
 * added later only when size holds the whole member.
 */
typedef struct LogSquirlDataSourceContext {
    /** sizeof( LogSquirlDataSourceContext ) in the host's build: how far the host filled it. */
    size_t size;
} LogSquirlDataSourceContext;

/** What logsquirl_datasource_start returns. */
typedef enum {
    LOGSQUIRL_DATASOURCE_STARTED = 0,  /**< The stream runs                                */
    LOGSQUIRL_DATASOURCE_FAILED = 1,   /**< Not started: the user is told it failed       */
    LOGSQUIRL_DATASOURCE_CANCELLED = 2 /**< Not started: the user cancelled, nothing said */
} LogSquirlDataSourceStartResult;

/**
 * Start the plugin's stream (optional).  Called on the UI thread when the user
 * starts the data source, before the stream counts as started: open the
 * device, socket or process here.  The plugin may show a modal dialog of its
 * own to ask which.  Lines pushed from inside the call already reach the
 * stream, and are discarded when it does not start.  While the plugin's
 * stream runs, the host refuses another start without calling the plugin.
 * A host older than LogSquirl 26.12 calls neither start nor stop; the hosts
 * that do pass a LogSquirlHostApi that holds unregister_menu_action.
 * @param context  Valid during the call only.
 * @return LOGSQUIRL_DATASOURCE_STARTED when the stream runs: the host opens it
 *         in a tab, and lines pushed from then on reach it.
 *         LOGSQUIRL_DATASOURCE_CANCELLED when the user cancelled: nothing
 *         opens, and the user is told nothing.  LOGSQUIRL_DATASOURCE_FAILED,
 *         or any other value, when it failed: nothing opens, and the user is
 *         told that the data source did not start, with the value; tell why
 *         with show_notification.  Only after LOGSQUIRL_DATASOURCE_STARTED is
 *         logsquirl_datasource_stop called.
 */
typedef int ( *LogSquirlDataSourceStartFn )( const LogSquirlDataSourceContext* context );

/**
 * Stop the plugin's stream (optional).  Called on the UI thread exactly once
 * for every start that returned LOGSQUIRL_DATASOURCE_STARTED, or for every
 * stream of a plugin that does not export logsquirl_datasource_start, however
 * the stream ends: the user stops it, the plugin is unloaded, or LogSquirl
 * quits.  It is called before the host signals end-of-stream, and on an
 * unload before logsquirl_plugin_shutdown: push the last lines and close the
 * device, socket or process here.  A line pushed until it returns reaches the
 * stream; a push that returns after it has returned may be dropped.  A plugin
 * that signalled end-of-stream itself is still stopped.
 * @param context  Valid during the call only.
 */
typedef void ( *LogSquirlDataSourceStopFn )( const LogSquirlDataSourceContext* context );

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
 *
 * A converter that reports progress and hears a cancellation also exports
 * (optional):
 *   LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert_ex( const char*, const char*,
 *                                                               const LogSquirlConvertControl* );
 *
 * A data source that hears when its stream starts and stops also exports
 * (optional, LogSquirl 26.12):
 *   LOGSQUIRL_PLUGIN_EXPORT int  logsquirl_datasource_start( const LogSquirlDataSourceContext* );
 *   LOGSQUIRL_PLUGIN_EXPORT void logsquirl_datasource_stop( const LogSquirlDataSourceContext* );
 */

#define LOGSQUIRL_PLUGIN_ENTRY_GET_INFO "logsquirl_plugin_get_info"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT "logsquirl_plugin_init"
#define LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN "logsquirl_plugin_shutdown"
#define LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE "logsquirl_plugin_configure"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT_EX "logsquirl_plugin_init_ex"
#define LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS "logsquirl_converter_get_extensions"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT "logsquirl_converter_convert"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT_EX "logsquirl_converter_convert_ex"
#define LOGSQUIRL_DATASOURCE_ENTRY_START "logsquirl_datasource_start"
#define LOGSQUIRL_DATASOURCE_ENTRY_STOP "logsquirl_datasource_stop"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOGSQUIRL_PLUGIN_API_H */
