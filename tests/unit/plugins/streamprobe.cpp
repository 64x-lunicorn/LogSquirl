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

// A data source plugin built only for the tests of a stream that ends while
// its lines arrive (#821). From init until shutdown a thread of its own pushes
// lines through the host in a loop, as a reader of a device does; shutdown
// pushes one more line, as a plugin may until shutdown returns, then stops
// and joins that thread. What it counted is handed to the test through extra
// exported functions. It uses the C and C++ standard libraries only.

#include "logsquirl_plugin_api.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace {

const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.stream-probe",
    "Stream Probe",
    "1.0.0",
    "Pushes lines from a thread until it is shut down, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_DATASOURCE,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

// Written by init before the pusher thread starts and by shutdown after it
// was joined: the thread reads them with those orderings.
const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;

std::atomic<bool> stopping{ false };
std::atomic<std::uint64_t> linesPushed{ 0 };
std::atomic<int> shutdownCalls{ 0 };
std::thread pusher;

void pushUntilShutdown()
{
    const char* const batch[] = { "stream probe batch a", "stream probe batch b" };
    const size_t batchLens[] = { 20, 20 };
    while ( !stopping.load() ) {
        const auto line = "stream probe line " + std::to_string( linesPushed.load() );
        hostApi->push_line( hostHandle, line.data(), line.size() );
        hostApi->push_lines( hostHandle, batch, batchLens, 2 );
        linesPushed.fetch_add( 3 );
        std::this_thread::yield();
    }
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
    stopping.store( false );
    pusher = std::thread( pushUntilShutdown );
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    shutdownCalls.fetch_add( 1 );
    // The last words of a plugin that is still allowed to push.
    const std::string last = "stream probe shutting down";
    hostApi->push_line( hostHandle, last.data(), last.size() );
    stopping.store( true );
    if ( pusher.joinable() ) {
        pusher.join();
    }
    hostApi = nullptr;
    hostHandle = nullptr;
}

// ── For the tests only ──────────────────────────────────────────────────────

/** How many lines the pusher thread has pushed since the last reset. */
LOGSQUIRL_PLUGIN_EXPORT uint64_t logsquirl_stream_probe_lines_pushed( void )
{
    return linesPushed.load();
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_stream_probe_shutdown_calls( void )
{
    return shutdownCalls.load();
}

/** Forgets what the probe counted; call while it is not initialised. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_stream_probe_reset( void )
{
    linesPushed.store( 0 );
    shutdownCalls.store( 0 );
}

} // extern "C"
