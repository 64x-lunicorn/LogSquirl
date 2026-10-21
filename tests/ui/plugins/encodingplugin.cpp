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

// A plugin built only for the tests (#803) that reads which Encoding a Log
// File is read in, as a plugin built against the current header does: it
// exports logsquirl_plugin_init_ex as well as logsquirl_plugin_init, and asks
// only when the host offers get_log_file_encoding. It keeps every encoding
// change the host tells it of. The tests read both through extra exports.

#include "logsquirl_plugin_api.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

// The Log Files and Encodings the encoding callback was told, in order.
std::vector<std::pair<std::string, std::string>> encodingChanges;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.encoding-plugin",
    "Encoding Plugin",
    "1.0.0",
    "Reads the encoding of a Log File",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersEncoding( size_t apiSize )
{
    return LOGSQUIRL_HOST_API_HAS( apiSize, get_log_file_encoding )
           && LOGSQUIRL_HOST_API_HAS( apiSize, register_encoding_callback );
}

void encodingChanged( void* /* user_data */, const char* file_path, const char* encoding )
{
    encodingChanges.emplace_back( file_path, encoding );
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
    encodingChanges.clear();
    if ( offersEncoding( api_size ) ) {
        api->register_encoding_callback( handle, &encodingChanged, nullptr );
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
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_encoding_plugin_host_api_size( void )
{
    return hostApiSize;
}

/**
 * get_log_file_encoding for file_path (NULL for the tab in front), the name
 * copied into name, cut to name_size; -100 when the plugin is not initialised.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_encoding_plugin_read( const char* file_path, char* name,
                                                            size_t name_size )
{
    if ( !hostApi || !offersEncoding( hostApiSize ) || name_size == 0 ) {
        return -100;
    }
    const char* encoding = nullptr;
    const auto result = hostApi->get_log_file_encoding( hostHandle, file_path, &encoding );
    const std::string read = encoding != nullptr ? encoding : "";
    const auto copied = read.size() < name_size ? read.size() : name_size - 1;
    std::memcpy( name, read.data(), copied );
    name[ copied ] = '\0';
    return result;
}

/** How many encoding changes the callback was told. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_encoding_plugin_change_count( void )
{
    return encodingChanges.size();
}

/** The Log File and Encoding of the change at index, valid until the next change. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_encoding_plugin_change( size_t index, const char** file_path,
                                                               const char** encoding )
{
    *file_path = encodingChanges[ index ].first.c_str();
    *encoding = encodingChanges[ index ].second.c_str();
}

} // extern "C"
