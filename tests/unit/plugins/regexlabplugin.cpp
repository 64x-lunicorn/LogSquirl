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

// A plugin built only for the tests (#662) that has the user test a pattern in
// the Regex Lab, as a plugin built against the current header does: it
// exports logsquirl_plugin_init_ex as well as logsquirl_plugin_init, and adds
// its Test Pattern menu action only when the host offers open_regex_lab. It
// tells the answer it gets through a notification, and hands the size of the
// host callback table it saw, and how many answers it got, back to the tests
// through extra exports.

#include "logsquirl_plugin_api.h"

#include <string>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;
// Every answer the plugin got, also one after it was shut down.
int answers = 0;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.regex-lab-plugin",
    "Regex Lab Plugin",
    "1.0.0",
    "Has the user test a pattern in the Regex Lab",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

// The pattern the plugin has tested, as its rules would read it.
const char* const TestedPattern = "ERROR (\\d+)";

void patternTested( void* /* user_data */, int result, const char* pattern, int flags )
{
    ++answers;
    if ( !hostApi ) {
        return;
    }
    std::string answer = "cancelled";
    if ( result == LOGSQUIRL_REGEX_LAB_APPLIED ) {
        answer = ( flags & LOGSQUIRL_REGEX_LAB_MATCH_CASE ) != 0 ? "applied, matching case: "
                                                                 : "applied, ignoring case: ";
        answer += pattern;
    }
    hostApi->show_notification( hostHandle, answer.c_str() );
}

void testPattern( void* /* user_data */ )
{
    if ( hostApi->open_regex_lab( hostHandle, TestedPattern, LOGSQUIRL_REGEX_LAB_MATCH_CASE,
                                  &patternTested, nullptr )
         != 0 ) {
        hostApi->show_notification( hostHandle, "no Regex Lab" );
    }
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
    if ( LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab ) ) {
        api->register_menu_action( handle, "Plugins", "Test Pattern", &testPattern, nullptr );
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

/** The size of the host callback table the plugin was initialised with. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_regex_lab_plugin_host_api_size( void )
{
    return hostApiSize;
}

/** How many answers of the Regex Lab the plugin got, ever. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_regex_lab_plugin_answers( void )
{
    return answers;
}

} // extern "C"
