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

#include <cstddef>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
void* configureParent = nullptr;
int configureCalls = 0;

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

// Where each member of the table as LogSquirl 26.10 published it lies, in the
// header this probe is built against (#662): built against that header and
// against the current one, the probe must give the same offsets.
const size_t firstMemberOffsets[] = {
    offsetof( LogSquirlHostApi, api_version ),
    offsetof( LogSquirlHostApi, push_line ),
    offsetof( LogSquirlHostApi, push_lines ),
    offsetof( LogSquirlHostApi, signal_eos ),
    offsetof( LogSquirlHostApi, signal_error ),
    offsetof( LogSquirlHostApi, log_message ),
    offsetof( LogSquirlHostApi, get_config_dir ),
    offsetof( LogSquirlHostApi, show_notification ),
    offsetof( LogSquirlHostApi, open_file ),
    offsetof( LogSquirlHostApi, register_status_widget ),
    offsetof( LogSquirlHostApi, unregister_status_widget ),
    offsetof( LogSquirlHostApi, register_menu_action ),
    offsetof( LogSquirlHostApi, register_sidebar_tab ),
    offsetof( LogSquirlHostApi, unregister_sidebar_tab ),
    offsetof( LogSquirlHostApi, register_footer_widget ),
    offsetof( LogSquirlHostApi, unregister_footer_widget ),
    offsetof( LogSquirlHostApi, get_active_file_path ),
    offsetof( LogSquirlHostApi, register_active_file_callback ),
};

// Built against a header that has LogSquirlConvertControl, the header of
// LogSquirl 26.12 or a later one, the probe also says where each member of
// both tables as 26.12 published them lies and how big it is, and calls a
// 26.12 function as a plugin built then does (#863).
#ifdef LOGSQUIRL_CONVERT_CONTROL_HAS

#define LOGSQUIRL_PROBE_MEMBER( type, member ) offsetof( type, member ), sizeof( type::member )

// Offset and size of each member, in the order declared: those of
// LogSquirlHostApi, then those of LogSquirlConvertControl.
const size_t members2612[] = {
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, api_version ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, push_line ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, push_lines ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, signal_eos ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, signal_error ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, log_message ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, get_config_dir ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, show_notification ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, open_file ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_status_widget ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, unregister_status_widget ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_menu_action ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_sidebar_tab ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, unregister_sidebar_tab ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_footer_widget ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, unregister_footer_widget ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, get_active_file_path ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_active_file_callback ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, open_regex_lab ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, go_to_log_line ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, get_selected_log_lines ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, get_log_file_encoding ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlHostApi, register_encoding_callback ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlConvertControl, size ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlConvertControl, host ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlConvertControl, report_progress ),
    LOGSQUIRL_PROBE_MEMBER( LogSquirlConvertControl, is_cancelled ),
};

#undef LOGSQUIRL_PROBE_MEMBER

// The size of the table the host passed to init_ex, or the base size when it
// called init.
size_t hostApiSizePassed = 0;

#endif

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &probeInfo;
}

#ifdef LOGSQUIRL_CONVERT_CONTROL_HAS
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi* api, void* handle,
                                                      size_t api_size )
{
    hostApi = api;
    hostHandle = handle;
    hostApiSizePassed = api_size;
    return 0;
}

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    return logsquirl_plugin_init_ex( api, handle, LOGSQUIRL_HOST_API_BASE_SIZE );
}
#else
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    hostApi = api;
    hostHandle = handle;
    return 0;
}
#endif

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
    ++configureCalls;
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

/** How often the host called configure. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_configure_calls( void )
{
    return configureCalls;
}

/** The footer widget the probe unregisters when it is shut down. */
LOGSQUIRL_PLUGIN_EXPORT void* logsquirl_probe_shutdown_footer_widget( void )
{
    return &shutdownFooterWidget;
}

/** sizeof( LogSquirlHostApi ) in the header the probe is built against. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_probe_host_api_size( void )
{
    return sizeof( LogSquirlHostApi );
}

/**
 * The offsets of the members of the 26.10 table, in the order declared, in the
 * header the probe is built against; count receives how many there are.
 */
LOGSQUIRL_PLUGIN_EXPORT const size_t* logsquirl_probe_first_member_offsets( size_t* count )
{
    *count = sizeof( firstMemberOffsets ) / sizeof( firstMemberOffsets[ 0 ] );
    return firstMemberOffsets;
}

#ifdef LOGSQUIRL_CONVERT_CONTROL_HAS
/**
 * The offset and size of each member of LogSquirlHostApi and then of
 * LogSquirlConvertControl as 26.12 published them, in the header the probe
 * is built against; count receives how many numbers there are, two a member.
 */
LOGSQUIRL_PLUGIN_EXPORT const size_t* logsquirl_probe_members_2612( size_t* count )
{
    *count = sizeof( members2612 ) / sizeof( members2612[ 0 ] );
    return members2612;
}

/** sizeof( LogSquirlConvertControl ) in the header the probe is built against. */
LOGSQUIRL_PLUGIN_EXPORT size_t logsquirl_probe_convert_control_size( void )
{
    return sizeof( LogSquirlConvertControl );
}

/** What logsquirl_probe_log_file_encoding returns on a host without the function. */
#define LOGSQUIRL_PROBE_NOT_OFFERED 100

/**
 * Asks the host which encoding the Log File at file_path is read in, as a
 * plugin built against 26.12 does: only when the table the host passed
 * holds get_log_file_encoding, else LOGSQUIRL_PROBE_NOT_OFFERED.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_probe_log_file_encoding( const char* file_path,
                                                               const char** encoding )
{
    if ( !hostApi || !LOGSQUIRL_HOST_API_HAS( hostApiSizePassed, get_log_file_encoding ) ) {
        return LOGSQUIRL_PROBE_NOT_OFFERED;
    }
    return hostApi->get_log_file_encoding( hostHandle, file_path, encoding );
}
#endif

} // extern "C"
