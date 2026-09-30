# The plugin API grows by appending host functions, and a plugin learns them from the table size

Until #662 the plugin API had never grown. A plugin gets the host's functions as one table, `LogSquirlHostApi`, passed to `logsquirl_plugin_init`, and `LOGSQUIRL_PLUGIN_API_VERSION` (1) is checked for equality in `plugin.json` and in the plugin's `LogSquirlPluginInfo`. The Regex Lab for plugins (#662), and going to a Log Line and reading the selected Log Lines (#663), add host functions. Plugins built against the old header, such as the four published ones (Logcat, Serial, tcpdump, Custom Footer), must keep loading and running unchanged; a plugin built against the new header must be able to find out whether the LogSquirl it runs in has the new functions, and do without them on an older one. The table had no size field, and its first member, `api_version`, was documented as equal to `LOGSQUIRL_PLUGIN_API_VERSION`.

## Decision

- **Append-only table.** A new host function is appended to the end of `LogSquirlHostApi`, below the comment *Added later*. Nothing is reordered, removed or changed in type or size, and only function pointers are appended, so the table has no padding at its end. A plugin built against an older header sees the same layout up to where its header ends.
- **The size of the table says what the host offers.** The host looks for an optional export, `logsquirl_plugin_init_ex( const LogSquirlHostApi* api, void* handle, size_t api_size )`, and calls it instead of `logsquirl_plugin_init`, with `sizeof( LogSquirlHostApi )` of its own build. An older host does not know it and calls `logsquirl_plugin_init`, which a plugin still exports: it forwards to its `init_ex` with `LOGSQUIRL_HOST_API_BASE_SIZE`, the size of the table before the first function was added. `LOGSQUIRL_HOST_API_HAS( size, member )` tells whether a table of that size has a function. The host calls exactly one of the two.
- **`LOGSQUIRL_PLUGIN_API_VERSION` stays 1** and still means compatibility: it changes only with an incompatible change, which rejects plugins of the other version at load time as before. The table's `api_version` member stays equal to it.
- **ABI rules** stay as they were and are written down in the header: C linkage, the platform's default C calling convention for every function both ways, UTF-8 NUL-terminated strings, strings passed to the host are copied, a string the host passes is valid as its function documents (for a callback: during the call). Each new function documents its thread (`open_regex_lab`: the UI thread only, returning non-zero elsewhere) and its callbacks.
- **No call into an unloaded plugin.** What the user interface answers a plugin through asynchronously is connected with a context object the Plugin Host keeps per loaded plugin. The host destroys it before it shuts the plugin down, and the plugin's contributions, a Regex Lab it opened included, are then removed; so nothing answers into a plugin that is shut down or whose library is released.

## Considered Options

- **A size field in the table.** The usual way, but only as the table's first member, and that is `api_version`. A size field appended at the end cannot be read on an older host: its table ends before it.
- **Counting up the table's `api_version` as a revision** (2 with the Regex Lab, 3 with #663). Simple for plugins, and none of the published plugins reads the member. But the header promised it equals `LOGSQUIRL_PLUGIN_API_VERSION`, and a plugin that checked that would stop working, which the ticket rules out; one name would also mean two things.
- **Bumping `LOGSQUIRL_PLUGIN_API_VERSION` to 2.** An older host rejects a plugin of version 2 outright, so the plugin could not degrade gracefully; the host would have to accept both versions for old plugins.
- **A query function** (`get_host_api_version`). It would itself be a new member of the table, which an older host does not have, so a plugin could not call it safely.
- **Host symbols the plugin resolves.** Plugins do not link against LogSquirl, and on Windows the executable exports nothing.

## Consequences

- A plugin built against the 26.10 header loads and runs unchanged; a test builds one against a frozen copy of that header.
- A plugin that wants a newer function exports two init functions, three lines more than before; the example plugin of the developer guide shows it, and its test runs it with an older host's table.
- Every later addition (#663 first) is one appended member and its trampoline; `LOGSQUIRL_HOST_API_HAS` covers it without a new mechanism. The developer guide lists which LogSquirl added which function.
- A struct passed from the plugin to the host (`LogSquirlPluginInfo`) cannot grow this way; growing it needs a mechanism of its own.
