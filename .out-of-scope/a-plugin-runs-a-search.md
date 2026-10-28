# A Plugin Runs a Search

A plugin cannot start a Search in a tab and read its Matches through the
plugin API.

## Why this is out of scope

A Search is one of the larger machines in LogSquirl. It runs asynchronously
over every Log Line, in parallel, with several kinds of pattern (plain text,
regular expression, logical combinations with Field Terms), reports progress,
can be cancelled, and keeps going in follow mode. Opening it to plugins means
a stable C surface for all of that: the pattern kinds and their flags, a
Search handle, progress and completion callbacks, cancellation, and what
happens when the user starts their own Search in the same tab. Every one of
those is a promise to every published plugin.

The use cases raised in #860 ("every Log Line with this request id",
correlation across lines) can be done without it: a plugin reads Log Lines by
range (#858) and matches them itself. That is slower than the engine's Search
on a very large Log File, but it costs no API surface. A plugin that wants the
user to see the result can set Marks (#866).

The maintainer's call during triage of #860 was "not for now". If a plugin
turns up for which scanning the lines itself is too slow, delete this record
and file a ticket for a minimal API: one pattern, and its Matches as line
numbers through a callback.

## Prior requests

- #860: "Decide which Searches, Filters, Highlighters, Marks and fields a plugin may drive"
