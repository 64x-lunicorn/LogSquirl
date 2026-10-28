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

// A plugin built only for the tests (#866) that sets, removes and reads the
// Marks of an open Log File by its path, as a plugin built against the
// current header does: it exports logsquirl_plugin_init_ex as well as
// logsquirl_plugin_init, and calls the Marks functions only when the host
// offers them. The tests call them through extra exports, on whichever
// thread they like.

#include "logsquirl_plugin_api.h"

#include <cstdint>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.marks-plugin",
    "Marks Plugin",
    "1.0.0",
    "Sets, removes and reads the Marks of an open Log File",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersMarks()
{
    return hostApi != nullptr && LOGSQUIRL_HOST_API_HAS( hostApiSize, add_marks )
           && LOGSQUIRL_HOST_API_HAS( hostApiSize, remove_marks )
           && LOGSQUIRL_HOST_API_HAS( hostApiSize, get_marks );
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

/** add_marks for file_path; -100 when the plugin is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_marks_plugin_add( const char* file_path,
                                                        const uint64_t* lines, size_t count )
{
    if ( !offersMarks() ) {
        return -100;
    }
    return hostApi->add_marks( hostHandle, file_path, lines, count );
}

/** remove_marks for file_path; -100 when the plugin is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_marks_plugin_remove( const char* file_path,
                                                           const uint64_t* lines, size_t count )
{
    if ( !offersMarks() ) {
        return -100;
    }
    return hostApi->remove_marks( hostHandle, file_path, lines, count );
}

/**
 * get_marks for file_path, the host's array handed on as it is: valid until
 * the next read. -100 when the plugin is not initialised.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_marks_plugin_get( const char* file_path,
                                                        const uint64_t** lines, size_t* count )
{
    if ( !offersMarks() ) {
        return -100;
    }
    return hostApi->get_marks( hostHandle, file_path, lines, count );
}

} // extern "C"
