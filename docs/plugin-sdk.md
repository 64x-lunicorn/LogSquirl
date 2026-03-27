# LogSquirl Plugin SDK

This guide explains how to create plugins for
[LogSquirl](https://github.com/64x-lunicorn/LogSquirl), the cross-platform log
viewer.

## Architecture Overview

LogSquirl uses a **pure C ABI** boundary between the host application and
plugins. This design decouples plugin licensing from the GPL-licensed host:

- The SDK header (`logsquirl_plugin_api.h`) is released under the **MIT licence**.
- Plugins are standalone shared libraries (`.dylib` / `.so` / `.dll`).
- Plugins do **not** link against any host library — the host resolves their
  symbols at runtime via `QLibrary`.
- All data crosses the boundary as C primitives (`int`, `const char*`,
  `size_t`, `void*`).

### Plugin Types

| Type          | Enum                          | Purpose                                              |
|---------------|-------------------------------|------------------------------------------------------|
| Data Source   | `LOGSQUIRL_PLUGIN_DATASOURCE` | Stream log lines into a `StreamLogData` view         |
| Converter     | `LOGSQUIRL_PLUGIN_CONVERTER`  | Convert a file format into plain text before viewing |
| UI Extension  | `LOGSQUIRL_PLUGIN_UI`         | Add menu items, status-bar widgets, or panels        |

---

## Quick Start

### 1. Create a Plugin Directory

```
my_plugin/
├── CMakeLists.txt
├── plugin.json          # manifest
└── src/
    └── my_plugin.cpp    # implementation
```

### 2. Write the Manifest (`plugin.json`)

```json
{
    "id": "com.example.my-plugin",
    "name": "My Plugin",
    "version": "0.1.0",
    "type": "datasource",
    "library": "libmy_plugin.dylib",
    "api_version": 1,
    "description": "A short description of what the plugin does.",
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
| `library`      | **yes**  | Filename of the shared library (platform-specific extension).   |
| `api_version`  | **yes**  | Must be `1` (current API version).                              |
| `description`  | no       | One-line description shown in the plugin manager.               |
| `author`       | no       | Author name or organisation.                                    |
| `license`      | no       | SPDX identifier of the plugin's licence.                        |

**Important**: `library` is the *filename* only — no path separators. The host
resolves it relative to the directory containing `plugin.json`.

### 3. Write the Implementation

```c
/* my_plugin.cpp — MIT licence example */
#include "logsquirl_plugin_api.h"
#include <cstring>

static LogSquirlPluginInfo pluginInfo;

/* Required: return plugin metadata */
LOGSQUIRL_PLUGIN_EXPORT LogSquirlPluginInfo LOGSQUIRL_PLUGIN_GET_INFO_NAME( void )
{
    pluginInfo.api_version  = LOGSQUIRL_PLUGIN_API_VERSION;
    pluginInfo.id           = "com.example.my-plugin";
    pluginInfo.name         = "My Plugin";
    pluginInfo.version      = "0.1.0";
    pluginInfo.description  = "A short description.";
    pluginInfo.type         = LOGSQUIRL_PLUGIN_DATASOURCE;
    return pluginInfo;
}

/* Required: initialise the plugin */
LOGSQUIRL_PLUGIN_EXPORT int LOGSQUIRL_PLUGIN_INIT_NAME(
    const LogSquirlHostApi* api, void* handle )
{
    /* Store `api` and `handle` for later use in your plugin state. */
    /* Return 0 on success, non-zero on failure. */
    api->log_message( handle, LOGSQUIRL_LOG_INFO, "My plugin initialised" );
    return 0;
}

/* Required: clean up resources */
LOGSQUIRL_PLUGIN_EXPORT void LOGSQUIRL_PLUGIN_SHUTDOWN_NAME( void )
{
    /* Release any resources you allocated. */
}
```

### 4. Build with CMake

```cmake
cmake_minimum_required(VERSION 3.12)
project(my_plugin LANGUAGES CXX)

# Only include the SDK header — no host libraries needed
set(LOGSQUIRL_SDK_DIR "" CACHE PATH "Path to LogSquirl SDK header directory")

add_library(my_plugin SHARED src/my_plugin.cpp)

target_include_directories(my_plugin PRIVATE "${LOGSQUIRL_SDK_DIR}")
target_compile_features(my_plugin PRIVATE cxx_std_17)

# Remove lib prefix on all platforms for consistent naming
set_target_properties(my_plugin PROPERTIES PREFIX "lib")
```

```bash
cmake -B build -S . -DLOGSQUIRL_SDK_DIR=/path/to/logsquirl_plugin_api.h/dir
cmake --build build
```

### 5. Install the Plugin

Copy the built library and `plugin.json` into one of the plugin search
directories:

| Platform | Directories                                                               |
|----------|---------------------------------------------------------------------------|
| macOS    | `<app>/../PlugIns/` · `~/Library/Application Support/LogSquirl/plugins/` |
| Linux    | `<app>/plugins/` · `~/.local/share/LogSquirl/plugins/`                   |
| Windows  | `<app>/plugins/` · `%APPDATA%/LogSquirl/plugins/`                        |

Each plugin should live in its own subdirectory:

```
~/.local/share/LogSquirl/plugins/
└── com.example.my-plugin/
    ├── plugin.json
    └── libmy_plugin.so
```

---

## C ABI Reference

### Plugin Exports

Every plugin **must** export these three symbols:

| Symbol                             | Signature                                                      |
|------------------------------------|----------------------------------------------------------------|
| `logsquirl_plugin_get_info`        | `LogSquirlPluginInfo (void)`                                   |
| `logsquirl_plugin_init`            | `int (const LogSquirlHostApi* api, void* handle)`              |
| `logsquirl_plugin_shutdown`        | `void (void)`                                                  |

**Optional** exports:

| Symbol                             | Signature                                        | Used by       |
|------------------------------------|--------------------------------------------------|---------------|
| `logsquirl_plugin_configure`       | `void (void* parent_widget)`                     | All types     |
| `logsquirl_converter_get_exts`     | `const char* (void)` — semicolon-separated list  | Converter     |
| `logsquirl_converter_convert`      | `int (const char* in, const char* out)`          | Converter     |

### `LogSquirlPluginInfo`

Returned by `get_info`. String pointers must remain valid until `shutdown` is
called.

```c
typedef struct LogSquirlPluginInfo {
    int           api_version;  /* Must equal LOGSQUIRL_PLUGIN_API_VERSION (1) */
    const char*   id;
    const char*   name;
    const char*   version;
    const char*   description;  /* May be NULL */
    LogSquirlPluginType type;
} LogSquirlPluginInfo;
```

### `LogSquirlHostApi`

Function-pointer table provided by the host during `init`:

```c
typedef struct LogSquirlHostApi {
    int version;   /* == LOGSQUIRL_PLUGIN_API_VERSION */

    /* Data source streaming (Phase 2) */
    void (*push_line)  (void* handle, const char* data, size_t len);
    void (*push_lines) (void* handle, const char* const* data,
                        const size_t* lens, size_t count);
    void (*signal_eos) (void* handle);
    void (*signal_error)(void* handle, const char* message);

    /* Logging */
    void (*log_message)(void* handle, int level, const char* message);

    /* Plugin configuration directory */
    const char* (*get_config_dir)(void* handle);

    /* UI integration (Phase 3) */
    void (*show_notification)(void* handle, const char* message);
    void (*open_file)(void* handle, const char* file_path, int follow);
    void (*register_status_widget)  (void* handle, void* qwidget_ptr);
    void (*unregister_status_widget)(void* handle, void* qwidget_ptr);
    void (*register_menu_action)    (void* handle, const char* menu_path,
                                     const char* label,
                                     void (*callback)(void*),
                                     void* user_data);
} LogSquirlHostApi;
```

The `handle` is an opaque pointer — always pass the same `handle` value that
was given to your `init` function. **Never** dereference or interpret it.

### Log Levels

```c
#define LOGSQUIRL_LOG_DEBUG   0
#define LOGSQUIRL_LOG_INFO    1
#define LOGSQUIRL_LOG_WARNING 2
#define LOGSQUIRL_LOG_ERROR   3
```

---

## Host API Usage Guide

### Logging

```c
api->log_message( handle, LOGSQUIRL_LOG_INFO, "Processing started" );
```

Messages appear in the LogSquirl log output prefixed with your plugin ID.

### Configuration Directory

```c
const char* dir = api->get_config_dir( handle );
/* e.g. ~/.local/share/LogSquirl/plugin_config/com.example.my-plugin/ */
```

The directory is created automatically before `init` is called. The pointer
remains valid until `shutdown`. Use it to persist any plugin-specific settings.

### Data Source Streaming (Phase 2)

Data-source plugins push log lines to the host:

```c
/* Push a single line */
api->push_line( handle, line_data, line_length );

/* Push multiple lines at once for efficiency */
const char*  lines[]  = { line1, line2, line3 };
const size_t lens[]   = { len1,  len2,  len3  };
api->push_lines( handle, lines, lens, 3 );

/* Signal end of stream */
api->signal_eos( handle );
```

### UI Integration (Phase 3)

```c
/* Show a notification toast */
api->show_notification( handle, "Import complete" );

/* Ask the host to open a file */
api->open_file( handle, "/tmp/converted.log", 0 /* follow = false */ );
```

### Configuration Dialog

If your plugin exports `logsquirl_plugin_configure`, the host calls it with a
`void*` that is actually a `QWidget*` parent. Cast it and create your dialog:

```cpp
LOGSQUIRL_PLUGIN_EXPORT void LOGSQUIRL_PLUGIN_CONFIGURE_NAME( void* parent_widget )
{
    auto* parent = static_cast<QWidget*>( parent_widget );
    MyConfigDialog dialog( parent );
    dialog.exec();
}
```

This is the **only** place where Qt types cross the ABI boundary. If you don't
use Qt in your plugin, simply don't export this symbol.

---

## Guidelines

- **Thread safety**: Host API calls are safe from any thread. The host
  dispatches UI-modifying calls to the main thread internally.
- **String lifetime**: All `const char*` strings you pass to host API functions
  are copied immediately — you may free them after the call returns.
- **Error reporting**: Return non-zero from `init` on failure. The host reads
  `get_info()` to identify the failing plugin.
- **No host headers**: Never include any host header other than
  `logsquirl_plugin_api.h`. This keeps your plugin fully decoupled.
- **Cross-platform**: Use the `LOGSQUIRL_PLUGIN_EXPORT` macro for all exported
  symbols — it handles `__declspec(dllexport)` on Windows and
  `__attribute__((visibility("default")))` on Unix.

---

## FAQ

**Q: Can I use C++ in my plugin?**
A: Yes. The exported functions must use C linkage (`extern "C"`) and C types at
the boundary, but the implementation may be C++. Use `cxx_std_17` or later.

**Q: Do I need to link against Qt?**
A: No. Most plugins don't need Qt at all. The only exception is if you export
`logsquirl_plugin_configure` and want to show a Qt dialog — in that case, link
against `Qt6::Widgets`.

**Q: My plugin needs a background thread. Is that safe?**
A: Yes. Create threads as needed. All host API functions are thread-safe. Call
`signal_eos` or `signal_error` when your thread is done.

**Q: How do I handle plugin-specific configuration?**
A: Use `get_config_dir()` to obtain your private directory, then read/write
files there (JSON, INI, etc.). The host does not manage plugin-specific
settings — only the enable/disable state.

**Q: What happens if the API version changes in a future LogSquirl release?**
A: The host checks `api_version` in both `plugin.json` and the `get_info()`
return value. Incompatible plugins are rejected at load time with a clear error
message. Plugin authors update `api_version` and adapt to the new API.

---

## Lua Plugins (optional)

When LogSquirl is built with `LOGSQUIRL_USE_LUA=ON`, plugins can be written as
Lua 5.4 scripts instead of compiled shared libraries.

### Lua Plugin Layout

```
my_lua_plugin/
├── plugin.json          # manifest (library field = "script.lua")
└── script.lua           # Lua implementation
```

### plugin.json for Lua

```json
{
  "id": "com.example.my-lua-plugin",
  "name": "My Lua Plugin",
  "version": "1.0.0",
  "description": "A plugin written in Lua",
  "author": "You",
  "license": "MIT",
  "library": "script.lua",
  "type": "datasource",
  "api_version": 1
}
```

### Lua API

The host table is passed to `plugin_init()`:

```lua
function plugin_init(host)
    -- host.push_line(data_string)
    -- host.signal_eos()
    -- host.signal_error(message)
    -- host.log_message(level, message)  -- 0=debug, 1=info, 2=warning, 3=error
    -- host.show_notification(message)
    -- host.open_file(path, follow_bool)
    -- host.get_config_dir() -> string
end

function plugin_shutdown()
    -- cleanup
end

-- DataSource plugins:
function start_data()
    host.push_line("Hello from Lua!")
    host.signal_eos()
end

-- Converter plugins:
function convert_file(input_path, output_path)
    -- convert input to output, return 0 on success
    return 0
end
```

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
