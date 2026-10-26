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

// The ids of the items a host that removes menu items gave: 0 for none.
int sayHelloItem = 0;
int stopItem = 0;

void sayHello( void* /* user_data */ )
{
    host->show_notification( hostHandle, "Hello from My Plugin" );
}

// Removes both items, from the callback of one of them: neither runs again.
void stopSayingHello( void* /* user_data */ )
{
    host->unregister_menu_action( hostHandle, sayHelloItem );
    host->unregister_menu_action( hostHandle, stopItem );
    sayHelloItem = 0;
    stopItem = 0;
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

// A plugin that reads the Log File itself reads it in the encoding LogSquirl
// reads it in, so its Log Lines are those LogSquirl shows.
void showEncoding( void* /* user_data */ )
{
    const char* encoding = nullptr;
    if ( host->get_log_file_encoding( hostHandle, nullptr, &encoding ) < 0 ) {
        host->show_notification( hostHandle, "No Log File in front" );
        return;
    }
    host->show_notification( hostHandle, ( std::string( "Read as " ) + encoding ).c_str() );
}

// Called on the UI thread whenever LogSquirl reads a Log File in another encoding.
void encodingChanged( void* /* user_data */, const char* file_path, const char* encoding )
{
    host->log_message( hostHandle, LOGSQUIRL_LOG_INFO,
                       ( std::string( file_path ) + " is read as " + encoding ).c_str() );
}

// Counts the Log Lines of the tab in front. get_log_line_count and
// get_log_lines may be called from any thread too: a long analysis reads the
// Log File by its path on a thread of its own.
void countLines( void* /* user_data */ )
{
    const char* path = host->get_active_file_path( hostHandle );
    uint64_t count = 0;
    if ( host->get_log_line_count( hostHandle, path, &count ) < 0 ) {
        host->show_notification( hostHandle, "No Log File in front" );
        return;
    }
    host->show_notification( hostHandle, ( std::to_string( count ) + " Log Lines" ).c_str() );
}

// Called on the UI thread whenever the Log Lines of an open Log File change.
void logLinesChanged( void* /* user_data */, const char* file_path, uint64_t line_count,
                      int change )
{
    const char* how = change == LOGSQUIRL_LOG_LINES_APPENDED ? " grew to " : " was loaded anew: ";
    host->log_message(
        hostHandle, LOGSQUIRL_LOG_INFO,
        ( std::string( file_path ) + how + std::to_string( line_count ) + " Log Lines" ).c_str() );
}

// Called on the UI thread once the user chose Show Line Number in the context
// menu of the Log Lines: with what was selected as it opened, the Log Lines
// numbered from 1, and a word selected within one, if any.
void showLineNumber( void* /* user_data */, const LogSquirlContextMenuContext* context )
{
    if ( context->line_number_count == 0 ) {
        return;
    }
    std::string text = "Line " + std::to_string( context->line_numbers[ 0 ] );
    if ( context->selected_line_count > 1 ) {
        text += " and " + std::to_string( context->selected_line_count - 1 ) + " more";
    }
    if ( context->selected_text != nullptr ) {
        text += ": " + std::string( context->selected_text, context->selected_text_length );
    }
    host->show_notification( hostHandle, text.c_str() );
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
    // A host that removes menu items gets an item that removes Say Hello again.
    if ( LOGSQUIRL_HOST_API_HAS( api_size, register_menu_action_ex )
         && LOGSQUIRL_HOST_API_HAS( api_size, unregister_menu_action ) ) {
        sayHelloItem
            = api->register_menu_action_ex( handle, "Plugins", "Say Hello", &sayHello, nullptr );
        stopItem = api->register_menu_action_ex( handle, "Plugins", "Stop Saying Hello",
                                                 &stopSayingHello, nullptr );
    }
    else {
        api->register_menu_action( handle, "Plugins", "Say Hello", &sayHello, nullptr );
    }
    // Only a host that offers the Regex Lab gets the item that opens it.
    if ( LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab ) ) {
        api->register_menu_action( handle, "Plugins", "Test Pattern", &testPattern, nullptr );
    }
    if ( LOGSQUIRL_HOST_API_HAS( api_size, get_selected_log_lines )
         && LOGSQUIRL_HOST_API_HAS( api_size, go_to_log_line ) ) {
        api->register_menu_action( handle, "Plugins", "Show Selection", &showSelection, nullptr );
        api->register_menu_action( handle, "Plugins", "Go to First Line", &goToFirstLine, nullptr );
    }
    if ( LOGSQUIRL_HOST_API_HAS( api_size, get_log_file_encoding )
         && LOGSQUIRL_HOST_API_HAS( api_size, register_encoding_callback ) ) {
        api->register_menu_action( handle, "Plugins", "Show Encoding", &showEncoding, nullptr );
        api->register_encoding_callback( handle, &encodingChanged, nullptr );
    }
    if ( LOGSQUIRL_HOST_API_HAS( api_size, get_log_line_count )
         && LOGSQUIRL_HOST_API_HAS( api_size, register_log_lines_callback ) ) {
        api->register_menu_action( handle, "Plugins", "Count Lines", &countLines, nullptr );
        api->register_log_lines_callback( handle, &logLinesChanged, nullptr );
    }
    // An item at the end of the context menu of the Log Lines, in a submenu.
    if ( LOGSQUIRL_HOST_API_HAS( api_size, register_context_menu_action ) ) {
        api->register_context_menu_action( handle, "My Plugin", "Show Line Number", &showLineNumber,
                                           nullptr );
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
    sayHelloItem = 0;
    stopItem = 0;
}

} // extern "C"
