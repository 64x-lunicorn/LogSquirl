# A struct the host passes a plugin grows by appending, behind its leading size

The background Conversion (#795) gives a converter plugin a way to report progress and to hear that the user cancelled. Both go through `LogSquirlConvertControl`, a struct the host fills and passes to the new optional export `logsquirl_converter_convert_ex` for the duration of the call. A later release may want more in it, and a plugin built against an older header must keep working with a newer host, and a newer plugin with an older host. ADR 0017 settled how `LogSquirlHostApi` grows and turned down a size field for it, because its first member was already `api_version`. This struct is new, so its first member is free.

## Decision

- **The first member is `size`**: `sizeof( LogSquirlConvertControl )` in the host's build, how far the running host filled the struct. Members are only appended, never reordered, removed or changed in type or size.
- **A plugin checks before it reads a later member**: `LOGSQUIRL_CONVERT_CONTROL_HAS( control, member )` tells whether the host filled the whole member. `report_progress` and `is_cancelled` were there from the start and need no check.
- **The host owns the struct**: it is valid during the call to `convert_ex` only, on the worker thread that call runs on. A plugin keeps the pointer it is given and never copies the struct, for the same reason it never copies `LogSquirlHostApi`: a copy reads the whole struct its header declares, past the end of what an older host filled.
- **Each function in the struct takes the struct itself as its first argument**, so the host finds the Conversion it belongs to through `host`, without a global or a lookup by thread.
- **`LOGSQUIRL_PLUGIN_API_VERSION` stays 1**: `convert_ex` is optional, and an older host calls `convert`, which a plugin exporting `convert_ex` still exports.

## Considered Options

- **Growing it the way ADR 0017 grows the table**, with no size in the struct and the size passed as an argument of `convert_ex`. That works, but the size then lives apart from the struct it describes, and any later function that passes the struct on would have to pass the size with it. ADR 0017 chose a separate size only because the table's first member was taken.
- **Plain arguments** (`convert_ex( in, out, progress_fn, cancelled_fn, user_data )`). Nothing could be added without a `convert_ex2`.
- **Host functions in `LogSquirlHostApi`** (`report_conversion_progress( handle, … )`). The table is shared by every plugin and every thread; the progress belongs to one call, so the host would have to find the Conversion from the calling thread or from a handle passed with every call.

## Consequences

- A member added later is one appended field, set by the host; `LOGSQUIRL_CONVERT_CONTROL_HAS` covers it without a new mechanism. The developer guide lists which LogSquirl added `convert_ex`.
- A plugin built against the 26.12 header and run by a later host sees a larger `size` and reads only what it knows.
- This applies to structs the host passes a plugin. A struct a plugin passes the host (`LogSquirlPluginInfo`) still cannot grow, as ADR 0017 says: the host cannot know how far an older plugin filled it unless that struct also carries a leading size, which `LogSquirlPluginInfo` does not.
