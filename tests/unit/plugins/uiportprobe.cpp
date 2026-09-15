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

// A plugin built only for the tests (#175). It keeps the host callback table
// and the handle the host passes to init, and hands both back to the test
// through extra exported functions, so the test calls the very function
// pointers a published plugin calls. Like the published plugins, it
// unregisters its footer widget when it is shut down.

#include "logsquirl_plugin_api.h"

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
void* configureParent = nullptr;

// Stands in for the footer widget a plugin creates; the host never looks behind it.
int shutdownFooterWidget = 0;

const LogSquirlPluginInfo probeInfo = {
    "io.github.logsquirl.test.ui-port-probe",
    "UI Port Probe",
    "1.0.0",
    "Hands its host callback table to the tests",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

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
    if ( hostApi ) {
        hostApi->unregister_footer_widget( hostHandle, &shutdownFooterWidget );
    }
    hostApi = nullptr;
    hostHandle = nullptr;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget )
{
    configureParent = parent_widget;
}

/** The host callback table passed to init, or NULL when not initialised. */
LOGSQUIRL_PLUGIN_EXPORT const LogSquirlHostApi* logsquirl_probe_host_api( void )
{
    return hostApi;
}

/** The handle passed to init, or NULL when not initialised. */
LOGSQUIRL_PLUGIN_EXPORT void* logsquirl_probe_host_handle( void )
{
    return hostHandle;
}

/** The parent the host passed to the last configure call. */
LOGSQUIRL_PLUGIN_EXPORT void* logsquirl_probe_configure_parent( void )
{
    return configureParent;
}

/** The footer widget the probe unregisters when it is shut down. */
LOGSQUIRL_PLUGIN_EXPORT void* logsquirl_probe_shutdown_footer_widget( void )
{
    return &shutdownFooterWidget;
}

} // extern "C"
