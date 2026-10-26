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

// A plugin built only for the tests (#858) that reads the Log Lines of an
// open Log File by its path, as a plugin built against the current header
// does: it exports logsquirl_plugin_init_ex as well as logsquirl_plugin_init,
// and reads only when the host offers get_log_line_count and get_log_lines.
// It keeps every change of Log Lines the host tells it of. The tests call
// the reads, on whichever thread they like, and read the changes through
// extra exports.

#include "logsquirl_plugin_api.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

// The changes the log lines callback was told, in order. The callback runs on
// the UI thread, the tests read them there too; guarded all the same.
struct Change {
    std::string filePath;
    uint64_t lineCount = 0;
    int change = -1;
};
std::mutex changesMutex;
std::vector<Change> changes;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.log-file-plugin",
    "Log File Plugin",
    "1.0.0",
    "Reads the Log Lines of an open Log File",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersLogLines( size_t apiSize )
{
    return LOGSQUIRL_HOST_API_HAS( apiSize, get_log_line_count )
           && LOGSQUIRL_HOST_API_HAS( apiSize, get_log_lines )
           && LOGSQUIRL_HOST_API_HAS( apiSize, register_log_lines_callback );
}

void logLinesChanged( void* /* user_data */, const char* file_path, uint64_t line_count,
                      int change )
{
    const std::scoped_lock lock( changesMutex );
    changes.push_back( { file_path, line_count, change } );
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
    {
        const std::scoped_lock lock( changesMutex );
        changes.clear();
    }
    if ( offersLogLines( api_size ) ) {
        api->register_log_lines_callback( handle, &logLinesChanged, nullptr );
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
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_logfile_plugin_host_api_size( void )
{
    return hostApiSize;
}

/** get_log_line_count for file_path; -100 when the plugin is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_logfile_plugin_count( const char* file_path, uint64_t* count )
{
    if ( !hostApi || !offersLogLines( hostApiSize ) ) {
        return -100;
    }
    return hostApi->get_log_line_count( hostHandle, file_path, count );
}

/**
 * get_log_lines for file_path, the host's text handed on as it is: valid
 * until the next read on the calling thread. -100 when the plugin is not
 * initialised.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_logfile_plugin_read( const char* file_path,
                                                           uint64_t first_line, size_t max_lines,
                                                           const char** text, size_t* length,
                                                           size_t* line_count )
{
    if ( !hostApi || !offersLogLines( hostApiSize ) ) {
        return -100;
    }
    return hostApi->get_log_lines( hostHandle, file_path, first_line, max_lines, text, length,
                                   line_count );
}

/** How many changes the log lines callback was told. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_logfile_plugin_change_count( void )
{
    const std::scoped_lock lock( changesMutex );
    return changes.size();
}

/** The change at index; its path is valid until the next change. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_logfile_plugin_change( size_t index, const char** file_path,
                                                              uint64_t* line_count, int* change )
{
    const std::scoped_lock lock( changesMutex );
    *file_path = changes[ index ].filePath.c_str();
    *line_count = changes[ index ].lineCount;
    *change = changes[ index ].change;
}

} // extern "C"
