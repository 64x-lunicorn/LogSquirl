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
| `id`           | **yes**  | Reverse-domain unique identifier.                               |
| `name`         | **yes**  | Human-readable display name.                                    |
| `version`      | **yes**  | Semver string.                                                  |
| `type`         | **yes**  | One of `datasource`, `converter`, `ui`.                         |
| `library`      | **yes**  | File name of the shared library.                                |
| `api_version`  | **yes**  | Must be `1` (`LOGSQUIRL_PLUGIN_API_VERSION`).                   |
| `description`  | no       | One-line description shown in the plugin dialog.                |
| `author`       | no       | Author name or organisation.                                    |
| `license`      | no       | SPDX identifier of the plugin's licence.                        |
| `icon`         | no       | File name of an icon next to `plugin.json`.                     |

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
```

The exported functions need C linkage: in C++, define them inside
`extern "C"`, or the host does not find them by name. The example exports
`logsquirl_plugin_init_ex` as well as `logsquirl_plugin_init`, so it runs on a
LogSquirl that knows the Regex Lab function and on one that does not.

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
- `logsquirl_plugin_shutdown` releases everything the plugin allocated. The
  host calls it before it unloads the library.

**Optional** exports, for any plugin type:

```c
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget );
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi*, void*, size_t );
```

A host that knows `logsquirl_plugin_init_ex` calls it instead of
`logsquirl_plugin_init`, with the size of its host API table as the third
argument; an older host calls `logsquirl_plugin_init`. The host calls one of
them, never both. A plugin that exports `logsquirl_plugin_init_ex` still
exports `logsquirl_plugin_init`.

Converter plugins additionally export both of these:

```c
LOGSQUIRL_PLUGIN_EXPORT const char* logsquirl_converter_get_extensions( void );
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_converter_convert( const char*, const char* );
```

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
```

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
`open_regex_lab`, which is called on the UI thread only.

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
} LogSquirlHostApi;
```

Functions added to the table later are listed below *Added later*; before you
call one, check that the running host offers it (see
[A Growing API](#a-growing-api)).

The `handle` is an opaque pointer — always pass the same `handle` value that
was given to your `init` function. **Never** dereference or interpret it.

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
- `LOGSQUIRL_HOST_API_HAS` says whether a table of that size has a function.
  Never call a function it does not have: an older host's table ends before
  it.

```c
#define LOGSQUIRL_HOST_API_BASE_SIZE offsetof( LogSquirlHostApi, open_regex_lab )
#define LOGSQUIRL_HOST_API_HAS( size, member ) ( ( size ) > offsetof( LogSquirlHostApi, member ) )
```

The example plugin shows the pattern: `logsquirl_plugin_init` forwards to
`logsquirl_plugin_init_ex` with `LOGSQUIRL_HOST_API_BASE_SIZE`, and
`logsquirl_plugin_init_ex` offers *Test Pattern* only when
`LOGSQUIRL_HOST_API_HAS( api_size, open_regex_lab )`. Keep the size, or what it
tells, for later calls.

| Function         | Added in        |
|------------------|-----------------|
| `open_regex_lab` | LogSquirl 26.11 |

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
called. The returned string is valid until the
next call to `get_config_dir`. Use it to persist any plugin-specific settings.

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

Lines pushed while no stream of the plugin is running are dropped.

### Converters

A converter plugin (`"type": "converter"`) exports
`logsquirl_converter_get_extensions` and `logsquirl_converter_convert`.
`logsquirl_converter_get_extensions` returns the file extensions it handles,
separated by semicolons, e.g. `".har;.pcap"`. When a Log File with one of
these extensions is opened, the host calls `logsquirl_converter_convert` with
the file's path and the path of a temporary file to write plain text into, and
shows that text if the converter returns 0. Both paths are absolute and UTF-8.

### UI Integration

```cpp
/* Show a notification toast */
api->show_notification( handle, "Import complete" );

/* Ask the host to open a file */
api->open_file( handle, "/tmp/converted.log", 0 /* follow = false */ );

/* Add an item to the Plugins menu */
api->register_menu_action( handle, "Plugins", "Say Hello", &sayHello, nullptr );
```

`menu_path` names the submenus of the `Plugins` menu the item goes into,
separated by `/`: `"My Plugin/Sub"` puts `label` into
`Plugins` → `My Plugin` → `Sub`. An empty path, or `"Plugins"`, puts the item
directly into the `Plugins` menu; a path starting with `Plugins/` is read
without that first segment. The last segment is a submenu too, the item's own
text is always `label`. Items and submenus go above `Plugin Management...`,
and plugins naming the same path share its submenus. When the plugin is
unloaded, its items go away, and so does every submenu nothing is left in.

`register_status_widget`, `register_sidebar_tab` and `register_footer_widget`
take a `QWidget*` cast to `void*`: the plugin creates and owns the widget and
the host parents it. Unregister the widget again before you delete it, at the
latest in `shutdown`. Such a plugin links against the same Qt 6 LogSquirl
uses.

### Active File

```c
/* The path of the Log File in the focused tab, or "" */
const char* path = api->get_active_file_path( handle );

/* Be told whenever the focused Log File changes */
api->register_active_file_callback( handle, &onActiveFile, userData );
```

The string `get_active_file_path` returns is valid until the next host API
call.

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
  `open_regex_lab`, which is called on the UI thread only. The host
  dispatches UI-modifying calls to the main thread internally.
- **String lifetime**: All `const char*` strings you pass to host API functions
  are copied immediately — you may free them after the call returns.
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
the host or shows a Qt dialog links against `Qt6::Widgets`.

**Q: My plugin needs a background thread. Is that safe?**
A: Yes. Create threads as needed. All host API functions are thread-safe. Call
`signal_eos` or `signal_error` when your thread is done.

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
Downloaded archives are verified against the `sha256` checksum.

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
