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

// A converter plugin built only for the tests (#303). It is slow to
// initialise -- it sleeps for LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS milliseconds
// -- and converts a ".slowconv" file by writing each of its lines behind a
// "converted: " prefix, so a test sees whether a Log File was opened through
// it. It uses the C and C++ standard libraries only.

#include "logsquirl_plugin_api.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {

const LogSquirlPluginInfo slowConverterInfo = {
    "io.github.logsquirl.test.slow-converter",
    "Slow Converter",
    "1.0.0",
    "Initialises slowly and converts .slowconv files for the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_CONVERTER,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &slowConverterInfo;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi*, void* )
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, on the loading thread.
    if ( const char* delay = std::getenv( "LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS" ) ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( std::atoi( delay ) ) );
    }
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void ) {}

LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void )
{
    return ".slowconv";
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char* input_path,
                                                         const char* output_path )
{
    std::ifstream input( input_path );
    std::ofstream output( output_path, std::ios::trunc );
    if ( !input || !output ) {
        return 1;
    }
    std::string line;
    while ( std::getline( input, line ) ) {
        output << "converted: " << line << '\n';
    }
    return output ? 0 : 1;
}

} // extern "C"
