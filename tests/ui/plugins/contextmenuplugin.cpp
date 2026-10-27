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

// A plugin built only for the tests (#859) that adds two items to the context
// menu of the Log Lines with register_context_menu_action, as a plugin built
// against the current header does: "Probe Item" directly into the plugin
// section and "Nested" into its submenus Context Probe > Sub. It counts how
// often each one ran and keeps what the last choice was handed, and on which
// thread. The tests read that, and unregister ids, through extra exports.

#include "logsquirl_plugin_api.h"

#include <string>
#include <thread>
#include <vector>

namespace {

const LogSquirlHostApi* hostApi = nullptr;
void* hostHandle = nullptr;
size_t hostApiSize = 0;

// The ids of "Probe Item" and "Nested", and how often each ran.
int ids[ 2 ] = { 0, 0 };
int runs[ 2 ] = { 0, 0 };

// What the last choice of either item was handed, copied.
struct Choice {
    size_t size = 0;
    std::string filePath;
    int view = -1;
    std::vector<uint64_t> lineNumbers;
    uint64_t selectedLineCount = 0;
    bool hasSelectedText = false;
    std::string selectedText;
    std::thread::id thread;
};
Choice lastChoice;

const LogSquirlPluginInfo pluginInfo = {
    "io.github.logsquirl.test.context-menu-plugin",
    "Context Menu Plugin",
    "1.0.0",
    "Adds items to the context menu of the Log Lines and removes them",
    "LogSquirl Contributors",
    "GPL-3.0-or-later",
    LOGSQUIRL_PLUGIN_UI,
    LOGSQUIRL_PLUGIN_API_VERSION,
};

bool offersContextMenu( size_t apiSize )
{
    return LOGSQUIRL_HOST_API_HAS( apiSize, register_context_menu_action )
           && LOGSQUIRL_HOST_API_HAS( apiSize, unregister_context_menu_action );
}

void recordChoice( void* user_data, const LogSquirlContextMenuContext* context )
{
    ++*static_cast<int*>( user_data );
    Choice choice;
    choice.size = context->size;
    choice.filePath = context->file_path != nullptr ? context->file_path : "";
    choice.view = context->view;
    if ( context->line_numbers != nullptr ) {
        choice.lineNumbers.assign( context->line_numbers,
                                   context->line_numbers + context->line_number_count );
    }
    choice.selectedLineCount = context->selected_line_count;
    choice.hasSelectedText = context->selected_text != nullptr;
    if ( choice.hasSelectedText ) {
        choice.selectedText.assign( context->selected_text, context->selected_text_length );
    }
    choice.thread = std::this_thread::get_id();
    lastChoice = choice;
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
    lastChoice = Choice{};
    if ( offersContextMenu( api_size ) ) {
        ids[ 0 ] = api->register_context_menu_action( handle, "", "Probe Item", &recordChoice,
                                                      &runs[ 0 ] );
        ids[ 1 ] = api->register_context_menu_action( handle, "Context Probe/Sub", "Nested",
                                                      &recordChoice, &runs[ 1 ] );
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

/** The id of "Probe Item" (0) or "Nested" (1) the host returned. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_context_menu_plugin_id( int item )
{
    return ids[ item ];
}

/** How often "Probe Item" (0) or "Nested" (1) ran. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_context_menu_plugin_runs( int item )
{
    return runs[ item ];
}

/** unregister_context_menu_action for the id, when the plugin is initialised. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_context_menu_plugin_unregister( int id )
{
    if ( hostApi && offersContextMenu( hostApiSize ) ) {
        hostApi->unregister_context_menu_action( hostHandle, id );
    }
}

/** The size, the view and the selected line count the last choice was handed. */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_context_menu_plugin_last( size_t* size, int* view,
                                                                 uint64_t* selectedLineCount )
{
    *size = lastChoice.size;
    *view = lastChoice.view;
    *selectedLineCount = lastChoice.selectedLineCount;
}

/** The Log File the last choice was handed. */
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_context_menu_plugin_last_file_path( void )
{
    return lastChoice.filePath.c_str();
}

/** The line numbers the last choice was handed, and how many. */
LOGSQUIRL_PLUGIN_EXPORT const uint64_t* logsquirl_context_menu_plugin_last_lines( size_t* count )
{
    *count = lastChoice.lineNumbers.size();
    return lastChoice.lineNumbers.data();
}

/** The selected text the last choice was handed; NULL when it was handed none. */
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_context_menu_plugin_last_selected_text( void )
{
    return lastChoice.hasSelectedText ? lastChoice.selectedText.c_str() : nullptr;
}

/** Non-zero when the last choice ran on the thread calling this. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_context_menu_plugin_last_ran_here( void )
{
    return lastChoice.thread == std::this_thread::get_id() ? 1 : 0;
}

} // extern "C"
