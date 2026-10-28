# A plugin sets Marks in every tab of the Log File, from any thread

An analysis plugin finds Log Lines worth a look -- an error burst, an anomaly, the start of a request -- and had no way to show them but a list of its own. LogSquirl already has a way to walk through Log Lines of interest: Marks, which the bullet margin and the Filtered View show and Next Mark and Previous Mark visit (#866, part of the decision in #860). Marks belong to a tab: each tab's Open Log File keeps its own, and the Session saves them per tab. Plugins read an open Log File by path on any thread (ADR 0024), and the analysis that finds the Log Lines runs on such a thread.

## Decision

- **Three host functions are appended** (ADR 0017), all LogSquirl 26.12: `add_marks`, `remove_marks` and `get_marks`. They name the Log File by its path, found as `get_log_lines` finds it (`PluginLogFiles`, ADR 0024), number Log Lines from 1 as the views do, and answer with the `LogSquirlLogLinesResult` codes; no new code was needed.
- **A Mark a plugin sets is an ordinary Mark.** It goes through the same Open Log File call as the user's, so the views, Next Mark, the Filtered View and the Session treat it as any other, and the user removes it as any other. The host does not remember who set a Mark: `remove_marks` removes the user's too, and `get_marks` reads both. `CONTEXT.md` says a Mark is flagged by the user, or by a plugin on the user's behalf.
- **`add_marks` and `remove_marks` may be called from any thread and never wait for the UI thread**, as `open_file` does: the host checks the arguments and looks the Log File up on the calling thread, answers at once, and carries the change out on the UI thread -- at once when called there, else queued, in the order of the calls of one thread; a call queued by a plugin unloaded before it runs is dropped, and a tab that closed meanwhile marks nothing. The answer `OK` says the Marks are set or will be.
- **`get_marks` is UI thread only**, like `get_selected_log_lines`: the Marks live in the tabs, and a worker that waits for the UI thread deadlocks (ADR 0024). Its array is the host's, valid until the plugin calls `get_marks` again or is shut down (ADR 0022). It holds the Marks in the order of the Log File, each once, `NULL` with a count of 0 when there are none.
- **One call takes at most `LOGSQUIRL_MARKS_MAX_LINES` (10000) line numbers**; more is `INVALID_ARGUMENT` and changes nothing, so one call never stalls the UI thread for long. A line of 0 or past the last Log Line indexed when the change is carried out, one already marked (to add) and one not marked (to remove) is skipped without failing the call: an analysis that raced a truncation still marks what is there.
- **Several tabs of one Log File: every tab.** `add_marks` and `remove_marks` change the Marks of every tab that shows the Log File, in every window, in front or not; `get_marks` reads a Log Line as marked when any of them marks it. Each tab still keeps Marks of its own -- the user's set in one tab stay there.

## Considered Options

- **The tab in front only, for several tabs.** A plugin names a Log File, not a tab; which tab is in front is a UI-thread notion that changes under a worker, and "in front" across windows is the most recently active window's, which the plugin cannot see. Marks the user cannot find because they went to a tab behind would look like a failed call.
- **The tab `PluginLogFiles` finds, the one opened last.** Consistent with `get_log_lines`, which reads the same data whichever tab it finds, but Marks differ per tab: a plugin's finding would show in one tab and not in another of the same Log File.
- **Waiting on the UI thread so the call answers what it did** (how many were set, `OUT_OF_RANGE` for lines past the end). A worker waiting for the UI thread while a modal dialog of the plugin's waits for the worker deadlocks, and the Log File may grow or be truncated between the call and its change anyway.
- **Failing the whole call for a line past the end.** An analysis of a Log File that is being written or truncated would have to retry; skipping is what the user's own marking does with a selection past the end.
- **Marks a plugin owns apart from the user's**, removed with the plugin or cleared without touching the user's. Out of scope for #866: it needs a second kind of Mark in the views, the Session and Next Mark.

## Consequences

- An analysis plugin marks what it found from its own thread, and the user walks through it with Next Mark and Previous Mark, in every tab of that Log File.
- A plugin that removes Marks it did not set removes the user's; a plugin that wants to undo only its own keeps the numbers it set.
- A plugin does not hear when the user changes Marks; it reads them again with `get_marks`. Marks do not survive a reload or a truncation, the user's included, which the log lines callback tells as `LOGSQUIRL_LOG_LINES_REPLACED`.
- `get_marks` right after `add_marks` from a worker may not see the new Marks yet; it sees them once the event loop has turned.
