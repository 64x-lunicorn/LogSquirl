# A Plugin Info Struct That Grows

`LogSquirlPluginInfo`, the struct a plugin returns from
`logsquirl_plugin_get_info`, does not grow, and there is no
`logsquirl_plugin_get_info_ex` with a leading size beside it.

## Why this is out of scope

Everything a plugin wants to say about itself goes into its manifest,
`plugin.json`, instead. ADR 0021 records the decision. The manifest is JSON,
so a new optional key costs no ABI thought: an older host ignores it, and a
newer host gets along without it. And the host reads the manifest *before* it
loads the library. That is what lets the Plugin Loader refuse a plugin built
against a Qt the host cannot serve (#862) without running any of its code.
An extended info struct could only be read after that point.

The fields #856 had in mind (the oldest LogSquirl a plugin needs, capabilities,
a homepage, the Qt it was built against) are all known when the plugin is
built:

```json
{
  "id": "io.example.myplugin",
  "api_version": 1,
  "qt_version": "6.8"
}
```

`qt_version` is already there. The others would be keys next to it.

If a plugin ever has to tell the host something only its running code knows,
that is a host function (ADR 0017) or a host-to-plugin struct (ADR 0020), not
a plugin-to-host struct.

## Prior requests

- #856: "A plugin describes itself in an info struct that can grow"
