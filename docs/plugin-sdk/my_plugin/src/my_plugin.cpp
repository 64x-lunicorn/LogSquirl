/* my_plugin.cpp: a LogSquirl UI extension plugin. MIT licence example. */
#include "logsquirl_plugin_api.h"

#include <string>

namespace {

// Designated initializers name every field, in the order the header declares them.
const LogSquirlPluginInfo pluginInfo = {
    .id = "com.example.my-plugin",
    .name = "My Plugin",
    .version = "0.1.0",
    .description = "Says hello from the Plugins menu.",
    .author = "Your Name",
    .license = "MIT",
    .type = LOGSQUIRL_PLUGIN_UI,
    .api_version = LOGSQUIRL_PLUGIN_API_VERSION,
};

// Valid from init until shutdown.
const LogSquirlHostApi* host = nullptr;
void* hostHandle = nullptr;

void sayHello( void* /* user_data */ )
{
    host->show_notification( hostHandle, "Hello from My Plugin" );
}

// Called once, on the UI thread, with what the user did in the Regex Lab.
void patternTested( void* /* user_data */, int result, const char* pattern, int /* flags */ )
{
    if ( result == LOGSQUIRL_REGEX_LAB_APPLIED ) {
        host->show_notification( hostHandle, ( std::string( "Applied: " ) + pattern ).c_str() );
    }
}

void testPattern( void* /* user_data */ )
{
    host->open_regex_lab( hostHandle, "ERROR (\\d+)", LOGSQUIRL_REGEX_LAB_MATCH_CASE,
                          &patternTested, nullptr );
}

// Shows the Log Lines selected in the tab in front: a whole line, even when
// only some of its characters are selected.
void showSelection( void* /* user_data */ )
{
    const char* text = nullptr;
    size_t length = 0;
    if ( host->get_selected_log_lines( hostHandle, &text, &length, nullptr ) < 0 ) {
        return;
    }
    // A notification is a line or two: show only the first 200 bytes of what
    // may be up to 1 MiB, not in the middle of a UTF-8 character.
    size_t shown = length < 200 ? length : 200;
    while ( shown < length && shown > 0
            && ( static_cast<unsigned char>( text[ shown ] ) & 0xC0 ) == 0x80 ) {
        --shown;
    }
    host->show_notification( hostHandle, std::string( text, shown ).c_str() );
}

// Line numbers count from 1, as LogSquirl shows them.
void goToFirstLine( void* /* user_data */ )
{
    host->go_to_log_line( hostHandle, 1 );
}

} // namespace

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &pluginInfo;
}

// A host that passes the size of its table calls this one.
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi* api, void* handle,
                                                      size_t api_size )
{
    host = api;
    hostHandle = handle;
    api->log_message( handle, LOGSQUIRL_LOG_INFO, "My Plugin initialised" );
    api->register_menu_action( handle, "Plugins", "Say Hello", &sayHello, nullptr );
    // Only a host that offers the Regex Lab gets the item that opens it.
    if ( LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab ) ) {
        api->register_menu_action( handle, "Plugins", "Test Pattern", &testPattern, nullptr );
    }
    if ( LOGSQUIRL_HOST_API_HAS( api_size, get_selected_log_lines )
         && LOGSQUIRL_HOST_API_HAS( api_size, go_to_log_line ) ) {
        api->register_menu_action( handle, "Plugins", "Show Selection", &showSelection, nullptr );
        api->register_menu_action( handle, "Plugins", "Go to First Line", &goToFirstLine, nullptr );
    }
    return 0;
}

// An older host calls this one: its table has no function added later.
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    return logsquirl_plugin_init_ex( api, handle, LOGSQUIRL_HOST_API_BASE_SIZE );
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    host = nullptr;
    hostHandle = nullptr;
}

} // extern "C"
