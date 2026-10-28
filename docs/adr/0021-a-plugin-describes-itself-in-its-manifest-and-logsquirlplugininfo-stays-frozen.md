# A plugin describes itself in its manifest, and LogSquirlPluginInfo stays frozen

ADR 0017 and ADR 0020 settle how the structs the host passes a plugin grow, and both note that `LogSquirlPluginInfo`, which a plugin returns from `logsquirl_plugin_get_info`, cannot: it has no leading size, so the host cannot tell how far an older plugin filled it. #856 proposed a second export, `logsquirl_plugin_get_info_ex`, returning a struct with `size` first, for what a plugin will want to declare later: the oldest LogSquirl it needs, capabilities, a homepage, the Qt it was built against.

Meanwhile a plugin's self-description has grown in another place. `plugin.json` gained `formats` in 26.12 and `qt_version` with #862. The Plugin Catalog reads the manifest without loading the library, and the Plugin Loader refuses a plugin it cannot serve before any of its code runs.

## Decision

- **What a plugin declares about itself goes into `plugin.json`.** A new key is optional, and a host that does not know it ignores it, so a plugin with the key still loads in an older LogSquirl, and one without it loads in a newer one. A key the host must understand to serve the plugin safely is checked by the Plugin Loader before it loads the library, as `qt_version` is.
- **`LogSquirlPluginInfo` stays as it is.** It is not extended, and there is no `logsquirl_plugin_get_info_ex`. The host keeps checking `api_version` in it against the manifest's.
- **What only the running plugin knows**, decided at run time rather than at build time, is asked for through a host-to-plugin struct (ADR 0020) or a host function (ADR 0017), not through a struct the plugin hands over.

## Considered Options

- **`logsquirl_plugin_get_info_ex` with a leading `size`** (#856). It works, but the host learns its contents only after loading the library and running the plugin's code. That is too late to refuse a plugin for the LogSquirl or Qt it needs, and a second place to declare things the manifest already declares.
- **Appending to `LogSquirlPluginInfo` and keying on `api_version`.** Raising `LOGSQUIRL_PLUGIN_API_VERSION` refuses every published plugin, which ADR 0017 rules out.

## Consequences

- Most of what `LogSquirlPluginInfo` holds (id, name, version, type, api_version) is said twice, in the manifest and in the library. The manifest is the one the user interface shows; the library's copy is a check.
- A build that wants a value from the build system in its manifest writes `plugin.json` from a `plugin.json.in` with `configure_file`, as the plugin SDK guide shows for `qt_version`.
- A future key (an oldest LogSquirl, a homepage) is a manifest field, a `PluginMetadata` accessor and a row in the guide's manifest table, with no ABI question.
