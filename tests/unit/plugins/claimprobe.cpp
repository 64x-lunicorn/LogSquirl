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

// A converter plugin built only for the tests of a converter that claims a
// file by its content (#861). It claims every file whose first bytes are its
// magic -- "LSQCLAIM" unless the test sets another -- and converts it by
// writing each of its lines behind a "converted by <name>: " prefix, so a
// test sees which plugin converted a file. Its check holds while the test
// keeps its gate closed, so a test sees the check under way, and writes down
// on which thread it was called and whether after its shutdown. It is built
// twice from this source:
//
// - probe A, which also converts ".clmboth" files by their extension;
// - with CLAIM_PROBE_B, probe B, whose plugin id comes after A's, and which
//   converts ".clm" and ".clmboth" files by their extension.
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

namespace {

#if defined( CLAIM_PROBE_B )
const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.claim-probe-b",
    "Claim Probe B",
    "1.0.0",
    "Claims files by their content and converts .clm and .clmboth files, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_CONVERTER,
    LOGSQUIRL_PLUGIN_API_VERSION,
};
const char* const extensions = ".clm;.clmboth";
const char* const prefix = "converted by B: ";
#else
const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.claim-probe-a",
    "Claim Probe A",
    "1.0.0",
    "Claims files by their content and converts .clmboth files, for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_CONVERTER,
    LOGSQUIRL_PLUGIN_API_VERSION,
};
const char* const extensions = ".clmboth";
const char* const prefix = "converted by A: ";
#endif

// Open, a check returns at once; closed, it holds until the gate opens.
std::atomic<bool> gateOpen{ true };
std::atomic<bool> initialised{ false };
std::atomic<int> claimsCalls{ 0 };
std::atomic<int> claimsRunning{ 0 };
std::atomic<int> claimsOnInitThread{ 0 };
std::atomic<int> claimsWhileShutDown{ 0 };
std::atomic<int> shutdownsWhileClaiming{ 0 };
std::atomic<int> shutdownCalls{ 0 };
std::atomic<int> conversions{ 0 };

// Under stateMutex: the magic, the thread init ran on and what the last
// check was handed.
std::mutex stateMutex;
std::string magic = "LSQCLAIM";
std::thread::id initThread;
std::string lastPath;
size_t lastHeadLength = 0;

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &probeInfo;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi*, void* )
{
    {
        const std::lock_guard<std::mutex> lock( stateMutex );
        initThread = std::this_thread::get_id();
    }
    initialised = true;
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    ++shutdownCalls;
    if ( claimsRunning > 0 ) {
        ++shutdownsWhileClaiming;
    }
    initialised = false;
}

LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void )
{
    return extensions;
}

LOGSQUIRL_PLUGIN_EXPORT int
logsquirl_converter_claims( const char* file_path, const unsigned char* head, size_t head_length )
{
    ++claimsRunning;
    ++claimsCalls;
    if ( !initialised ) {
        ++claimsWhileShutDown;
    }
    {
        const std::lock_guard<std::mutex> lock( stateMutex );
        if ( std::this_thread::get_id() == initThread ) {
            ++claimsOnInitThread;
        }
        lastPath = file_path;
        lastHeadLength = head_length;
    }
    while ( !gateOpen ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    // Read once the gate opened: a test sets it while the check holds.
    std::string wanted;
    {
        const std::lock_guard<std::mutex> lock( stateMutex );
        wanted = magic;
    }
    const bool claims
        = head_length >= wanted.size() && std::memcmp( head, wanted.data(), wanted.size() ) == 0;
    --claimsRunning;
    return claims ? 1 : 0;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char* input_path,
                                                         const char* output_path )
{
    ++conversions;
    std::ifstream input( input_path, std::ios::binary );
    // Binary, so a line ends in "\n" on Windows too, as the tests expect.
    std::ofstream output( output_path, std::ios::trunc | std::ios::binary );
    if ( !input || !output ) {
        return 1;
    }
    std::string line;
    while ( std::getline( input, line ) ) {
        output << prefix << line << '\n';
    }
    return output ? 0 : 1;
}

/** Claims, from now on, the files whose first bytes are the length bytes of value. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_set_magic( const char* value, size_t length )
{
    const std::lock_guard<std::mutex> lock( stateMutex );
    magic.assign( value, length );
}

/** Holds every check, under way or later, until the gate opens. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_close_gate( void )
{
    gateOpen = false;
}

/** Lets every held check, and every later one, return. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_open_gate( void )
{
    gateOpen = true;
}

/** How many checks began. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_claims_calls( void )
{
    return claimsCalls.load();
}

/** How many checks are under way right now. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_claims_running( void )
{
    return claimsRunning.load();
}

/** How many checks ran on the thread logsquirl_plugin_init ran on. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_claims_on_init_thread( void )
{
    return claimsOnInitThread.load();
}

/** How many checks began while the probe was shut down. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_claims_while_shut_down( void )
{
    return claimsWhileShutDown.load();
}

/** How often the probe was shut down while a check was under way. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_shutdowns_while_claiming( void )
{
    return shutdownsWhileClaiming.load();
}

/** How often the host shut the probe down. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_shutdown_calls( void )
{
    return shutdownCalls.load();
}

/** How many convert calls began. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_conversions( void )
{
    return conversions.load();
}

/** The head length the last check was handed. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_probe_last_head_length( void )
{
    const std::lock_guard<std::mutex> lock( stateMutex );
    return lastHeadLength;
}

/** The path the last check was handed; valid until the next check begins. */
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_probe_last_path( void )
{
    const std::lock_guard<std::mutex> lock( stateMutex );
    return lastPath.c_str();
}

/** Forgets what the probe counted, opens its gate and claims "LSQCLAIM" again. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_probe_reset( void )
{
    gateOpen = true;
    claimsCalls = 0;
    claimsOnInitThread = 0;
    claimsWhileShutDown = 0;
    shutdownsWhileClaiming = 0;
    shutdownCalls = 0;
    conversions = 0;
    const std::lock_guard<std::mutex> lock( stateMutex );
    magic = "LSQCLAIM";
    lastPath.clear();
    lastHeadLength = 0;
}

} // extern "C"
