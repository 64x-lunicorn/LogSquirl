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

// A converter plugin built only for the tests of a Conversion that runs in
// the background (#795). It converts a ".cvx" file by writing each of its
// lines behind a "converted: " prefix, and holds after each line until the
// test opens its gate, so a test sees the Conversion under way, cancels it
// or unloads the plugin meanwhile. An input whose first line is "fail" fails
// with 3. It is built twice from this source:
//
// - the probe, a plugin of kind "ui" that exports convert_ex, which reports
//   one line of progress per line and polls is_cancelled while it holds, and
//   convert for older hosts;
// - with CONVERT_PROBE_PLAIN, a plugin of kind "converter" with the plain
//   convert only, which cannot be cancelled: it holds until the gate opens.
//
// What it saw is handed to the test through extra exported functions. It
// uses the C and C++ standard libraries only.

#include "logsquirl_plugin_api.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

#if defined( CONVERT_PROBE_PLAIN )
const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.convert-probe-plain",
    "Plain Convert Probe",
    "1.0.0",
    "Converts .cvxplain files with the plain convert entry point, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_CONVERTER,
    LOGSQUIRL_PLUGIN_API_VERSION,
};
const char* const extensions = ".cvxplain";
#else
const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.convert-probe",
    "Convert Probe",
    "1.0.0",
    "Converts .cvx files with progress and cancellation, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};
const char* const extensions = ".cvx";
#endif

// Closed, a conversion holds after each line it wrote until it opens, or,
// through convert_ex, until it is cancelled; a step lets one held line pass.
std::atomic<bool> gateOpen{ false };
std::atomic<int> stepsGranted{ 0 };

// Whether a held conversion may go on: the gate is open, or a step was granted.
bool mayPass()
{
    if ( gateOpen ) {
        return true;
    }
    int granted = stepsGranted.load();
    while ( granted > 0 ) {
        if ( stepsGranted.compare_exchange_weak( granted, granted - 1 ) ) {
            return true;
        }
    }
    return false;
}
std::atomic<int> running{ 0 };
std::atomic<int> mostRunningAtOnce{ 0 };
std::atomic<int> conversionsStarted{ 0 };
std::atomic<int> conversionsReturned{ 0 };
std::atomic<int> cancellationsSeen{ 0 };
std::atomic<int> shutdownCalls{ 0 };

std::mutex pathsMutex;
std::vector<std::string> outputPaths;

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;

void noteStart( const char* output_path )
{
    ++conversionsStarted;
    const int now = ++running;
    int most = mostRunningAtOnce.load();
    while ( now > most && !mostRunningAtOnce.compare_exchange_weak( most, now ) ) {
    }
    const std::lock_guard<std::mutex> lock( pathsMutex );
    outputPaths.emplace_back( output_path );
}

int noteReturn( int result )
{
    --running;
    ++conversionsReturned;
    return result;
}

// Writes the lines; between lines, waits at the gate. control is NULL for the
// plain entry point, which cannot hear a cancellation.
int convertLines( const char* input_path, const char* output_path,
                  const LogSquirlConvertControl* control )
{
    noteStart( output_path );
    std::ifstream input( input_path );
    // Binary, so a line ends in "\n" on Windows too, as the tests expect.
    std::ofstream output( output_path, std::ios::trunc | std::ios::binary );
    if ( !input || !output ) {
        return noteReturn( 1 );
    }
    std::vector<std::string> lines;
    std::string line;
    while ( std::getline( input, line ) ) {
        lines.push_back( line );
    }
    if ( !lines.empty() && lines.front() == "fail" ) {
        return noteReturn( 3 );
    }
    const auto total = static_cast<uint64_t>( lines.size() );
    for ( uint64_t i = 0; i < total; ++i ) {
        output << "converted: " << lines[ static_cast<size_t>( i ) ] << '\n';
        output.flush();
        if ( control ) {
            control->report_progress( control, i + 1, total );
        }
        while ( !mayPass() ) {
            if ( control && control->is_cancelled( control ) ) {
                ++cancellationsSeen;
                return noteReturn( 2 );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }
    return noteReturn( output ? 0 : 1 );
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
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    ++shutdownCalls;
    hostApi = nullptr;
    hostHandle = nullptr;
}

LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void )
{
    return extensions;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char* input_path,
                                                         const char* output_path )
{
    return convertLines( input_path, output_path, nullptr );
}

#if !defined( CONVERT_PROBE_PLAIN )
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert_ex( const char* input_path,
                                                            const char* output_path,
                                                            const LogSquirlConvertControl* control )
{
    return convertLines( input_path, output_path, control );
}
#endif

/** Lets every conversion held at the gate, and every later one, run through. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_open_gate( void )
{
    gateOpen = true;
}

/** Holds every later conversion at the gate again. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_close_gate( void )
{
    gateOpen = false;
}

/** Lets one line of a held conversion pass, which then holds after the next. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_step( void )
{
    ++stepsGranted;
}

/** How many conversions are inside a convert call right now. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_running( void )
{
    return running.load();
}

/** The most conversions that were inside a convert call at the same time. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_most_running_at_once( void )
{
    return mostRunningAtOnce.load();
}

/** How many convert calls began. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_conversions_started( void )
{
    return conversionsStarted.load();
}

/** How many convert calls returned. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_conversions_returned( void )
{
    return conversionsReturned.load();
}

/** How many convert_ex calls returned because is_cancelled said so. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_cancellations_seen( void )
{
    return cancellationsSeen.load();
}

/** How often the host shut the probe down. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_shutdown_calls( void )
{
    return shutdownCalls.load();
}

/**
 * The output path of the n-th convert call (0 first), or an empty string.
 * Valid until the next convert call begins.
 */
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_probe_output_path( int n )
{
    const std::lock_guard<std::mutex> lock( pathsMutex );
    if ( n < 0 || static_cast<size_t>( n ) >= outputPaths.size() ) {
        return "";
    }
    return outputPaths[ static_cast<size_t>( n ) ].c_str();
}

/** Forgets what the probe counted, for the next test. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_reset( void )
{
    gateOpen = false;
    stepsGranted = 0;
    mostRunningAtOnce = 0;
    conversionsStarted = 0;
    conversionsReturned = 0;
    cancellationsSeen = 0;
    shutdownCalls = 0;
    const std::lock_guard<std::mutex> lock( pathsMutex );
    outputPaths.clear();
}

} // extern "C"
