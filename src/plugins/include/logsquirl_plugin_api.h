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
 * Opaque handles (`void*`) are used for host–plugin communication.
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

/** Current plugin API version.  Bumped on incompatible changes. */
#define LOGSQUIRL_PLUGIN_API_VERSION 1

/* ── Plugin type ─────────────────────────────────────────────────────────── */

/** Describes the capability a plugin provides. */
typedef enum {
    LOGSQUIRL_PLUGIN_DATASOURCE = 0, /**< Streams log lines from external source */
    LOGSQUIRL_PLUGIN_CONVERTER  = 1, /**< Converts files into plain-text logs    */
    LOGSQUIRL_PLUGIN_UI         = 2  /**< Adds UI elements (toolbar, status bar) */
} LogSquirlPluginType;

/* ── Log levels (mirroring spdlog) ───────────────────────────────────────── */

typedef enum {
    LOGSQUIRL_LOG_TRACE    = 0,
    LOGSQUIRL_LOG_DEBUG    = 1,
    LOGSQUIRL_LOG_INFO     = 2,
    LOGSQUIRL_LOG_WARNING  = 3,
    LOGSQUIRL_LOG_ERROR    = 4,
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
    int         type;        /**< One of LogSquirlPluginType                                   */
    int         api_version; /**< Must equal LOGSQUIRL_PLUGIN_API_VERSION                      */
} LogSquirlPluginInfo;

/* ── Host API (provided by host, called by plugin) ───────────────────────── */

/**
 * Function-pointer table supplied by the host to the plugin during init.
 * The plugin stores this pointer and calls through it to interact with the
 * host.  The `handle` parameter in every call is the opaque handle that
 * was passed to logsquirl_plugin_init() — it identifies the plugin instance.
 *
 * **Lifetime**: the struct is valid from init until shutdown.
 * **Thread safety**: all functions are safe to call from any thread.
 */
typedef struct {
    /** API struct version (== LOGSQUIRL_PLUGIN_API_VERSION). */
    int api_version;

    /* ── DataSource callbacks ─────────────────────────────────────────── */

    /** Push a single log line (UTF-8). */
    void (*push_line)( void* handle, const char* data, size_t len );

    /** Push multiple log lines in one call. */
    void (*push_lines)( void* handle,
                        const char* const* data,
                        const size_t* lens,
                        size_t count );

    /** Signal that the data source has reached end-of-stream. */
    void (*signal_eos)( void* handle );

    /** Signal an error in the data source. */
    void (*signal_error)( void* handle, const char* message );

    /* ── General utilities ────────────────────────────────────────────── */

    /** Write a message to the host log. */
    void (*log_message)( void* handle, int level, const char* message );

    /**
     * Return the plugin-private configuration directory (UTF-8 path).
     * The host creates the directory if it does not exist.
     * The returned string is valid until the next call to get_config_dir.
     */
    const char* (*get_config_dir)( void* handle );

    /** Show a transient notification to the user. */
    void (*show_notification)( void* handle, const char* message );

    /**
     * Open a file in the main viewer.
     * @param file_path  Absolute UTF-8 path to the file.
     * @param follow     Non-zero to open in follow/tail mode.
     */
    void (*open_file)( void* handle, const char* file_path, int follow );

    /* ── UI extension callbacks ───────────────────────────────────────── */

    /**
     * Register a QWidget* for the status bar area.
     * The plugin creates and owns the widget; the host parents it.
     * Cast your QWidget* to void* before passing.
     */
    void (*register_status_widget)( void* handle, void* qwidget_ptr );

    /** Remove a previously registered status bar widget. */
    void (*unregister_status_widget)( void* handle, void* qwidget_ptr );

    /**
     * Add a custom action to the host menu bar.
     * @param menu_path  Slash-separated menu path, e.g. "Tools/My Plugin".
     * @param label      Menu item label.
     * @param callback   Function called when the item is triggered.
     * @param user_data  Passed back to callback unchanged.
     */
    void (*register_menu_action)( void* handle,
                                  const char* menu_path,
                                  const char* label,
                                  void ( *callback )( void* user_data ),
                                  void* user_data );

    /**
     * Register a QWidget* as a new tab in the host sidebar.
     * The plugin creates and owns the widget; the host parents it.
     * The sidebar is shown automatically when a tab is added.
     * @param label         Tab label (UTF-8).
     * @param qwidget_ptr   Cast of a QWidget* to void*.
     */
    void (*register_sidebar_tab)( void* handle,
                                  const char* label,
                                  void* qwidget_ptr );

    /** Remove a previously registered sidebar tab. */
    void (*unregister_sidebar_tab)( void* handle, void* qwidget_ptr );

} LogSquirlHostApi;

/* ── Plugin-exported entry points ────────────────────────────────────────── */

/*
 * Every plugin shared library must export the following C functions.
 * Use the LOGSQUIRL_PLUGIN_EXPORT macro to ensure correct visibility.
 */

#ifdef _WIN32
#  define LOGSQUIRL_PLUGIN_EXPORT __declspec( dllexport )
#else
#  define LOGSQUIRL_PLUGIN_EXPORT __attribute__( ( visibility( "default" ) ) )
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
typedef int ( *LogSquirlConverterConvertFn )( const char* input_path,
                                              const char* output_path );

/* ── Canonical exported symbol names ─────────────────────────────────────── */

/*
 * Plugins should define their entry points with these exact names:
 *
 *   LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void );
 *   LOGSQUIRL_PLUGIN_EXPORT int  logsquirl_plugin_init( const LogSquirlHostApi*, void* );
 *   LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void );
 *   LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget );  // optional
 *
 * FileConverter plugins additionally export:
 *   LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void );
 *   LOGSQUIRL_PLUGIN_EXPORT int         logsquirl_converter_convert( const char*, const char* );
 */

#define LOGSQUIRL_PLUGIN_ENTRY_GET_INFO      "logsquirl_plugin_get_info"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT          "logsquirl_plugin_init"
#define LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN      "logsquirl_plugin_shutdown"
#define LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE     "logsquirl_plugin_configure"
#define LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS   "logsquirl_converter_get_extensions"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT    "logsquirl_converter_convert"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOGSQUIRL_PLUGIN_API_H */
