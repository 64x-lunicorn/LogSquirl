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

// A data source plugin built only for the tests of when its stream starts and
// stops (#855). It exports logsquirl_datasource_start and
// logsquirl_datasource_stop and writes down, in order, each call the host
// makes into it; the test adds what the host says meanwhile through an extra
// exported function, so one list shows the order of both. start pushes a
// line and returns what the test chose; stop pushes a last line. A hook the
// test sets runs inside start, inside stop and inside a menu action of the
// plugin's, as a dialog of the plugin's would. It uses the C and C++ standard libraries only.

#include "logsquirl_plugin_api.h"

#include <cstddef>
#include <string>

namespace {

const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.lifecycle-probe",
    "Lifecycle Probe",
    "1.0.0",
    "Writes down when its stream starts and stops, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_DATASOURCE,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

using Hook = void ( * )( void* user_data );

// All on the UI thread, as the host calls the plugin there.
const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
std::string events;
int startResult = LOGSQUIRL_DATASOURCE_STARTED;
size_t contextSize = 0;
Hook startHook = nullptr;
Hook stopHook = nullptr;
Hook menuHook = nullptr;
void* hookUserData = nullptr;

void runMenuHook( void* /* user_data */ )
{
    if ( menuHook ) {
        menuHook( hookUserData );
    }
}

void note( const char* event )
{
    if ( !events.empty() ) {
        events += ',';
    }
    events += event;
}

void push( const std::string& line )
{
    hostApi->push_line( hostHandle, line.data(), line.size() );
}

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &probeInfo;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    hostApi = api;
    hostHandle = handle;
    note( "init" );
    // A call into the plugin other than start and stop, for the test to run.
    api->register_menu_action( handle, "", "Run Hook", &runMenuHook, nullptr );
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    note( "shutdown" );
    hostApi = nullptr;
    hostHandle = nullptr;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_datasource_start( const LogSquirlDataSourceContext* context )
{
    note( "start" );
    contextSize = context ? context->size : 0;
    push( "lifecycle probe opened" );
    if ( startHook ) {
        startHook( hookUserData );
    }
    return startResult;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_datasource_stop( const LogSquirlDataSourceContext* context )
{
    note( "stop" );
    contextSize = context ? context->size : 0;
    push( "lifecycle probe closing" );
    if ( stopHook ) {
        stopHook( hookUserData );
    }
}

// ── For the tests only ──────────────────────────────────────────────────────

/** What happened since the last reset, comma-separated, e.g. "init,start,stop". */
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_lifecycle_probe_events( void )
{
    return events.c_str();
}

/** Adds what the test heard of the host to the list. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_lifecycle_probe_note( const char* event )
{
    note( event );
}

/** What logsquirl_datasource_start returns from now on. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_lifecycle_probe_set_start_result( int result )
{
    startResult = result;
}

/** The size of the context the host passed last. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_lifecycle_probe_context_size( void )
{
    return contextSize;
}

/**
 * Runs onStart inside start, onStop inside stop and onMenu inside the menu
 * action Run Hook, each with userData; NULL runs nothing.
 */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_lifecycle_probe_set_hooks( Hook onStart, Hook onStop,
                                                                  Hook onMenu, void* userData )
{
    startHook = onStart;
    stopHook = onStop;
    menuHook = onMenu;
    hookUserData = userData;
}

/** Forgets everything, the hooks included, and starts again with start returning 0. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_lifecycle_probe_reset( void )
{
    events.clear();
    startResult = LOGSQUIRL_DATASOURCE_STARTED;
    contextSize = 0;
    startHook = nullptr;
    stopHook = nullptr;
    menuHook = nullptr;
    hookUserData = nullptr;
}

} // extern "C"
