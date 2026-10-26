# A converter claims a file by its content after the extensions, in plugin-id order, on a worker thread

A converter plugin was asked about a file by its extension only: `logsquirl_converter_get_extensions` lists the suffixes it takes, and the main window asked the Plugin Host for `converterForExtension` as the file opened. A format whose files have a generic extension or none -- a capture saved as `.log`, `.bin`, `.dat` -- was never converted. When two converters listed the same extension, the one with the smallest plugin id won, because the host keeps its plugins in a map by id; that was written down nowhere (#861).

## Decision

- **One optional export is added**, resolved when the library loads next to the other converter exports, LogSquirl 26.12: `int logsquirl_converter_claims( const char* file_path, const unsigned char* head, size_t head_length )`. Non-zero claims the file. A plugin without it is never asked. Only a plugin that is a converter already (`get_extensions` and a convert) is asked; one that claims by content only returns an empty extension list. `LOGSQUIRL_PLUGIN_API_VERSION` stays 1.
- **The head is read by the host**: `LOGSQUIRL_CONVERTER_CLAIMS_HEAD_LENGTH` (4096) bytes, or fewer for a shorter file, 0 for an empty one; `head` is never `NULL` and valid during the call only. A file the host cannot open or read is claimed by no plugin, and no plugin is asked. The path handed is absolute.
- **The order is fixed and written down**: first a converter whose extensions match, of several the one with the smallest plugin id; else the converters that export the check, asked one after the other in plugin-id order, and the first that claims the file converts it; else decompression and loading as before. The host sorts its converters by id for both steps rather than relying on how it stores them.
- **The content is the file's on disk, before decompression.** A compressed file whose own head a converter claims is converted, not decompressed. A file decompressed or taken out of an archive is opened like any file and goes through both steps again; no extra rule.
- **Only an Ordinary Log File is offered.** What LogSquirl writes itself -- a Conversion's output, a stream's spool, a merge, the clipboard's text, a download -- is Transient and is opened as before. A Conversion's output is so never offered back to the converter that wrote it.
- **The check runs on a worker thread of the host's, one thread per file.** `PluginHost::checkContent` starts it and reports the claiming plugin's id, or none, on the host's thread; it returns 0 and reports nothing when no loaded converter exports the check, and the main window then opens the file synchronously as before. Checks of different files run at the same time, also beside a Conversion of the same plugin. While a check runs, the window keeps the file as opening, as while the plugins load; opening it again meanwhile waits for the same check. A window that closes cancels its checks: the host reports nothing then, and asks no further plugin.
- **An unload waits for the plugin's call under way**, as it waits for a running Conversion: the plugin leaves every check, and the unload blocks until a call into it has returned, before `logsquirl_plugin_shutdown`. It does not wait for checks that are asking another plugin. A claim the plugin returned from a call during which it left counts for nothing, and the next plugin is asked. A plugin that claimed the file and is unloaded before the result reaches the UI thread converts nothing; the file opens as text.

## Considered Options

- **Content before extension.** A greedy content check of one plugin would take files another plugin names by extension, and every file opened would wait for the check. The extension is the cheaper and more explicit claim.
- **Asking on the UI thread, or with a deadline.** The head read alone may block on a network drive, and a plugin's check is the plugin's code; a deadline would leave the plugin's call running after the host gave up on it, so the unload would have to wait anyway.
- **Asking all converters at once and choosing by id.** Faster when several plugins check slowly, but calls plugins whose answer is not needed, and a check is meant to compare a few bytes.
- **A user-set priority between converters, or an "open without conversion".** Out of scope; a user who does not want a plugin's claims disables the plugin.
- **Showing the example plugin of the guide as a converter.** It is a menu plugin whose purpose is the smallest plugin that loads; the guide shows the check as a snippet in its Converters section instead.

## Consequences

- A plugin for a format with a magic number converts its files whatever they are named, and decompressed members of an archive too.
- With a converter that checks content loaded, opening an Ordinary Log File no extension matches is asynchronous: its tab comes from the event loop, after the check. Without one, nothing changes.
- A slow or blocking check delays the opening of every such file, and an unload of that plugin, by as long as it takes; the guide asks plugins to return quickly.
- A downloaded file is not offered by content; its extension still counts.
