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

// A plugin built only for the tests (#663) that goes to a Log Line and reads
// the selected Log Lines, as a plugin built against the current header does:
// it exports logsquirl_plugin_init_ex as well as logsquirl_plugin_init, and
// adds its menu actions only when the host offers go_to_log_line and
// get_selected_log_lines. The tests call both through extra exports, on the
// thread they choose, and get what the host returned.

#include "logsquirl_plugin_api.h"

#include <string>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.log-lines-plugin",
    "Log Lines Plugin",
    "1.0.0",
    "Goes to a Log Line and reads the selected Log Lines",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersLogLines( size_t apiSize )
{
    return LOGSQUIRL_HOST_API_HAS( apiSize, go_to_log_line )
           && LOGSQUIRL_HOST_API_HAS( apiSize, get_selected_log_lines );
}

// Tells the selected Log Lines, or why there are none, in a notification.
void showSelection( void* /* user_data */ )
{
    const char* text = nullptr;
    const auto result = hostApi->get_selected_log_lines( hostHandle, &text, nullptr, nullptr );
    hostApi->show_notification( hostHandle, result >= 0 ? text : std::to_string( result ).c_str() );
}

void goToTenthLine( void* /* user_data */ )
{
    hostApi->go_to_log_line( hostHandle, 10 );
}

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &pluginInfo;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi* api, void* handle,
                                                      size_t api_size )
{
    hostApi = api;
    hostHandle = handle;
    hostApiSize = api_size;
    if ( offersLogLines( api_size ) ) {
        api->register_menu_action( handle, "Plugins", "Show Selection", &showSelection, nullptr );
        api->register_menu_action( handle, "Plugins", "Go to Line 10", &goToTenthLine, nullptr );
    }
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    return logsquirl_plugin_init_ex( api, handle, LOGSQUIRL_HOST_API_BASE_SIZE );
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    hostApi = nullptr;
    hostHandle = nullptr;
}

// ── For the tests only ──────────────────────────────────────────────────────

/** The size of the host callback table the plugin was told. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_log_lines_plugin_host_api_size( void )
{
    return hostApiSize;
}

/** go_to_log_line, on the calling thread; -100 when the plugin is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_log_lines_plugin_go_to( uint64_t line_number )
{
    if ( !hostApi || !offersLogLines( hostApiSize ) ) {
        return -100;
    }
    return hostApi->go_to_log_line( hostHandle, line_number );
}

/** get_selected_log_lines, on the calling thread; -100 when the plugin is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT int
logsquirl_log_lines_plugin_read_selected( const char** text, size_t* length, size_t* line_count )
{
    if ( !hostApi || !offersLogLines( hostApiSize ) ) {
        return -100;
    }
    return hostApi->get_selected_log_lines( hostHandle, text, length, line_count );
}

} // extern "C"
