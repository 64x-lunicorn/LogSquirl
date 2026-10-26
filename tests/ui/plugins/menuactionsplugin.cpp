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

// A plugin built only for the tests (#857) that registers two menu items with
// register_menu_action_ex, as a plugin built against the current header does:
// "Stays" directly in the Plugins menu and "Goes" in Plugins > Menu Probe >
// Sub, a submenu nothing else is in. It counts how often each one ran. The
// tests read the ids and the counts, and unregister ids, through extra
// exports.

#include "logsquirl_plugin_api.h"

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

// The ids of "Stays" and "Goes", and how often each ran.
int ids[ 2 ] = { 0, 0 };
int runs[ 2 ] = { 0, 0 };

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.menu-actions-plugin",
    "Menu Actions Plugin",
    "1.0.0",
    "Registers menu items with an id and removes them",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersIds( size_t apiSize )
{
    return LOGSQUIRL_HOST_API_HAS( apiSize, register_menu_action_ex )
           && LOGSQUIRL_HOST_API_HAS( apiSize, unregister_menu_action );
}

void countRun( void* user_data )
{
    ++*static_cast<int*>( user_data );
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
    runs[ 0 ] = 0;
    runs[ 1 ] = 0;
    if ( offersIds( api_size ) ) {
        ids[ 0 ]
            = api->register_menu_action_ex( handle, "Plugins", "Stays", &countRun, &runs[ 0 ] );
        ids[ 1 ] = api->register_menu_action_ex( handle, "Menu Probe/Sub", "Goes", &countRun,
                                                 &runs[ 1 ] );
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

/** The id of "Stays" (0) or "Goes" (1) the host returned. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_menu_actions_plugin_id( int item )
{
    return ids[ item ];
}

/** How often "Stays" (0) or "Goes" (1) ran. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_menu_actions_plugin_runs( int item )
{
    return runs[ item ];
}

/** unregister_menu_action for the id, when the plugin is initialised. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_menu_actions_plugin_unregister( int id )
{
    if ( hostApi && offersIds( hostApiSize ) ) {
        hostApi->unregister_menu_action( hostHandle, id );
    }
}

} // extern "C"
