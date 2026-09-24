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

// One source, several plugin libraries for the Plugin Loader test (#444). Each
// library is built with one LOADER_FIXTURE_* definition that leaves out or
// breaks exactly one thing the loader relies on, so the test drives each
// failure path with a real library.

#include "logsquirl_plugin_api.h"

#include <cstring>

#if defined( LOADER_FIXTURE_BAD_API_VERSION )
constexpr int InfoApiVersion = 99;
#else
constexpr int InfoApiVersion = LOGSQUIRL_PLUGIN_API_VERSION;
#endif

namespace {

// Unused in the variant without get_info.
[[maybe_unused]] const LogSquirlPluginInfo fixtureInfo = {
    "io.github.logsquirl.test.loader-fixture",
    "Loader Fixture",
    "1.0.0",
    "Fails the Plugin Loader in one chosen way",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_CONVERTER,
    InfoApiVersion,
};

int shutdownCalls = 0;

} // namespace

extern "C" {

#if !defined( LOADER_FIXTURE_NO_GET_INFO )
LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
#if defined( LOADER_FIXTURE_NULL_INFO )
    return nullptr;
#else
    return &fixtureInfo;
#endif
}
#endif

#if !defined( LOADER_FIXTURE_NO_INIT )
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi*, void* )
{
#if defined( LOADER_FIXTURE_INIT_FAILS )
    return 7;
#else
    return 0;
#endif
}
#endif

#if !defined( LOADER_FIXTURE_NO_SHUTDOWN )
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    ++shutdownCalls;
}
#endif

#if defined( LOADER_FIXTURE_CONVERTER )
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void )
{
    return "abc;xyz";
}

// Succeeds for "good" -> "out"; otherwise reports both path lengths in bytes,
// so the test sees how the paths arrive.
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char* input, const char* output )
{
    if ( std::strcmp( input, "good" ) == 0 && std::strcmp( output, "out" ) == 0 ) {
        return 0;
    }
    return static_cast<int>( std::strlen( input ) * 100 + std::strlen( output ) );
}
#endif

/** How often the loader shut the fixture down. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_fixture_shutdown_calls( void )
{
    return shutdownCalls;
}

} // extern "C"
