# A data source is stopped once for every start it let run, before its stream ends

A data source plugin pushed lines into a stream the Plugin Host created when the user chose the plugin in the Sources menu, and ended when the user stopped it, the plugin was unloaded or LogSquirl quit. The plugin was called at none of these points, and lines pushed while no stream ran were dropped (#821), so it could not know when to open or close its device, socket or process: the published plugins open theirs in `init` and push from then on. A plugin may also need to ask the user for parameters -- which device, which host -- before a stream makes sense, and the user may cancel that (#855).

## Decision

- **Two optional exports**, resolved when the library loads like `convert_ex`: `int logsquirl_datasource_start( const LogSquirlDataSourceContext* )` and `void logsquirl_datasource_stop( const LogSquirlDataSourceContext* )`. A plugin without them streams as before; each is optional on its own. `LOGSQUIRL_PLUGIN_API_VERSION` stays 1.
- **`LogSquirlDataSourceContext` grows behind its leading `size`**, as ADR 0020 says for a struct the host passes. It holds only `size` for now: it is where a stream id or the stream's parameters go later. The host owns it, and it is valid during the call only.
- **Start runs on the UI thread before the stream counts as started.** The host creates the stream first, so that lines pushed from inside start are kept, then calls start as a call into the plugin, so that an unload asked for from a dialog start shows waits until it returns. `LOGSQUIRL_DATASOURCE_STARTED` (0) lets the stream run: the host says `dataSourceStarted` and the tab opens. Anything else frees the stream again, says nothing and opens no tab. `LOGSQUIRL_DATASOURCE_CANCELLED` is reported to nobody but the log, since the user chose it; `LOGSQUIRL_DATASOURCE_FAILED` and every other value are reported as a start that failed, with the value, as a failed start was reported before. A start while the plugin's stream runs or starts is refused by the host and does not reach the plugin.
- **Stop runs on the UI thread exactly once for every start that let the stream run**, and before the host signals end-of-stream, however the stream ends: the user stops it, the plugin is unloaded, or LogSquirl quits. On an unload the order is stop, `logsquirl_plugin_shutdown`, end-of-stream: the end of the stream still waits for shutdown, so that a thread of the plugin's pushes into a live stream until it is joined (#821). A plugin without start counts as having let every stream run.
- **Stop is owed also while the plugin's unload is pending.** It goes through the guarded call path like every call into the plugin. When the user stops the stream while an unload waits for another call into the plugin to return, that path refuses calls; the plugin is not shut down yet, so the host calls stop all the same, counted as a call into the plugin, rather than ending the stream before stop or skipping it.
- **A plugin unloaded from inside its own start that lets the stream run** counts as unloaded: no tab opens, and the unload, once start has returned, stops the plugin, shuts it down and ends the stream.
- A stop asked for while the plugin's start or stop runs does nothing: the call under way ends the way it ends.

## Considered Options

- **Creating the stream after start returned 0.** No stream would have to be freed on a refusal, but a plugin that starts a reader thread inside start would lose the first lines, and a second start during start's dialog would need a state of its own to be refused anyway.
- **A non-zero return always reported, or never.** The host cannot tell a cancel from a failure, and a plugin cannot word the host's error dialog. Always reporting shows an error for a cancel; never reporting leaves a failure unexplained when the plugin does not say it. One code for a cancel and every other value for a failure lets the plugin choose, and a plugin that returns 1 or -1 for any failure gets the error dialog, not silence.
- **A host-side parameter dialog from declared parameters.** Out of scope; start shows the plugin's own dialog meanwhile, and the context is where the parameters would go.
- **Calling stop after `logsquirl_plugin_shutdown`, or not at all on unload, since shutdown releases everything.** A plugin would then close its source in two places, and its last lines would race the end of the stream.

## Consequences

- A plugin that opens its source in start and closes it in stop no longer pushes into nothing between streams, and may refuse a stream it cannot serve.
- A plugin that wants to run on a host older than 26.12 too cannot rely on start: such a host calls neither export. The hosts that call them pass a table that holds `unregister_menu_action`, which the plugin checks with `LOGSQUIRL_HOST_API_HAS` to open its source in `init` on an older host.
- A plugin that signalled end-of-stream itself still counts as streaming until the host ends the stream, and is stopped then.
- Several streams of one plugin at a time, a stream id, or a stream argument for `push_line` would add members to the context; the context's `size` keeps that open without a new export.
