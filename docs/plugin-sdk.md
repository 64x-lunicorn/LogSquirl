# LogSquirl Plugin SDK

This guide explains how to create plugins for
[LogSquirl](https://github.com/64x-lunicorn/LogSquirl), the cross-platform log
viewer. It describes the plugin API as
[`logsquirl_plugin_api.h`](../src/plugins/include/logsquirl_plugin_api.h)
defines it; where the two differ, the header is right.

## Architecture Overview

LogSquirl uses a **pure C ABI** boundary between the host application and
plugins. This design decouples plugin licensing from the GPL-licensed host:

- The SDK header (`logsquirl_plugin_api.h`) is released under the **MIT licence**.
  `cmake --install` installs it as `include/logsquirl/logsquirl_plugin_api.h`.
- Plugins are standalone shared libraries (`.so` / `.dylib` / `.dll`).
- Plugins do **not** link against any host library — the host resolves their
  symbols at runtime via `QLibrary`.
- All data crosses the boundary as C primitives (`int`, `const char*`,
  `size_t`, `void*`).

### Plugin Types

| Type          | `type` in `plugin.json` | Enum                          | Purpose                                              |
|---------------|-------------------------|-------------------------------|------------------------------------------------------|
| Data Source   | `datasource`            | `LOGSQUIRL_PLUGIN_DATASOURCE` | Stream log lines into a tab of their own             |
| Converter     | `converter`             | `LOGSQUIRL_PLUGIN_CONVERTER`  | Convert a file format into plain text before viewing |
| UI Extension  | `ui`                    | `LOGSQUIRL_PLUGIN_UI`         | Add menu items, status-bar, sidebar or footer widgets |

---

## Quick Start

The example below is a complete UI extension plugin: it adds a *Say Hello*
item to the `Plugins` menu that shows a notification and, on a LogSquirl that
offers the Regex Lab to plugins, a *Test Pattern* item that has the user test
a pattern there (see [A Growing API](#a-growing-api)). It is the plugin in
[`docs/plugin-sdk/my_plugin/`](plugin-sdk/my_plugin/): LogSquirl's own build
compiles it against the current header and its tests load it, so what you
copy from here builds and loads.

### 1. Create a Plugin Directory

```
my_plugin/
├── CMakeLists.txt
├── plugin.json          # manifest
└── src/
    └── my_plugin.cpp    # implementation
```

### 2. Write the Manifest (`plugin.json`)

<!-- example: plugin.json -->
```json
{
    "id": "com.example.my-plugin",
    "name": "My Plugin",
    "version": "0.1.0",
    "type": "ui",
    "library": "my_plugin",
    "api_version": 1,
    "description": "Says hello from the Plugins menu.",
    "author": "Your Name",
    "license": "MIT"
}
```

| Field          | Required | Description                                                     |
|----------------|----------|-----------------------------------------------------------------|
| `id`           | **yes**  | Reverse-domain unique identifier: letters, digits, `.`, `-` and `_`, not starting with a dot. It names the plugin's folder. |
| `name`         | **yes**  | Human-readable display name.                                    |
| `version`      | **yes**  | Semver string.                                                  |
| `type`         | **yes**  | One of `datasource`, `converter`, `ui`.                         |
| `library`      | **yes**  | File name of the shared library.                                |
| `api_version`  | **yes**  | Must be `1` (`LOGSQUIRL_PLUGIN_API_VERSION`).                   |
| `description`  | no       | One-line description shown in the plugin dialog.                |
| `author`       | no       | Author name or organisation.                                    |
| `license`      | no       | SPDX identifier of the plugin's licence.                        |
| `icon`         | no       | File name of an icon next to `plugin.json`.                     |
| `formats`      | no       | Log Format files the plugin ships, see [Shipping Log Formats](#shipping-log-formats). |
| `predefined_filters` | no | Predefined Filter files the plugin ships, see [Shipping Predefined Filters and Highlighter Sets](#shipping-predefined-filters-and-highlighter-sets). |
| `highlighter_sets` | no   | Highlighter Set files the plugin ships, see [Shipping Predefined Filters and Highlighter Sets](#shipping-predefined-filters-and-highlighter-sets). |
| `qt_version`   | no       | The Qt the plugin is built against, as `major.minor` (`"6.8"`), see [Plugins That Use Qt](#plugins-that-use-qt). |

`library` is resolved relative to the directory containing `plugin.json`. It
may leave out the platform's prefix and extension: `my_plugin` finds
`libmy_plugin.so` on Linux, `libmy_plugin.dylib` or `libmy_plugin.so` on macOS
and `my_plugin.dll` on Windows, so one manifest serves every platform.

### 3. Write the Implementation

<!-- example: src/my_plugin.cpp -->
```cpp
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

// Marks the selected Log Lines, as the user would: Next Mark visits them.
// add_marks may be called from any thread too: a long analysis marks what it
// found on a thread of its own, at most LOGSQUIRL_MARKS_MAX_LINES per call.
void markSelection( void* /* user_data */, const LogSquirlContextMenuContext* context )
{
    host->add_marks( hostHandle, context->file_path, context->line_numbers,
                     context->line_number_count );
}

// Shows the fields of the first selected Log Line as the Table View shows
// them: named by the Log Format, the values UTF-8 with their lengths, as a
// value may hold a tab or a line feed. get_log_line_fields may be called from
// any thread too: statistics per level or per component read ranges of Log
// Lines on a thread of their own, at most LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES
// per call.
void showFields( void* /* user_data */, const LogSquirlContextMenuContext* context )
{
    if ( context->line_number_count == 0 ) {
        return;
    }
    const char* format = nullptr;
    const char* const* names = nullptr;
    size_t nameCount = 0;
    if ( host->get_log_format_fields( hostHandle, context->file_path, &format, &names, &nameCount )
         < 0 ) {
        host->show_notification( hostHandle, "No Log Format" );
        return;
    }
    const char* const* values = nullptr;
    const size_t* lengths = nullptr;
    const int* parsed = nullptr;
    size_t fieldCount = 0;
    size_t lineCount = 0;
    if ( host->get_log_line_fields( hostHandle, context->file_path, context->line_numbers[ 0 ], 1,
                                    &values, &lengths, &parsed, &fieldCount, &lineCount )
             < 0
         || lineCount == 0 || fieldCount != nameCount ) {
        // Another Log Format applies since the names were read: read them anew.
        return;
    }
    if ( parsed[ 0 ] == 0 ) {
        host->show_notification( hostHandle,
                                 ( std::string( "Not a Log Line of " ) + format ).c_str() );
        return;
    }
    std::string text = format;
    for ( size_t field = 0; field < fieldCount; ++field ) {
        text += ( field == 0 ? ": " : ", " ) + std::string( names[ field ] ) + "="
                + std::string( values[ field ], lengths[ field ] );
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
        if ( LOGSQUIRL_HOST_API_HAS( api_size, add_marks ) ) {
            api->register_context_menu_action( handle, "My Plugin", "Mark Selected Lines",
                                               &markSelection, nullptr );
        }
        if ( LOGSQUIRL_HOST_API_HAS( api_size, get_log_line_fields ) ) {
            api->register_context_menu_action( handle, "My Plugin", "Show Fields", &showFields,
                                               nullptr );
        }
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
```

The exported functions need C linkage: in C++, define them inside
`extern "C"`, or the host does not find them by name. The example exports
`logsquirl_plugin_init_ex` as well as `logsquirl_plugin_init`, so it runs on a
LogSquirl that knows the Regex Lab, Log Line, encoding, menu item removal,
Log File reading, context menu, Marks and field functions and on one that
does not.
On a LogSquirl that removes menu items, a
*Stop Saying Hello* item takes *Say Hello* and itself away again; on one with
plugin items in the context menu of the Log Lines, *My Plugin* → *Show Line
Number* there shows the number of the selected Log Line, and *My Plugin* →
*Mark Selected Lines* marks the selected Log Lines on a LogSquirl whose plugins
set Marks; *My Plugin* → *Show Fields* shows the fields of the first selected
Log Line, as the Table View shows them, on one whose plugins read them.

### 4. Build with CMake

<!-- example: CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.16)
project(my_plugin LANGUAGES CXX)

# The plugin includes the SDK header and nothing else of LogSquirl.
if(NOT EXISTS "${LOGSQUIRL_SDK_DIR}/logsquirl_plugin_api.h")
  message(FATAL_ERROR "Set LOGSQUIRL_SDK_DIR to the directory that holds logsquirl_plugin_api.h")
endif()

# A MODULE is a library that is loaded at runtime and never linked against.
add_library(my_plugin MODULE src/my_plugin.cpp)
target_include_directories(my_plugin PRIVATE "${LOGSQUIRL_SDK_DIR}")
target_compile_features(my_plugin PRIVATE cxx_std_20)
# Export only the functions marked LOGSQUIRL_PLUGIN_EXPORT.
set_target_properties(my_plugin PROPERTIES CXX_VISIBILITY_PRESET hidden)
```

```bash
cmake -B build -S . -DLOGSQUIRL_SDK_DIR=/path/to/directory/of/logsquirl_plugin_api.h
cmake --build build
```

### 5. Where LogSquirl Looks for Plugins

LogSquirl searches two plugin directories, the user plugin directory first,
then the one next to the application:

| Platform | User plugin directory                               | Next to the application                  |
|----------|-----------------------------------------------------|------------------------------------------|
| Linux    | `~/.local/share/logsquirl/plugins/`                 | `<directory of logsquirl>/plugins/`      |
| macOS    | `~/Library/Application Support/logsquirl/plugins/`  | `Contents/PlugIns/` in the app bundle    |
| Windows  | `%APPDATA%\logsquirl\plugins\`                      | `<directory of logsquirl.exe>\plugins\`  |

Plugins installed or updated from the catalog go into the user plugin
directory (see [ADR 0014](adr/0014-a-plugin-from-the-catalog-goes-into-the-user-plugin-directory-and-wins-over-the-shipped-copy.md)).
A portable LogSquirl keeps its user plugin directory beside its executable, in
the `plugins` folder next to the application, and searches that one folder
(see [ADR 0015](adr/0015-a-portable-run-keeps-its-data-beside-the-executable.md)).

Each plugin lives in a subdirectory of its own, directly below a plugin
directory, that holds its `plugin.json` and its library:

```
~/.local/share/logsquirl/plugins/
└── com.example.my-plugin/
    ├── plugin.json
    └── libmy_plugin.so
```

A library the plugin needs that LogSquirl does not ship, such as a Qt module
the application does not use, goes into the same subdirectory. On Windows
LogSquirl loads the plugin so that the libraries it imports are also found
there; on Linux and macOS the plugin finds them through an rpath of `$ORIGIN`
or `@loader_path`. A library a plugin delay-loads or loads itself on Windows is
looked for in the usual places only, so load it by its full path.

LogSquirl reads the manifests when it starts. If two directories hold a plugin
with the same `id`, the one found first is used. Plugins are enabled and
disabled in `Plugins` → `Plugin Management...`.

---

## Shipping Log Formats

A plugin can ship Log Formats: the JSON definitions LogSquirl recognizes a
log file by and shows it as a table with (see the user documentation's *Log
Formats* section for the file format, which is the same as for a file in the
user's Log Formats folder). This is declarative: the plugin names the files
in its manifest and LogSquirl reads them. No host function is involved, the
plugin's library does not need to run for its Log Formats to be known, and
the files travel in the plugin's release archive beside the library.

Add the optional `formats` key to `plugin.json` (added in LogSquirl 26.12), a
list of paths relative to the plugin directory:

```json
{
    "id": "com.example.tcpdump",
    "name": "tcpdump",
    "version": "1.0.0",
    "type": "converter",
    "library": "tcpdump_plugin",
    "api_version": 1,
    "formats": [ "formats/tcpdump_log.json" ]
}
```

```
~/.local/share/logsquirl/plugins/
└── com.example.tcpdump/
    ├── plugin.json
    ├── libtcpdump_plugin.so
    └── formats/
        └── tcpdump_log.json
```

Each file may hold one or more Log Formats, keyed by name as in a user's file.
A path that leaves the plugin directory (`../x.json`, an absolute path) makes
the manifest invalid, and the plugin is not listed at all. A manifest without
the key, or with an empty list, ships no Log Format and the plugin behaves as
before.

**When the Log Formats are read.** The Log Format Catalog, one per running
LogSquirl, is rebuilt as soon as the plugin manifests have been read after
start-up, whenever a plugin is enabled or disabled in Plugin Management, and
on every settings change. It reads the Log Formats of every *enabled* plugin,
whether or not the plugin's library is loaded (for example with auto-load
off). A log file opened before the plugins were discovered keeps the Log
Format it was recognized with until it is reloaded.

**Precedence.** The Catalog reads the built-in Log Formats first, then the
enabled plugins', then the user's folder, and the later one wins by name: a
plugin's Log Format replaces a built-in one of the same name, and a user's
replaces a plugin's. Between two enabled plugins that name the same Log
Format, the plugin whose `id` sorts first wins and a warning naming both
plugins and the Log Format is logged. Choose names unlikely to collide, such
as `tcpdump_log` rather than `log`.

**A file that cannot be read.** A missing file, one that is not JSON, or one
without a Log Format in it is skipped with a logged warning naming the plugin
and the file. The plugin's other Log Format files and the plugin itself are
unaffected: it still loads. Plugin Management shows the warning on the
plugin's card, and the Options dialog's Log Formats page shows every Log
Format with its Source: *Built-in*, *User*, or the plugin's name.

---

## Shipping Predefined Filters and Highlighter Sets

A plugin can ship Predefined Filters and Highlighter Sets for the logs it
deals with, say a "Crashes" filter group and a set that colours log levels.
Like Log Formats this is declarative: the plugin names the files in its
manifest, LogSquirl reads them, and no host function is involved.

Add the optional `predefined_filters` and `highlighter_sets` keys to
`plugin.json` (added in LogSquirl 26.12), each a list of paths relative to the
plugin directory, next to `formats`:

```json
{
    "id": "com.example.logcat",
    "name": "Logcat",
    "version": "1.0.0",
    "type": "converter",
    "library": "logcat_plugin",
    "api_version": 1,
    "formats": [ "formats/logcat_log.json" ],
    "predefined_filters": [ "filters/Crashes_filter.conf" ],
    "highlighter_sets": [ "highlighters/Levels_highlighter.conf" ]
}
```

**The files.** Each file has the format the *Predefined Filters* dialog and the
*Highlighters* dialog export: build the filter group or the Highlighter Set in
LogSquirl, select it, press *Export*, and put the file into the plugin
directory. A file may hold more than one group. The path rules are those of
`formats`: a path that leaves the plugin directory (`../x.conf`, an absolute
path) makes the manifest invalid, and the plugin is not listed at all. A
manifest without the keys, or with empty lists, ships none and the plugin
behaves as before.

**When they are read.** They are read for every *enabled* plugin, whether or
not its library is loaded, at the points the Log Format Catalog takes the
plugins' Log Formats: once the plugin manifests have been read after start-up,
and whenever a plugin is enabled or disabled in Plugin Management.

**How they show.** The Filters panel, the Highlighters menu and the two dialogs
list them after the user's own, each named with its Source, the plugin's name:
*Crashes (Logcat)*. They are applied like the user's own, but are read-only:
the dialogs show them in a section of their own, and *Copy to my groups* makes
a copy the user owns and can change. A group or set of the user's with the same
name stays as it is; both are listed, told apart by their Source.

**Never stored.** Nothing of a plugin's groups goes into the user's settings:
the user's Predefined Filters and Highlighter Sets are stored without them.
Only whether one of the plugin's Highlighter Sets is switched on is the user's,
and kept. Disabling the plugin removes its groups; if one of its Highlighter
Sets was switched on, it is switched off, and when no other is on the Log Lines
show as with no Highlighter Set chosen. Enabling the plugin again brings them
back, switched off.

**A file that cannot be read.** A missing file, one that is not an exported
filter group or Highlighter Set, or one of the other kind is skipped with a
logged warning naming the plugin and the file. The plugin's other files and the
plugin itself are unaffected.

---

## Plugins That Use Qt

Most plugins don't touch Qt. A plugin that hands a widget to the host
(`register_status_widget`, `register_sidebar_tab`, `register_footer_widget`)
or shows a dialog from `logsquirl_plugin_configure` links against the Qt 6
LogSquirl runs on, and works only with a Qt that can serve it. Qt 6 is
backward binary compatible: a plugin built against Qt 6.8 runs on Qt 6.8 and
every later Qt 6, but not on Qt 6.7, and not on another Qt major.

Such a plugin names the Qt it is built against in the optional `qt_version`
key of `plugin.json` (added in LogSquirl 26.12), as `major.minor`. LogSquirl
compares it with the Qt it runs on before it loads the plugin's library: a
plugin built against a newer Qt 6 minor or another Qt major is not loaded,
and its card in Plugin Management says why, enabled or not. A `qt_version`
that is not `major.minor` makes the manifest invalid, and the plugin is not
listed at all. A plugin without the key is loaded as before, so a plugin that
does not touch Qt leaves it out. An older LogSquirl ignores the key.

Let CMake write the version the plugin is really built against: keep the
manifest as `plugin.json.in` with

```json
    "qt_version": "@Qt6_VERSION_MAJOR@.@Qt6_VERSION_MINOR@"
```

and configure it next to the library:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
target_link_libraries(my_plugin PRIVATE Qt6::Widgets)
# The manifest names the Qt the plugin is built against.
configure_file(plugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/plugin.json" @ONLY)
```

Build against the oldest Qt 6 you want the plugin to run on. LogSquirl's
release builds use the Qt named in `BUILD.md`.

On Windows the compiler matters as well: LogSquirl's Windows build uses Qt
for MSVC (`win64_msvc2022_64`), and a plugin built with MinGW against a MinGW
Qt does not work with it, whatever its `qt_version`. LogSquirl does not check
this; build a Windows plugin that uses Qt with MSVC.

---

## C ABI Reference

### Version and Export Macro

```c
#define LOGSQUIRL_PLUGIN_API_VERSION 1

#ifdef _WIN32
#define LOGSQUIRL_PLUGIN_EXPORT __declspec( dllexport )
#else
#define LOGSQUIRL_PLUGIN_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif
```

Mark every exported function with `LOGSQUIRL_PLUGIN_EXPORT`. Every function
crossing the boundary, both ways, uses the platform's default C calling
convention; declare none of your own.

`LOGSQUIRL_PLUGIN_API_VERSION` changes only with an incompatible change. A host
function added later keeps it at `1`: see [A Growing API](#a-growing-api).

### Plugin Exports

Every plugin **must** export these three functions:

```c
LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void );
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi*, void* );
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void );
```

- `logsquirl_plugin_get_info` is called before `init` and returns a pointer to
  the plugin's metadata, which must stay valid as long as the library is
  loaded (a `static` or namespace-scope `const` object does).
- `logsquirl_plugin_init` receives the host API table and the opaque handle;
  it returns 0 on success and non-zero on failure. A plugin whose `init` fails
  is not loaded.
- `logsquirl_plugin_shutdown` releases everything the plugin allocated, and
  stops and joins the threads it started. The host calls it before it unloads
  the library, and before it ends a stream of the plugin's that is still
  running, but after it stopped that stream through
  `logsquirl_datasource_stop`.

**Optional** exports, for any plugin type:

```c
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget );
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi*, void*, size_t );
```

A host that knows `logsquirl_plugin_init_ex` calls it instead of
`logsquirl_plugin_init`, with the size of its host API table as the third
argument; an older host calls `logsquirl_plugin_init`. The host calls one of
them, never both. A plugin that exports `logsquirl_plugin_init_ex` still
exports `logsquirl_plugin_init`; the host refuses a plugin without it, which an
older host could not load.

A converter exports the extensions it handles and a convert function:

```c
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void );
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char*, const char* );
```

A converter that reports progress and hears a cancellation also exports
(optional, added in LogSquirl 26.12):

```c
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert_ex( const char*, const char*,
                                                            const LogSquirlConvertControl* );
```

A host that knows `logsquirl_converter_convert_ex` calls it instead of
`logsquirl_converter_convert`; an older host calls the latter, so a plugin
that exports `convert_ex` still exports `convert`. A plugin may export
either one, or both. See [Converters](#converters).

A converter that claims a file by its content as well also exports
(optional, added in LogSquirl 26.12):

```c
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_claims( const char*, const unsigned char*,
                                                        size_t );
```

A plugin without it is never asked, and converts the files its extensions
match as before. See [Claiming a File by Its Content](#claiming-a-file-by-its-content).

A data source that hears when its stream starts and stops also exports
(optional, added in LogSquirl 26.12):

```c
LOGSQUIRL_PLUGIN_EXPORT int  logsquirl_datasource_start( const LogSquirlDataSourceContext* );
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_datasource_stop( const LogSquirlDataSourceContext* );
```

Each is optional on its own; a plugin without them streams as before. An older
host never calls them: such a plugin opens its source in `init` there, see
[Data Source Streaming](#data-source-streaming).

The host resolves the functions by these names, which the header also defines
as constants:

```c
#define LOGSQUIRL_PLUGIN_ENTRY_GET_INFO "logsquirl_plugin_get_info"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT "logsquirl_plugin_init"
#define LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN "logsquirl_plugin_shutdown"
#define LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE "logsquirl_plugin_configure"
#define LOGSQUIRL_PLUGIN_ENTRY_INIT_EX "logsquirl_plugin_init_ex"
#define LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS "logsquirl_converter_get_extensions"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT "logsquirl_converter_convert"
#define LOGSQUIRL_CONVERTER_ENTRY_CONVERT_EX "logsquirl_converter_convert_ex"
#define LOGSQUIRL_CONVERTER_ENTRY_CLAIMS "logsquirl_converter_claims"
#define LOGSQUIRL_DATASOURCE_ENTRY_START "logsquirl_datasource_start"
#define LOGSQUIRL_DATASOURCE_ENTRY_STOP "logsquirl_datasource_stop"
```

and the function pointer types it casts them to:

```c
typedef const LogSquirlPluginInfo* ( *LogSquirlPluginGetInfoFn )( void );
typedef int ( *LogSquirlPluginInitFn )( const LogSquirlHostApi* api, void* handle );
typedef int ( *LogSquirlPluginInitExFn )( const LogSquirlHostApi* api, void* handle,
                                          size_t api_size );
typedef void ( *LogSquirlPluginShutdownFn )( void );
typedef void ( *LogSquirlPluginConfigureFn )( void* parent_widget );
typedef const char* ( *LogSquirlConverterGetExtsFn )( void );
typedef int ( *LogSquirlConverterConvertFn )( const char* input_path, const char* output_path );
typedef int ( *LogSquirlConverterConvertExFn )( const char* input_path, const char* output_path,
                                                const LogSquirlConvertControl* control );
typedef int ( *LogSquirlConverterClaimsFn )( const char* file_path, const unsigned char* head,
                                             size_t head_length );
typedef int ( *LogSquirlDataSourceStartFn )( const LogSquirlDataSourceContext* context );
typedef void ( *LogSquirlDataSourceStopFn )( const LogSquirlDataSourceContext* context );
```

### `LogSquirlConvertControl`

What the host hands `logsquirl_converter_convert_ex`: progress out,
cancellation in. The host owns it, it is valid for the duration of the call,
and its functions are called on the thread `convert_ex` runs on only, each
with the control itself as the first argument. It grows by appending members,
and `size` says how many of them the running host filled: check
`LOGSQUIRL_CONVERT_CONTROL_HAS( control, member )` before reading a member
added later (the members below are always there). Keep the pointer the host
passes and never copy the struct.

```c
typedef struct LogSquirlConvertControl {
    size_t size; /* sizeof( LogSquirlConvertControl ) in the host's build */

    void* host; /* the host's own; never dereference or interpret it */

    /* done of total units of the plugin's choosing; a total of 0 means unknown */
    void ( *report_progress )( const struct LogSquirlConvertControl* control, uint64_t done,
                               uint64_t total );

    /* non-zero once the user cancelled: return as soon as possible then */
    int ( *is_cancelled )( const struct LogSquirlConvertControl* control );
} LogSquirlConvertControl;

#define LOGSQUIRL_CONVERT_CONTROL_HAS( control, member )                                           \
    ( offsetof( LogSquirlConvertControl, member )                                                  \
          + sizeof( ( (const LogSquirlConvertControl*)0 )->member )                                \
      <= ( control )->size )
```

### `LogSquirlDataSourceContext`

What the host hands `logsquirl_datasource_start` and
`logsquirl_datasource_stop` (added in LogSquirl 26.12). The host owns it: it
is valid during the call only, so keep no pointer to it, and never copy the
struct. It holds only its size for now, and grows by appending members, as
`LogSquirlConvertControl` does: read a member added later only when `size`
holds the whole member.

```c
typedef struct LogSquirlDataSourceContext {
    size_t size; /* sizeof( LogSquirlDataSourceContext ) in the host's build */
} LogSquirlDataSourceContext;

typedef enum {
    LOGSQUIRL_DATASOURCE_STARTED = 0,  /* the stream runs                                */
    LOGSQUIRL_DATASOURCE_FAILED = 1,   /* not started: the user is told it failed       */
    LOGSQUIRL_DATASOURCE_CANCELLED = 2 /* not started: the user cancelled, nothing said */
} LogSquirlDataSourceStartResult;
```

`LogSquirlDataSourceStartResult` is what `logsquirl_datasource_start`
returns. Any value other than these three counts as failed, as
`LOGSQUIRL_DATASOURCE_FAILED` does.

### `LogSquirlPluginType`

```c
typedef enum {
    LOGSQUIRL_PLUGIN_DATASOURCE = 0,
    LOGSQUIRL_PLUGIN_CONVERTER = 1,
    LOGSQUIRL_PLUGIN_UI = 2
} LogSquirlPluginType;
```

### `LogSquirlPluginInfo`

Returned by `logsquirl_plugin_get_info`. All strings are UTF-8 and must stay
valid as long as the library is loaded.

```c
typedef struct {
    const char* id;          /* Reverse-domain identifier, as in plugin.json */
    const char* name;        /* Human-readable display name */
    const char* version;     /* SemVer string, e.g. "1.2.0" */
    const char* description; /* One-line description */
    const char* author;      /* Author / organisation */
    const char* license;     /* SPDX license identifier, e.g. "MIT" */
    int type;                /* One of LogSquirlPluginType */
    int api_version;         /* Must equal LOGSQUIRL_PLUGIN_API_VERSION */
} LogSquirlPluginInfo;
```

The host refuses a plugin whose `api_version` differs from its own
`LOGSQUIRL_PLUGIN_API_VERSION`; the plugin's type and the rest of its metadata
come from `plugin.json`.

### `LogSquirlHostApi`

The function-pointer table the host passes to `init`. It is valid from `init`
until `shutdown`, and its functions are safe to call from any thread, but for
`open_regex_lab`, `go_to_log_line`, `get_selected_log_lines`,
`get_log_file_encoding`, `get_active_file_path` and `get_marks`, which are
called on the UI thread only. `get_log_line_count`, `get_log_lines`,
`get_log_format_fields` and `get_log_line_fields` in particular are made for a
thread of the plugin's own: they never wait for the UI thread; nor do
`add_marks` and `remove_marks`, which the host carries out on the UI thread.

```c
typedef struct {
    int api_version; /* == LOGSQUIRL_PLUGIN_API_VERSION */

    /* Data source callbacks */
    void ( *push_line )( void* handle, const char* data, size_t len );
    void ( *push_lines )( void* handle, const char* const* data, const size_t* lens, size_t count );
    void ( *signal_eos )( void* handle );
    void ( *signal_error )( void* handle, const char* message );

    /* General utilities */
    void ( *log_message )( void* handle, int level, const char* message );
    const char* ( *get_config_dir )( void* handle );
    void ( *show_notification )( void* handle, const char* message );
    void ( *open_file )( void* handle, const char* file_path, int follow );

    /* UI extension callbacks */
    void ( *register_status_widget )( void* handle, void* qwidget_ptr );
    void ( *unregister_status_widget )( void* handle, void* qwidget_ptr );
    void ( *register_menu_action )( void* handle, const char* menu_path, const char* label,
                                    void ( *callback )( void* user_data ), void* user_data );
    void ( *register_sidebar_tab )( void* handle, const char* label, void* qwidget_ptr );
    void ( *unregister_sidebar_tab )( void* handle, void* qwidget_ptr );
    void ( *register_footer_widget )( void* handle, void* qwidget_ptr );
    void ( *unregister_footer_widget )( void* handle, void* qwidget_ptr );

    /* Active file queries */
    const char* ( *get_active_file_path )( void* handle );
    void ( *register_active_file_callback )( void* handle,
                                             void ( *callback )( void* user_data,
                                                                 const char* file_path ),
                                             void* user_data );

    /* Added later: check LOGSQUIRL_HOST_API_HAS before calling */
    int ( *open_regex_lab )( void* handle, const char* pattern, int flags,
                             LogSquirlRegexLabCallbackFn callback, void* user_data );
    int ( *go_to_log_line )( void* handle, uint64_t line_number );
    int ( *get_selected_log_lines )( void* handle, const char** text, size_t* length,
                                     size_t* line_count );
    int ( *get_log_file_encoding )( void* handle, const char* file_path, const char** encoding );
    void ( *register_encoding_callback )( void* handle, LogSquirlEncodingCallbackFn callback,
                                          void* user_data );
    int ( *register_menu_action_ex )( void* handle, const char* menu_path, const char* label,
                                      void ( *callback )( void* user_data ), void* user_data );
    void ( *unregister_menu_action )( void* handle, int id );
    int ( *get_log_line_count )( void* handle, const char* file_path, uint64_t* count );
    int ( *get_log_lines )( void* handle, const char* file_path, uint64_t first_line,
                            size_t max_lines, const char** text, size_t* length,
                            size_t* line_count );
    void ( *register_log_lines_callback )( void* handle, LogSquirlLogLinesCallbackFn callback,
                                           void* user_data );
    int ( *register_context_menu_action )( void* handle, const char* menu_path, const char* label,
                                           LogSquirlContextMenuCallbackFn callback,
                                           void* user_data );
    void ( *unregister_context_menu_action )( void* handle, int id );
    int ( *add_marks )( void* handle, const char* file_path, const uint64_t* lines, size_t count );
    int ( *remove_marks )( void* handle, const char* file_path, const uint64_t* lines,
                           size_t count );
    int ( *get_marks )( void* handle, const char* file_path, const uint64_t** lines,
                        size_t* count );
    int ( *get_log_format_fields )( void* handle, const char* file_path, const char** format_name,
                                    const char* const** field_names, size_t* field_count );
    int ( *get_log_line_fields )( void* handle, const char* file_path, uint64_t first_line,
                                  size_t max_lines, const char* const** values,
                                  const size_t** lengths, const int** parsed, size_t* field_count,
                                  size_t* line_count );
} LogSquirlHostApi;
```

Functions added to the table later are listed below *Added later*; before you
call one, check that the running host offers it (see
[A Growing API](#a-growing-api)).

The `handle` is an opaque pointer — always pass the same `handle` value that
was given to your `init` function. **Never** dereference or interpret it.

The functions follow one set of rules for strings and registrations (see
[ADR 0022](adr/0022-a-registration-a-plugin-removes-returns-an-id-and-a-returned-string-lives-until-the-same-call.md)):

- **Strings you pass** are copied before the call returns; free them after.
- **Strings the host returns** (`get_config_dir`, `get_active_file_path`,
  `get_selected_log_lines`, `get_log_file_encoding`) are the host's: each is
  valid until your plugin calls the same function again or is shut down,
  whatever other plugins call meanwhile. The text of `get_log_lines` is valid
  until your plugin calls it again *on the same thread*, so each thread keeps
  its own; so are the names of `get_log_format_fields` and the arrays and
  values of `get_log_line_fields`, each until your plugin calls that same
  function again on the same thread. The array of `get_marks` is valid until
  your plugin calls it again or is shut down, like those strings. Copy what
  you keep longer.
- **Strings the host passes to a callback** are valid during the call only.
- **Registrations**: each `register_*` function says whether a second call
  replaces the first registration or adds another, and how a registration is
  removed; see the table below. Whatever your plugin still has registered is
  removed when it is unloaded. A registration that returns an id returns one
  greater than 0, never handed out twice while the plugin is loaded, or a
  negative value when nothing was registered; its `unregister_*` function does
  nothing for an id it does not know, or no longer knows.

| Function                        | A second call                                    | Removed by                         |
|---------------------------------|--------------------------------------------------|------------------------------------|
| `register_status_widget`        | adds another widget; the same widget again does nothing | `unregister_status_widget`  |
| `register_sidebar_tab`          | adds another tab; the same widget again does nothing    | `unregister_sidebar_tab`    |
| `register_footer_widget`        | adds another widget; the same widget again does nothing | `unregister_footer_widget`  |
| `register_menu_action`          | adds another item, but none of a menu path and label the plugin already has | unloading the plugin only |
| `register_menu_action_ex`       | adds another item with an id of its own; a menu path and label the plugin already has fails | `unregister_menu_action` |
| `register_context_menu_action`  | adds another item with an id of its own, whatever its label | `unregister_context_menu_action` |
| `register_active_file_callback` | replaces the callback                            | a `NULL` callback                  |
| `register_encoding_callback`    | replaces the callback                            | a `NULL` callback                  |
| `register_log_lines_callback`   | replaces the callback                            | a `NULL` callback                  |

### `LogSquirlLogLevel`

The levels for `log_message`, mirroring spdlog:

```c
typedef enum {
    LOGSQUIRL_LOG_TRACE = 0,
    LOGSQUIRL_LOG_DEBUG = 1,
    LOGSQUIRL_LOG_INFO = 2,
    LOGSQUIRL_LOG_WARNING = 3,
    LOGSQUIRL_LOG_ERROR = 4,
    LOGSQUIRL_LOG_CRITICAL = 5
} LogSquirlLogLevel;
```

### A Growing API

LogSquirl adds host functions without changing `LOGSQUIRL_PLUGIN_API_VERSION`
(see [ADR 0017](adr/0017-the-plugin-api-grows-by-appending-host-functions-and-a-plugin-learns-them-from-the-table-size.md)):

- A new function is appended to the end of `LogSquirlHostApi`. Nothing in the
  table is reordered, removed or changed, and only function pointers are
  appended, so a plugin built against an older header reads the table as it
  always did, and keeps loading and running unchanged.
- A plugin built against a newer header learns which functions the running
  host offers from the size of the host's table: the host passes it to
  `logsquirl_plugin_init_ex`. A host older than that entry point calls
  `logsquirl_plugin_init` instead; its table is
  `LOGSQUIRL_HOST_API_BASE_SIZE` bytes long.
- `LOGSQUIRL_HOST_API_HAS` says whether a table of that size holds a whole
  function pointer. Never call or read a function it does not report: an
  older host's table ends before it.

```c
#define LOGSQUIRL_HOST_API_BASE_SIZE offsetof( LogSquirlHostApi, open_regex_lab )

#ifdef __cplusplus
#define LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) sizeof( LogSquirlHostApi::member )
#else
#define LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) sizeof( ( (LogSquirlHostApi*)0 )->member )
#endif

#define LOGSQUIRL_HOST_API_HAS( size, member )                                                     \
    ( offsetof( LogSquirlHostApi, member ) + LOGSQUIRL_HOST_API_MEMBER_SIZE( member ) <= ( size ) )
```

Keep the pointer the host passes and read the table through it. Never copy
`*api` into a `LogSquirlHostApi` of your own: the copy reads the whole struct
your header declares, past the end of an older host's table. For the same
reason, never read a member `LOGSQUIRL_HOST_API_HAS` does not report, not even
to compare it with `NULL`.

The example plugin shows the pattern: `logsquirl_plugin_init` forwards to
`logsquirl_plugin_init_ex` with `LOGSQUIRL_HOST_API_BASE_SIZE`, and
`logsquirl_plugin_init_ex` offers *Test Pattern* only when
`LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab )`. Keep the size, or what it
tells, for later calls.

| Function                         | Added in        |
|----------------------------------|-----------------|
| `open_regex_lab`                 | LogSquirl 26.11 |
| `go_to_log_line`                 | LogSquirl 26.11 |
| `get_selected_log_lines`         | LogSquirl 26.11 |
| `get_log_file_encoding`          | LogSquirl 26.12 |
| `register_encoding_callback`     | LogSquirl 26.12 |
| `register_menu_action_ex`        | LogSquirl 26.12 |
| `unregister_menu_action`         | LogSquirl 26.12 |
| `get_log_line_count`             | LogSquirl 26.12 |
| `get_log_lines`                  | LogSquirl 26.12 |
| `register_log_lines_callback`    | LogSquirl 26.12 |
| `register_context_menu_action`   | LogSquirl 26.12 |
| `unregister_context_menu_action` | LogSquirl 26.12 |
| `add_marks`                      | LogSquirl 26.12 |
| `remove_marks`                   | LogSquirl 26.12 |
| `get_marks`                      | LogSquirl 26.12 |
| `get_log_format_fields`          | LogSquirl 26.12 |
| `get_log_line_fields`            | LogSquirl 26.12 |

### Regex Lab Types

The flag, the result and the callback of `open_regex_lab`:

```c
#define LOGSQUIRL_REGEX_LAB_MATCH_CASE 0x1

typedef enum {
    LOGSQUIRL_REGEX_LAB_CANCELLED = 0,
    LOGSQUIRL_REGEX_LAB_APPLIED = 1
} LogSquirlRegexLabResult;

typedef void ( *LogSquirlRegexLabCallbackFn )( void* user_data, int result, const char* pattern,
                                               int flags );
```

### Log Lines Types

The result of `go_to_log_line`, `get_selected_log_lines`,
`get_log_file_encoding`, `get_log_line_count`, `get_log_lines`, `add_marks`,
`remove_marks`, `get_marks`, `get_log_format_fields` and
`get_log_line_fields`, the bounds of the text `get_selected_log_lines` and
`get_log_lines` return and of the Log Lines and values `get_log_line_fields`
returns, the most line numbers one call of `add_marks` or `remove_marks`
takes, and the change and the callback of `register_log_lines_callback`.
`LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT` came with LogSquirl 26.12, which an older
host never returns:

```c
typedef enum {
    LOGSQUIRL_LOG_LINES_OK = 0,
    LOGSQUIRL_LOG_LINES_TRUNCATED = 1,
    LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD = -1,
    LOGSQUIRL_LOG_LINES_NO_LOG_FILE = -2,
    LOGSQUIRL_LOG_LINES_OUT_OF_RANGE = -3,
    LOGSQUIRL_LOG_LINES_NO_SELECTION = -4,
    LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT = -5,
    LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT = -6
} LogSquirlLogLinesResult;

#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES 1000
#define LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES 1048576

#define LOGSQUIRL_MARKS_MAX_LINES 10000

typedef enum {
    LOGSQUIRL_LOG_LINES_APPENDED = 0,
    LOGSQUIRL_LOG_LINES_REPLACED = 1
} LogSquirlLogLinesChange;

typedef void ( *LogSquirlLogLinesCallbackFn )( void* user_data, const char* file_path,
                                               uint64_t line_count, int change );
```

### Encoding Types

The callback of `register_encoding_callback`:

```c
typedef void ( *LogSquirlEncodingCallbackFn )( void* user_data, const char* file_path,
                                               const char* encoding );
```

### Context Menu Types

The view, the context and the callback of `register_context_menu_action`
(added in LogSquirl 26.12). The host owns the context and everything it
points to: it is valid during the call only, so keep no pointer to it, and
never copy the struct. It grows by appending members, as
`LogSquirlConvertControl` does: check
`LOGSQUIRL_CONTEXT_MENU_CONTEXT_HAS( context, member )` before reading a
member added later (the members below are always there).

```c
typedef enum {
    LOGSQUIRL_LOG_VIEW_TEXT = 0,    /* the Text View     */
    LOGSQUIRL_LOG_VIEW_TABLE = 1,   /* the Table View    */
    LOGSQUIRL_LOG_VIEW_FILTERED = 2 /* the Filtered View */
} LogSquirlLogView;

typedef struct LogSquirlContextMenuContext {
    size_t size;           /* sizeof( LogSquirlContextMenuContext ) in the host's build */
    const char* file_path; /* the Log File, as get_active_file_path names it */
    int view;              /* one of LogSquirlLogView */
    /* the selected Log Lines, numbered from 1, in the order of the Log File */
    const uint64_t* line_numbers;
    size_t line_number_count;
    uint64_t selected_line_count; /* how many were selected, those not listed too */
    /* the text selected within one Log Line or cell; NULL for whole Log Lines */
    const char* selected_text;
    size_t selected_text_length; /* in bytes, without the NUL */
} LogSquirlContextMenuContext;

#define LOGSQUIRL_CONTEXT_MENU_CONTEXT_HAS( context, member )                                      \
    ( offsetof( LogSquirlContextMenuContext, member )                                              \
          + sizeof( ( (const LogSquirlContextMenuContext*)0 )->member )                            \
      <= ( context )->size )

typedef void ( *LogSquirlContextMenuCallbackFn )( void* user_data,
                                                  const LogSquirlContextMenuContext* context );
```

---

## Host API Usage Guide

### Logging

```c
api->log_message( handle, LOGSQUIRL_LOG_INFO, "Processing started" );
```

Messages appear in LogSquirl's log prefixed with `[plugin]`. Trace goes to the
debug level and critical to the error level.

### Configuration Directory

```c
const char* dir = api->get_config_dir( handle );
/* e.g. ~/.local/share/logsquirl/plugin_config/com.example.my-plugin */
```

The directory is `plugin_config/<plugin id>` next to the user plugin directory,
beside the executable in a portable LogSquirl, and is created before `init` is
called. The host owns the returned string: it is valid until your plugin calls
`get_config_dir` again or is shut down. Use it to persist any plugin-specific
settings.

### Data Source Streaming

A data source plugin is listed in the `Sources` menu. Choosing it starts a
stream that opens as a new tab; the lines the plugin pushes go into that tab:

```c
/* Push a single line */
api->push_line( handle, line_data, line_length );

/* Push multiple lines at once for efficiency */
const char*  lines[]  = { line1, line2, line3 };
const size_t lens[]   = { len1,  len2,  len3  };
api->push_lines( handle, lines, lens, 3 );

/* Signal end of stream */
api->signal_eos( handle );

/* Report an error; it is written to LogSquirl's log */
api->signal_error( handle, "Device disconnected" );
```

Lines pushed while no stream of the plugin is running are dropped. A plugin
has one stream at a time: while it runs, choosing the plugin again in the
`Sources` menu does not start another.

Call `signal_eos`, from any thread, when the source has nothing more to give:
the process exited, the device went away, the file was read to its end. The
stream takes no more lines from then on, and the lines pushed after
`signal_eos` are dropped. Once the UI thread's event loop turns, the host ends
the stream as it ends any other: it calls `logsquirl_datasource_stop`, if the
plugin exports it, and counts the data source as stopped. Its tab stays open
and keeps showing what was pushed: the stream's file stays until the plugin is
disabled or unloaded, or LogSquirl closes. The user can start the data source
again from the `Sources` menu, which opens a new tab with a stream of its own.

A plugin learns when its stream starts and stops from two optional exports
(added in LogSquirl 26.12, see
[ADR 0023](adr/0023-a-data-source-is-stopped-once-for-every-start-it-let-run-before-its-stream-ends.md)),
both called on the UI thread with a
[`LogSquirlDataSourceContext`](#logsquirldatasourcecontext) that is valid
during the call only:

- `logsquirl_datasource_start` is called when the user starts the data
  source, before the stream counts as started. Open the device, socket or
  process here; the plugin may show a modal dialog of its own to ask which.
  Lines pushed from inside the call already reach the stream. Return
  `LOGSQUIRL_DATASOURCE_STARTED` to let the stream run: it opens in a tab,
  and lines pushed from then on reach it. Return
  `LOGSQUIRL_DATASOURCE_CANCELLED` when the user cancelled: nothing opens and
  the user is told nothing. Return `LOGSQUIRL_DATASOURCE_FAILED`, or any other
  non-zero value, when it failed: nothing opens, and the user is told that the
  data source did not start, with the value; say why with
  `show_notification` first. What was pushed is discarded when the stream
  does not start.
- `logsquirl_datasource_stop` is called exactly once for every start that
  returned `LOGSQUIRL_DATASOURCE_STARTED`, however the stream ends: the user
  stops it, the plugin signals end-of-stream itself, the plugin is disabled
  or unloaded, or LogSquirl quits. It is called before the host signals
  end-of-stream, so push the last lines here and close the source. A push
  that returns after `stop` has returned may be dropped. A plugin that
  signalled end-of-stream itself is stopped once the event loop turns, after
  its own end-of-stream: what it pushes in `stop` then is dropped, and `stop`
  only closes the source.

A start the plugin refused is never stopped. A plugin without
`logsquirl_datasource_start` counts as having let every stream run, and a
plugin without `logsquirl_datasource_stop` is simply not told.

```cpp
/* A data source that reads a device from a thread of its own. */
std::thread reader;
std::atomic<bool> reading{ false };

LOGSQUIRL_PLUGIN_EXPORT int logsquirl_datasource_start( const LogSquirlDataSourceContext* )
{
    if ( !askWhichDevice() ) {
        return LOGSQUIRL_DATASOURCE_CANCELLED; /* the user closed the dialog */
    }
    if ( !openDevice() ) {
        host->show_notification( hostHandle, "The device is busy" );
        return LOGSQUIRL_DATASOURCE_FAILED;
    }
    reading = true;
    reader = std::thread( [] {
        std::string line;
        while ( reading && readLine( line ) ) {
            host->push_line( hostHandle, line.data(), line.size() );
        }
    } );
    return LOGSQUIRL_DATASOURCE_STARTED;
}

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_datasource_stop( const LogSquirlDataSourceContext* )
{
    reading = false;
    reader.join(); /* its last lines still reach the stream */
    closeDevice();
}
```

A stream that ends because the plugin is disabled or unloaded from the
Plugin Management dialog, or because LogSquirl closes, goes in this order: the
host calls `logsquirl_datasource_stop` first, then `logsquirl_plugin_shutdown`
and waits for it to return, then signals end-of-stream and frees the stream,
and with it the files of this and every earlier stream of the plugin.
A thread of the plugin's may push until `shutdown` returns; what it pushes
meanwhile reaches a live stream. `shutdown` must stop and join such a thread
before it returns, since nothing the plugin calls after that is answered. A
stream the host ends on its own, with the plugin still loaded, is stopped in
the plugin first and drops what the plugin pushes once `stop` has returned;
its file stays for its tab until the plugin is unloaded.
A host older than LogSquirl 26.12 knows neither export and never calls them.
The hosts that call them pass a host API table that holds
`unregister_menu_action`, which an older host's does not: a plugin that also
runs on an older host opens its source in `logsquirl_plugin_init_ex` when
`LOGSQUIRL_HOST_API_HAS( api_size, unregister_menu_action )` is false, and in
`logsquirl_plugin_init`, and closes it in `shutdown`.

### Converters

A converter exports `logsquirl_converter_get_extensions` and
`logsquirl_converter_convert`, `logsquirl_converter_convert_ex`, or both.
Any plugin that exports them converts, whatever `type` its manifest names:
`type` names the plugin's main kind for the Plugins menu and the plugin
dialog, and a `ui` plugin claims its extensions all the same.
`logsquirl_converter_get_extensions` returns the file extensions it handles,
separated by semicolons, e.g. `".har;.pcap"`. When a Log File with one of
these extensions is opened -- from the File menu, by drag and drop, from the
recent files, the command line or `open_file` alike -- the host calls the
convert function with the file's path and the path of a temporary file to
write plain text into, and shows that text as a Transient Log File if the
converter returns 0. Both paths are absolute and UTF-8. A non-zero return
removes the output, and the user is told that the plugin could not convert
the file.

The call runs on a worker thread of the host's, never on the UI thread: the
window stays in use, with a progress dialog beside it that has a Cancel
button. The host calls a plugin's converter one file at a time, never
re-entrantly -- a second file for the same plugin waits its turn -- while
converters of different plugins run at the same time. Keep the converter's
state in locals, and call the host API from inside under its thread rules
(`open_file` is fine, the UI-thread-only functions are not).

`logsquirl_converter_convert_ex` gets a `LogSquirlConvertControl` as its third
argument: call `report_progress` with how far the conversion is -- the
dialog shows the share done; with a total of 0 it shows the conversion as
busy -- and poll `is_cancelled` now and then; once it is non-zero, return as
soon as possible with any code, since what was written is discarded anyway.
Call both from the thread `convert_ex` runs on only, never after it returned.
A plugin with the plain `convert` only is shown as busy without a share done,
and a cancellation discards its result once it returns.

```cpp
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert_ex( const char* input_path,
                                                            const char* output_path,
                                                            const LogSquirlConvertControl* control )
{
    std::ifstream in( input_path );
    std::ofstream out( output_path );
    const uint64_t total = fileSize( input_path ); /* 0 when unknown */
    uint64_t done = 0;
    std::string record;
    while ( readRecord( in, record ) ) {
        if ( control->is_cancelled( control ) ) {
            return 2; /* discarded by the host */
        }
        out << decode( record ) << '\n';
        done += record.size();
        control->report_progress( control, done, total );
    }
    return out ? 0 : 1;
}

/* For a host older than convert_ex: no progress, no Cancel. */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char* input_path,
                                                         const char* output_path )
{
    static const LogSquirlConvertControl none
        = { sizeof( LogSquirlConvertControl ), nullptr, &noProgress, &neverCancelled };
    return logsquirl_converter_convert_ex( input_path, output_path, &none );
}
```

#### Claiming a File by Its Content

A format whose files have no extension of their own -- a capture saved as
`.log`, `.bin`, `.dat` or without any -- is recognised by its first bytes
instead. A converter that also exports `logsquirl_converter_claims` (added in
LogSquirl 26.12) is asked about such a file: it gets the file's absolute UTF-8
path and its first bytes, and returns non-zero when it converts the file. The
host reads the bytes itself and hands over this many, or fewer when the file
is shorter (none for an empty file; `head` is never `NULL`):

```c
#define LOGSQUIRL_CONVERTER_CLAIMS_HEAD_LENGTH 4096
```

`head` is valid during the call only. A file the host cannot read is claimed
by no plugin, and no plugin is asked about it. When a Log File is opened, it
goes to the first converter in this order that takes it:

1. A converter whose `logsquirl_converter_get_extensions` lists the file's
   extension. When several do, the one with the smallest plugin `id` converts
   it, and no plugin is asked about its content.
2. Else the converters that export `logsquirl_converter_claims`, asked one
   after the other in the order of their plugin `id`s. The first that returns
   non-zero converts the file, and the converters after it are not asked.
3. Else no converter: the file is decompressed if it is compressed or an
   archive, and opened as text.

The bytes are the file's as it is on disk, before it is decompressed: a
converter may claim a compressed file, which it then converts. A file taken
out of an archive or decompressed is offered again like any other file. The
Conversion runs as it does for an extension, with progress and Cancel, and the
Log Format of what it wrote is recognised as for any other Log File. A
converter that claims by its content only returns an empty string from
`logsquirl_converter_get_extensions`. What LogSquirl writes itself -- what a
Conversion wrote, a stream, a merge, the text pasted from the clipboard, a
download -- is not offered.

The check runs on a worker thread of the host's, never on the UI thread, and
may run for several files at the same time, also while a conversion of the
plugin runs. The file opens only once the check has returned, so return
quickly: compare a few bytes, do not parse the file. The host never calls it
after `logsquirl_plugin_shutdown`: an unload waits for a call under way. A
host older than 26.12 never calls it, and converts by the extensions only.

```cpp
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_claims( const char* file_path,
                                                        const unsigned char* head,
                                                        size_t head_length )
{
    (void)file_path; /* judged by the content alone */
    static const unsigned char magic[] = { 'M', 'Y', 'F', 'M', 'T', '1' };
    return head_length >= sizeof( magic ) && std::memcmp( head, magic, sizeof( magic ) ) == 0;
}
```

### UI Integration

```cpp
/* Show a notification toast */
api->show_notification( handle, "Import complete" );

/* Ask the host to open a file */
api->open_file( handle, "/tmp/converted.log", 0 /* follow = false */ );

/* Add an item to the Plugins menu */
api->register_menu_action( handle, "Plugins", "Say Hello", &sayHello, nullptr );
```

`open_file` may be called from any thread: the file opens in the most recently
active LogSquirl window on its UI thread, at once when called there, else once
that window's event loop turns, and the call returns at once. A call queued by
a plugin that is unloaded before then is dropped.

`menu_path` names the submenus of the `Plugins` menu the item goes into,
separated by `/`: `"My Plugin/Sub"` puts `label` into
`Plugins` → `My Plugin` → `Sub`. An empty path, or `"Plugins"`, puts the item
directly into the `Plugins` menu; a path starting with `Plugins/` is read
without that first segment. The last segment is a submenu too, the item's own
text is always `label`. Items and submenus go above `Plugin Management...`,
and plugins naming the same path share its submenus. When the plugin is
unloaded, its items go away, and so does every submenu nothing is left in.
A plugin's items in one submenu have different labels: a second
`register_menu_action` of a menu path and label the plugin already has adds
nothing. Paths that name the same submenu, such as `"Tools"` and
`"Plugins/Tools/"`, count as the same; an item of the same label in another
submenu, such as `"Tools"`/`"Export"` and `"Report"`/`"Export"`, is an item
of its own.

An item of `register_menu_action` stays until the plugin is unloaded. To
remove one earlier, register it with `register_menu_action_ex` (added in
LogSquirl 26.12), which takes the same arguments and returns the item's id:

```c
if ( LOGSQUIRL_HOST_API_HAS( api_size, register_menu_action_ex )
     && LOGSQUIRL_HOST_API_HAS( api_size, unregister_menu_action ) ) {
    int id = api->register_menu_action_ex( handle, "My Plugin", "Connect", &connect, userData );
    /* ... later, from any thread, from the item's own callback too */
    api->unregister_menu_action( handle, id );
}
```

The id is greater than 0 and unique within the plugin while it is loaded; it
comes back at once, also when called on another thread, before the item
appears on the UI thread. A negative value means no item was added: a `NULL`
or empty `label`, a `NULL` callback, or a menu path and label the plugin
already has an item of. `unregister_menu_action` removes the item from every window, and a
submenu the host created for it once nothing is left in it. Once it returns,
the item's callback is not called again: a trigger still queued is dropped.
An id the plugin was not given, or one already removed, does nothing. To
relabel an item, remove it and register it again.

`register_status_widget`, `register_sidebar_tab` and `register_footer_widget`
take a `QWidget*` cast to `void*`: the plugin creates and owns the widget and
the host parents it. Unregister the widget again before you delete it, at the
latest in `shutdown`. Such a plugin links against the same Qt 6 LogSquirl
uses and names it in its manifest, see [Plugins That Use Qt](#plugins-that-use-qt).

### Context Menu

`register_context_menu_action` (added in LogSquirl 26.12, see
[ADR 0025](adr/0025-a-context-menu-item-is-called-from-the-event-loop-with-what-was-selected-as-the-menu-opened.md))
adds an item to the context menu of the Log Lines: that of the Text View, the
Table View and the Filtered View, in every window and every tab, those opened
later included. The items of all plugins sit below a separator at the end of
the menu, in the order they were registered; without any, there is no
separator. `menu_path` makes submenus of that section as it does for the
Plugins menu (`"My Plugin/Decode"`), and an empty path, or `NULL`, puts the
item directly into the section.

```c
/* Called on the UI thread once the user chose the item and the menu closed. */
void decode( void* user_data, const LogSquirlContextMenuContext* context )
{
    if ( context->selected_text != NULL ) {
        /* a word or an id selected within one Log Line or table cell */
    }
    for ( size_t i = 0; i < context->line_number_count; ++i ) {
        /* read context->line_numbers[ i ] with get_log_lines( handle,
           context->file_path, context->line_numbers[ i ], 1, ... ) */
    }
}

if ( LOGSQUIRL_HOST_API_HAS( api_size, register_context_menu_action )
     && LOGSQUIRL_HOST_API_HAS( api_size, unregister_context_menu_action ) ) {
    int id = api->register_context_menu_action( handle, "My Plugin", "Decode", &decode, userData );
    /* ... later, from any thread, from the item's own callback too */
    api->unregister_context_menu_action( handle, id );
}
```

An item is always shown and enabled: LogSquirl does not call the plugin as
the menu opens, so it cannot hide an item that does not fit the selection.
When the user chooses it, the callback is called on the UI thread once the
menu has closed, with a `LogSquirlContextMenuContext` of what was selected as
the menu opened (see [Context Menu Types](#context-menu-types)):

- `file_path` is the Log File, as `get_active_file_path` names it, and `view`
  says which view the menu was opened in.
- `line_numbers` lists the selected Log Lines, numbered from 1 as the views
  number them and in the order of the Log File; in the Filtered View too, they
  are the numbers of the Log File. A right-click on a Log Line outside the
  selection selects that Log Line first. At most
  `LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES` are listed, the first ones;
  `selected_line_count` says how many were selected.
- `selected_text` is the text selected within one Log Line or one table cell,
  UTF-8, with its length in bytes; `NULL` when whole Log Lines are selected.
  Read the text of whole Log Lines with `get_log_lines`.

The callback is not called when the view the menu was opened in, or its tab,
was closed meanwhile, nor once `unregister_context_menu_action` has returned
for it, a choice still queued included. Each call of
`register_context_menu_action` adds an item of its own and returns its id,
greater than 0; a negative value means no item was added: a `NULL` or empty
`label` or a `NULL` callback. An id the plugin was not given, or one already
removed, does nothing.

### Active File

```c
/* The path of the Log File in the focused tab, or "" */
const char* path = api->get_active_file_path( handle );

/* Be told whenever the focused Log File changes */
api->register_active_file_callback( handle, &onActiveFile, userData );
```

The host owns the string `get_active_file_path` returns: it is valid until
your plugin calls `get_active_file_path` again or is shut down, whatever other
plugins ask meanwhile. Call it on the UI thread only: called on another
thread, it touches no window and returns an empty string, as without a file.

The active-file callback is called on the UI thread with the new path, valid
during the call only. A plugin has one such callback: registering another
replaces it, and `NULL` removes it.

### Regex Lab

A plugin that works with regular expressions lets the user test one in
LogSquirl's Regex Lab instead of building a tester of its own:

```c
if ( LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab ) ) {
    api->open_regex_lab( handle, "ERROR (\\d+)", LOGSQUIRL_REGEX_LAB_MATCH_CASE,
                         &patternTested, userData );
}
```

The Lab opens over the LogSquirl window, on the Log Lines of the tab in front
or on pasted text, and reads the pattern as a Perl-compatible regular
expression, as Qt's `QRegularExpression` does. The user may change the pattern
and *Match case*, then applies or cancels it. `open_regex_lab` returns at once:
0 when the Lab opened, non-zero when it did not (called off the UI thread,
no LogSquirl window, a `NULL` callback), and then the callback is never called.
A `NULL` pattern is an empty one; `flags` is `LOGSQUIRL_REGEX_LAB_MATCH_CASE`
or 0 to ignore case. Each call opens a Lab of its own.

The callback is called once, on the UI thread:

- with `LOGSQUIRL_REGEX_LAB_APPLIED`, the applied pattern in UTF-8 (valid
  during the call only, copy it) and `LOGSQUIRL_REGEX_LAB_MATCH_CASE` in
  `flags` when it matches case;
- with `LOGSQUIRL_REGEX_LAB_CANCELLED`, a `NULL` pattern and 0 flags, when the
  user cancels or closes the Lab, or the window it belongs to closes.

Once your plugin is shut down, the callback is never called: a Lab it opened
and that is still open is closed without an answer. Call `open_regex_lab` on
the UI thread only: the thread `init`, menu actions and the callbacks run on.

### Log Lines

A plugin shows the user a Log Line of the tab in front, and reads the Log
Lines the user selected there:

```c
if ( LOGSQUIRL_HOST_API_HAS( api_size, go_to_log_line ) ) {
    /* Line numbers count from 1, as LogSquirl shows them */
    int result = api->go_to_log_line( handle, 42 );
}

if ( LOGSQUIRL_HOST_API_HAS( api_size, get_selected_log_lines ) ) {
    const char* text = NULL;
    size_t length = 0;
    size_t lineCount = 0;
    int result = api->get_selected_log_lines( handle, &text, &length, &lineCount );
    if ( result >= 0 ) {
        /* text: lineCount Log Lines, joined by '\n' */
    }
}
```

`go_to_log_line` does what *Go to line* does: it selects the Log Line and
scrolls it into view in the Presentation shown -- the Text View or the Table
View -- and the Filtered View selects the nearest line it shows. The
`line_number` is the one LogSquirl shows: the first Log Line is 1. It returns
`LOGSQUIRL_LOG_LINES_OK`, or `LOGSQUIRL_LOG_LINES_OUT_OF_RANGE` for 0 or a
number past the last line indexed so far, and changes nothing then. A `NULL`
handle is `LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT`.

`get_selected_log_lines` returns the Log Lines selected in the view the user
was last in -- the Filtered View or the Presentation shown -- in the order of
the Log File. A selection within a Log Line returns that whole Log Line. The
text is UTF-8, the lines joined by `'\n'` with none after the last, and
NUL-terminated; `length` is its size in bytes without the NUL, `line_count` how
many Log Lines it holds, and either may be `NULL`. It holds whole Log Lines, at
most `LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES` of them and at most
`LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES` bytes, the first ones selected; a
first Log Line longer than that is cut at a character. When it holds fewer
than are selected, the result is `LOGSQUIRL_LOG_LINES_TRUNCATED` instead of
`LOGSQUIRL_LOG_LINES_OK`. The host owns the text: it is valid until your plugin
calls `get_selected_log_lines` again or is shut down, so copy what you keep.
Without a selection the result is `LOGSQUIRL_LOG_LINES_NO_SELECTION`, and
`text` is `NULL`.

Both work on the tab in front of the most recently active LogSquirl window,
and return `LOGSQUIRL_LOG_LINES_NO_LOG_FILE` when it shows no Log File.
Call them on the UI thread only: called on another thread, they do nothing and
return `LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD`.

### Encoding

A plugin that reads a Log File itself -- to find values in it, say -- reads it
in the text encoding LogSquirl reads it in, so that its Log Lines, and their
numbers, are those the views show:

```c
if ( LOGSQUIRL_HOST_API_HAS( api_size, get_log_file_encoding ) ) {
    const char* encoding = NULL;
    /* NULL or "" for the tab in front, or a path as get_active_file_path names it */
    int result = api->get_log_file_encoding( handle, NULL, &encoding );
    if ( result == LOGSQUIRL_LOG_LINES_OK ) {
        /* encoding: "UTF-8", "UTF-16LE", "windows-1252", ... */
    }
}

if ( LOGSQUIRL_HOST_API_HAS( api_size, register_encoding_callback ) ) {
    /* Be told whenever a Log File is read in another encoding */
    api->register_encoding_callback( handle, &onEncodingChanged, userData );
}
```

`get_log_file_encoding` returns the encoding LogSquirl reads the Log File in:
the one the user chose for its tab in the *Encoding* menu, else the one
detected -- from a byte order mark, or guessed from the bytes, as for a UTF-16
file without one -- else the locale's. The name is the encoding's IANA name,
such as `UTF-8`, `UTF-16LE`, `UTF-16BE`, `ISO-8859-1` or `windows-1252`. With
`file_path` `NULL` or `""` it is the Log File of the tab in front of the most
recently active LogSquirl window; with a path, that of the tab showing that
Log File, in any window. It returns `LOGSQUIRL_LOG_LINES_OK` and the name, or
`LOGSQUIRL_LOG_LINES_NO_LOG_FILE` with a `NULL` name when the tab in front
shows no Log File or no tab shows the one named. A `NULL` handle or `encoding`
is `LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT`. The host owns the name: it is valid
until your plugin calls `get_log_file_encoding` again or is shut down. Call it
on the UI thread only: called on another thread, it returns
`LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD`.

The encoding of a Log File changes when the user chooses another one for its
tab, and may change when a load of it detects another one -- the first load
detects it only once it has read the file. The callback of
`register_encoding_callback` is called on the UI thread on every such change,
for the Log File of any tab, with its path and the new encoding's name, both
valid during the call only; a Log File shown in two windows is reported for
each tab whose encoding changes. A plugin has one such callback: registering
another replaces it, and `NULL` removes it. When the user switches tabs, the
active-file callback is called instead; ask for the encoding of the new tab in
front there.

### Reading a Log File

A plugin that analyses a whole Log File reads its Log Lines from LogSquirl
rather than opening the file itself (added in LogSquirl 26.12, see
[ADR 0024](adr/0024-a-plugin-reads-an-open-log-file-by-path-on-any-thread-and-each-thread-keeps-its-text.md)):
it gets them decoded in the encoding the tab reads them in, numbered as the
views number them, and for a converted or decompressed Log File the text
LogSquirl shows rather than the bytes of the original file.

```cpp
/* On a thread of the plugin's own: the UI stays in use meanwhile. */
std::thread analysis( [ path = std::string( pathFromActiveFileCallback ) ] {
    uint64_t count = 0;
    if ( host->get_log_line_count( hostHandle, path.c_str(), &count ) < 0 ) {
        return; /* the tab closed */
    }
    uint64_t next = 1; /* the first Log Line is 1 */
    while ( next <= count ) {
        const char* text = NULL;
        size_t lineCount = 0;
        int result = host->get_log_lines( hostHandle, path.c_str(), next, 1000, &text,
                                          NULL, &lineCount );
        if ( result < 0 ) {
            break; /* LOGSQUIRL_LOG_LINES_NO_LOG_FILE once the tab closed */
        }
        analyse( text ); /* lineCount Log Lines, joined by '\n' */
        next += lineCount;
    }
} );
```

Both functions name the Log File by its path, as `get_active_file_path` and
the active-file callback name it: that of a tab in any window, in front or
not. A path no tab shows gives `LOGSQUIRL_LOG_LINES_NO_LOG_FILE`; a `NULL`
path is `LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT`.

`get_log_line_count` counts the Log Lines LogSquirl has indexed so far, so
fewer while the Log File still loads.

`get_log_lines` returns the text of Log Lines from `first_line` on, in the
format of `get_selected_log_lines`: UTF-8, the lines joined by `'\n'` with none
after the last, and NUL-terminated; `length` and `line_count` may be `NULL`.
It holds whole Log Lines, at most `max_lines` and
`LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES` of them and at most
`LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES` bytes; a first Log Line longer than
that is cut at a character. When it holds fewer Log Lines than `max_lines` and
the Log File has, the result is `LOGSQUIRL_LOG_LINES_TRUNCATED`: go on from
`first_line + line_count`. A `first_line` of 0, or past the last Log Line
indexed so far, is `LOGSQUIRL_LOG_LINES_OUT_OF_RANGE`; a `max_lines` of 0 reads
an empty text.

Both may be called on any thread and never wait for the UI thread, so a
thread of the plugin's may read while the UI thread is busy, and a modal
dialog of the plugin's own may wait for that thread. The host keeps the Log
File's data for the duration of the call: when the user closes the tab before
or during a call, the call returns `LOGSQUIRL_LOG_LINES_NO_LOG_FILE`, and
nothing it read is handed back. The host owns the text: it is valid until your
plugin calls `get_log_lines` again *on the same thread*, or is shut down, so
two threads of one plugin each keep their own text. Stop and join your threads
in `shutdown`.

The callback of `register_log_lines_callback` is called on the UI thread
whenever the Log Lines of a tab's Log File change, in front or not, with its
path, its Log Line count after the change and how it changed:

- `LOGSQUIRL_LOG_LINES_APPENDED`: Log Lines were added at the end -- the file
  grew, followed or not. What you read before stays as it was; read on from
  where you stopped. The last Log Line you read may have grown longer.
- `LOGSQUIRL_LOG_LINES_REPLACED`: the Log File was loaded anew -- reloaded,
  truncated, or its first load finished, which settles its encoding -- or it
  reads differently: the user chose another encoding, the way LogSquirl
  decodes its text changed, or another Log Format applies to it, or none, so
  its fields differ (see [Fields of Log Lines](#fields-of-log-lines)). What
  you read before may differ; read it again.

Calls may be coalesced, one standing for several changes, so take
`line_count` from the call rather than adding up. The path is valid during
the call only. A plugin has one such callback: registering
another replaces it, and `NULL` removes it.

### Fields of Log Lines

A plugin that analyses structured Log Files -- statistics per level, per
component, per request -- reads the fields of their Log Lines as the Table
View shows them, rather than parsing the text itself (added in LogSquirl
26.12, see
[ADR 0027](adr/0027-a-plugin-reads-the-fields-of-log-lines-as-the-table-view-shows-them.md)):

```cpp
/* On a thread of the plugin's own, for the Log File at path. */
const char* format = NULL;
const char* const* names = NULL;
size_t nameCount = 0;
if ( host->get_log_format_fields( hostHandle, path.c_str(), &format, &names, &nameCount ) < 0 ) {
    return; /* LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT: no Log Format applies */
}
int level = -1; /* the column of the field named "level" */
for ( size_t field = 0; field < nameCount; ++field ) {
    if ( std::strcmp( names[ field ], "level" ) == 0 ) {
        level = static_cast<int>( field );
    }
}
std::map<std::string, uint64_t> perLevel;
uint64_t next = 1; /* the first Log Line is 1 */
for ( ;; ) {
    const char* const* values = NULL;
    const size_t* lengths = NULL;
    const int* parsed = NULL;
    size_t fieldCount = 0;
    size_t lineCount = 0;
    int result = host->get_log_line_fields( hostHandle, path.c_str(), next, 1000, &values,
                                            &lengths, &parsed, &fieldCount, &lineCount );
    if ( result < 0 || fieldCount != nameCount ) {
        break; /* past the end, the tab closed, or another Log Format applies */
    }
    for ( size_t line = 0; line < lineCount; ++line ) {
        if ( parsed[ line ] && level >= 0 ) {
            size_t at = line * fieldCount + static_cast<size_t>( level );
            ++perLevel[ std::string( values[ at ], lengths[ at ] ) ];
        }
    }
    next += lineCount;
}
```

Both name the Log File by its path and may be called on any thread, as
`get_log_lines`; they never wait for the UI thread, and a tab that closes
before or during a call gives `LOGSQUIRL_LOG_LINES_NO_LOG_FILE`.

`get_log_format_fields` names the Log Format that applies to the Log File --
its own name, as the key of its definition names it (`"syslog_log"`) -- and
its fields in the order of the Table View's columns, named as their headers
name them. The elapsed time the Table View shows after a timestamp field is no
field, and is not among them. `format_name` may be `NULL`. When no Log Format
applies, the result is `LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT`.

`get_log_line_fields` reads the fields of Log Lines from `first_line` on, each
value as the Table View's cell shows it. The values come row-major:
`field_count` of them per Log Line, the first Log Line's first, so field `j`
of the Log Line `first_line + i` is `values[ i * field_count + j ]`, of
`lengths[ i * field_count + j ]` bytes. A value is UTF-8 and NUL-terminated,
but it may hold any character, a tab, a line feed or a NUL too: read it by its
length. A field a Log Line has no value for is `""`. A Log Line the Log Format
does not parse has no fields, as a Field Term of a Search treats it, though
the Table View shows it whole in its last column: `parsed[ i ]` is 0 for it
and its values are `NULL`, of length 0; `parsed[ i ]` is 1 for a Log Line
parsed. A JSON or logfmt Log Format parses every Log Line: one that is no
JSON object, or holds no key of the Log Format, has every value `""`, as the
Table View shows it. `lengths` and `parsed` may be `NULL`.

It holds whole Log Lines, at most `max_lines` and
`LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES` of them, whose values are at most
`LOGSQUIRL_SELECTED_LOG_LINES_MAX_BYTES` bytes together; the values of a first
Log Line longer than that are cut at a character, and those after the cut are
`""`. When it holds fewer Log Lines than `max_lines` and the Log File has, the
result is `LOGSQUIRL_LOG_LINES_TRUNCATED`: go on from `first_line +
line_count`. A `first_line` of 0, or past the last Log Line indexed so far, is
`LOGSQUIRL_LOG_LINES_OUT_OF_RANGE`; no Log Format is
`LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT`.

The Log Format is the one Format Recognition chose. It changes when the Log
File is recognized anew -- reloaded, truncated, or a Log File too short for a
Log Format grows -- and the callback of `register_log_lines_callback` tells it
with `LOGSQUIRL_LOG_LINES_REPLACED`: read the names and the values again. A
read made while it changes may be under either Log Format; `field_count` says
how many fields the read was under. The host owns the names, the arrays and
the values: each is valid until your plugin calls the same function again *on
the same thread*, or is shut down.

### Marks

An analysis plugin marks what it found -- an error burst, an anomaly, the
start of a request -- and the user walks through the findings with Next Mark
and Previous Mark (added in LogSquirl 26.12, see
[ADR 0026](adr/0026-a-plugin-sets-marks-in-every-tab-of-the-log-file-from-any-thread.md)):

```cpp
/* On the plugin's analysis thread, with the Log Lines it found, numbered from 1. */
std::vector<uint64_t> found = analyse( path );
for ( size_t at = 0; at < found.size(); at += LOGSQUIRL_MARKS_MAX_LINES ) {
    size_t count = std::min<size_t>( found.size() - at, LOGSQUIRL_MARKS_MAX_LINES );
    if ( host->add_marks( hostHandle, path.c_str(), found.data() + at, count ) < 0 ) {
        break; /* LOGSQUIRL_LOG_LINES_NO_LOG_FILE once the tab closed */
    }
}
```

A Mark a plugin sets is an ordinary Mark: the bullet margin and the Filtered
View show it, Next Mark visits it, the Session keeps it, and the user removes
it like any other. LogSquirl does not tell the user's Marks from a plugin's:
`remove_marks` removes either, and `get_marks` reads both.

All three name the Log File by its path, as `get_log_lines` does, and number
its Log Lines as the views do: the first Log Line is 1.

- `add_marks` and `remove_marks` take at most `LOGSQUIRL_MARKS_MAX_LINES`
  numbers per call, in any order; a count above is
  `LOGSQUIRL_LOG_LINES_INVALID_ARGUMENT` and changes nothing. A line of 0, or
  past the last Log Line indexed so far, is skipped, and so is one already
  marked (for `add_marks`) or not marked (for `remove_marks`): none of them
  fails the call.
- They may be called from any thread and never wait for the UI thread: they
  tell at once whether a tab shows the Log File -- else
  `LOGSQUIRL_LOG_LINES_NO_LOG_FILE` -- and the host carries the change out on
  the UI thread, at once when called there, else once its event loop turns,
  in the order the calls of one thread were made. A call still queued when
  your plugin is unloaded is dropped; a tab that closes meanwhile marks
  nothing.
- `get_marks` is called on the UI thread only. It hands back the Marks in the
  order of the Log File, each once, and how many there are: `NULL` and 0 when
  there are none. A Mark set from another thread is read once the host has
  carried that call out. The host owns the array: it is valid until your
  plugin calls `get_marks` again or is shut down.

Each tab keeps Marks of its own, the user's as well. When several tabs show
the same Log File -- in different windows, say -- `add_marks` and
`remove_marks` change the Marks of every one of them, in front or not, and
`get_marks` reads a Log Line as marked when any of them marks it.

LogSquirl does not tell your plugin when the user changes Marks; read them
again with `get_marks` when you need them. Marks do not survive a reload or a
truncation of the Log File, the user's included: the callback of
`register_log_lines_callback` tells of both with
`LOGSQUIRL_LOG_LINES_REPLACED`.

### Configuration Dialog

A plugin may export `logsquirl_plugin_configure`; the host passes it a
`QWidget*` (cast to `void*`) to use as the parent of its dialog:

```cpp
extern "C" LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget )
{
    auto* parent = static_cast<QWidget*>( parent_widget );
    MyConfigDialog dialog( parent );
    dialog.exec();
}
```

The user opens it with the *Configure...* button of the plugin's card in
`Plugins` → `Plugin Management...`. The button is enabled only while the plugin
is loaded and exports `logsquirl_plugin_configure`; each click calls it once.
The parent is the LogSquirl main window, not the Plugin Management dialog, so a
window the plugin keeps open outlives the dialog.

---

## Guidelines

- **Thread safety**: Host API calls are safe from any thread, but for
  `open_regex_lab`, `go_to_log_line`, `get_selected_log_lines`,
  `get_log_file_encoding`, `get_active_file_path` and `get_marks`, which are
  called on the UI thread only. The host
  dispatches UI-modifying calls, `open_file`, `add_marks` and `remove_marks`
  included, to the UI thread internally. `init`, `shutdown`, `configure`, `logsquirl_datasource_start`,
  `logsquirl_datasource_stop` and menu actions run on the UI thread; `logsquirl_converter_convert` and `logsquirl_converter_convert_ex`
  run on a host worker thread, one call per plugin at a time, and the
  `LogSquirlConvertControl` functions are called on that thread only.
  `logsquirl_converter_claims` runs on a host worker thread too, possibly for
  several files at the same time.
- **String lifetime**: All `const char*` strings you pass to host API functions
  are copied immediately — you may free them after the call returns. A string
  the host returns is valid until your plugin calls the same function again
  or is shut down; one passed to a callback, during the call only (see
  [`LogSquirlHostApi`](#logsquirlhostapi)).
- **Error reporting**: Return non-zero from `init` on failure; the host logs
  the code and does not load the plugin.
- **No host headers**: Never include any host header other than
  `logsquirl_plugin_api.h`. This keeps your plugin fully decoupled.
- **Cross-platform**: Use the `LOGSQUIRL_PLUGIN_EXPORT` macro for all exported
  symbols — it handles `__declspec(dllexport)` on Windows and
  `__attribute__((visibility("default")))` on Unix.

---

## FAQ

**Q: Can I use C++ in my plugin?**
A: Yes. The exported functions must use C linkage (`extern "C"`) and C types at
the boundary, but the implementation may be C++. The example uses C++20 for
its designated initializers.

**Q: Do I need to link against Qt?**
A: No. Most plugins don't need Qt at all. Only a plugin that hands widgets to
the host or shows a Qt dialog links against `Qt6::Widgets`, and names the Qt it
is built against in `qt_version`, see [Plugins That Use Qt](#plugins-that-use-qt).

**Q: My plugin needs a background thread. Is that safe?**
A: Yes. Create threads as needed. All host API functions are safe from any
thread, but for `open_regex_lab`, `go_to_log_line`, `get_selected_log_lines`
and `get_active_file_path`, which are called on the UI thread only; `open_file`
from a worker thread opens the file on the UI thread. Call `signal_eos` or
`signal_error` when your thread is done; `signal_eos` ends the stream, and
the host then calls `logsquirl_datasource_stop` on the UI thread. A
converter needs no thread of its
own: the host calls `logsquirl_converter_convert`,
`logsquirl_converter_convert_ex` and `logsquirl_converter_claims` on a worker
thread already.

**Q: How do I handle plugin-specific configuration?**
A: Use `get_config_dir()` to obtain your private directory, then read/write
files there (JSON, INI, etc.). The host does not manage plugin-specific
settings — only the enable/disable state.

**Q: What happens if the API version changes in a future LogSquirl release?**
A: A new host function does not change it: it is appended to the host API
table, and your plugin checks for it with `LOGSQUIRL_HOST_API_HAS` (see
[A Growing API](#a-growing-api)). Only an incompatible change bumps
`LOGSQUIRL_PLUGIN_API_VERSION`. The host checks `api_version` in both
`plugin.json` and the `LogSquirlPluginInfo` that `logsquirl_plugin_get_info`
returns, and rejects a plugin whose version differs at load time with an error
message. Plugin authors then update `api_version` and adapt to the new API.

---

## Plugin Repository

LogSquirl includes a built-in plugin manager (Plugins → Plugin Management...)
that lists installed and available plugins in a unified card-based dialog.

### Registry Architecture

The plugin registry uses a **two-level** design:

1. **Central catalog** (`plugins.json`) — lightweight entries listing each plugin
   with its name, author, description, and a pointer to its own `releases.json`.
2. **Per-plugin releases** (`releases.json`) — hosted in each plugin's repository,
   listing all versions with per-platform download URLs and SHA-256 checksums.

This decouples version updates from the central catalog: plugins publish new
releases without touching the central `plugins.json`.

### Catalog Format (schema v2) — `plugins.json`

```json
{
  "schema_version": 2,
  "plugins": [
    {
      "id": "com.example.myplugin",
      "name": "My Plugin",
      "author": "Author Name",
      "description": "Does useful things",
      "repo_url": "https://github.com/example/myplugin",
      "releases_url": "https://raw.githubusercontent.com/example/myplugin/main/releases.json",
      "icon_url": "https://raw.githubusercontent.com/example/myplugin/main/icon.png"
    }
  ]
}
```

### Per-Plugin Release Manifest — `releases.json`

```json
{
  "plugin_id": "com.example.myplugin",
  "releases": [
    {
      "version": "1.0.0",
      "api_version": 1,
      "release_notes": "Initial release",
      "assets": [
        {
          "platform": "macos",
          "download_url": "https://github.com/example/myplugin/releases/download/v1.0.0/myplugin-1.0.0-macos.zip",
          "sha256": "abc123def456..."
        },
        {
          "platform": "linux",
          "download_url": "https://github.com/example/myplugin/releases/download/v1.0.0/myplugin-1.0.0-linux.zip",
          "sha256": "789abc012def..."
        }
      ]
    }
  ]
}
```

Releases are ordered newest-first. Assets are filtered by the current platform.

### Checksums and URLs

Every asset must carry the SHA-256 of its archive as 64 hex digits: the host
keeps a download only when its hash is that one, and lists an asset with an
empty or malformed `sha256` as not installable, saying so on the plugin's card.
Produce the value from the archive you publish:

```bash
sha256sum myplugin-1.0.0-macos.zip      # Linux
shasum -a 256 myplugin-1.0.0-macos.zip  # macOS
```

`scripts/check_plugin_catalog.py` downloads every archive the public catalog
offers and compares it with the catalog's hash; the maintainer runs it after a
catalog change (#817).

Every URL a catalog entry or a release manifest hands out, `releases_url`,
`icon_url`, `repo_url` and `download_url`, must use `https`. An entry or an
asset with any other scheme, `http` included, is skipped when the catalog is
read and nothing is requested from it.

### Plugin Icons

Plugins can include an icon by adding `"icon": "icon.png"` to their `plugin.json`
manifest. The icon is displayed in the Plugin Management dialog. Remote icons
are fetched from the `icon_url` in the catalog entry.

### Legacy Format (schema v1)

For backward compatibility, the host also supports schema v1 where all version
and platform data is inline in `plugins.json`:

```json
{
  "schema_version": 1,
  "plugins": [
    {
      "id": "com.example.myplugin",
      "name": "My Plugin",
      "version": "1.0.0",
      "description": "Does useful things",
      "author": "Author Name",
      "download_url": "https://example.com/myplugin-1.0.0.zip",
      "sha256": "abc123def456...",
      "platforms": ["macos", "linux", "windows"],
      "api_version": 1
    }
  ]
}
```

The same rules apply: `sha256` is required and `download_url` must use `https`.
