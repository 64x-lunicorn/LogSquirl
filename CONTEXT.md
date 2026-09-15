# LogSquirl

A desktop log file explorer: it opens very large log files, indexes them, and lets a
user search, filter, colorize and structure them interactively. This glossary fixes
the language used across the codebase and in design discussions.

Spelling follows the product documentation and the existing code: **color**, not colour.

## Language

### The log file

**Log File**:
The file being explored. May be plain, compressed, remote, or produced by a converter
plugin — once opened, all of these behave identically.
_Avoid_: document, source, input

**Log Line**:
One line of a Log File, addressed by its number in the file.
_Avoid_: record, entry, row (row belongs to the Table View)

**Index**:
The mapping from line numbers to byte offsets that makes a Log File navigable without
reading it end to end. Built once per file and cached across sessions.
_Avoid_: offset table, line map

**Encoding**:
The character encoding a Log File is interpreted with, either detected or chosen by the user.

### Displaying

**Presentation**:
The way the upper pane shows the Log Lines of a Log File: either the Text View or the
Table View. A Presentation owns its own selection.
_Avoid_: view mode, renderer, display

**Text View**:
The Presentation that paints Log Lines as Visual Lines of plain text. The Filtered View is
drawn the same way.
_Avoid_: plain view, painted view, main view

**Viewport**:
The visible area of a text view of a Log File — the main view or the Filtered View — with
its margins: the bullet zone, the optional line numbers and the text. One layout decides
where every Log Line is drawn in it and what sits under any point of it, before anything
has been painted.
_Avoid_: screen, canvas, page

**Visual Line**:
One line of text as drawn in the Viewport. Without text wrapping a Visual Line shows a
whole Log Line; with text wrapping a long Log Line is drawn as several Visual Lines.
_Avoid_: row (row belongs to the Table View), wrapped row, fragment, screen line

**Scroll Position**:
Where a text view stands in its Log File: the Log Line at the top of the Viewport together
with which of its Visual Lines is shown first. Scrolling moves it; nothing else does.
_Avoid_: first line, top line, anchor

### Searching and filtering

**Search**:
A pattern applied to a whole Log File, producing the set of lines shown in the Filtered View.
_Avoid_: query, grep

**Search Session**:
The owner of everything whose correctness depends on the ordering of a Search: the current
pattern, the run in flight, its results, its progress and its cached results. A new request
supersedes the one in flight rather than waiting for it.
_Avoid_: search manager, search controller, search engine

**Filtered View**:
The lower pane, showing only the Log Lines a Search selected.
_Avoid_: results pane, filter window

**QuickFind**:
Interactive incremental search within the currently displayed lines. Distinct from Search:
it navigates, it does not filter.
_Avoid_: find, incremental search

**Predefined Filter**:
A saved, named search pattern the user can apply without retyping it.
_Avoid_: saved search, bookmark

**Search Limits**:
An optional line range a Search is restricted to. Lines outside it are shown but visually
subdued.
_Avoid_: search range, scope

**Match**:
A Log Line selected by the current Search.

**Mark**:
A Log Line the user has flagged by hand. Independent of Match: a line can be either, both,
or neither.
_Avoid_: bookmark, flag, pin

**Context Line**:
A Log Line shown in the Filtered View only because it neighbours a Match, not because it
matched itself.

### Color

**Highlighter**:
One user-configured rule: a pattern plus the foreground and background color to paint what
it selects. A Highlighter colors either just the text it matched, or the entire Log Line.
_Avoid_: rule, colorizer, style

**Highlighter Set**:
A named group of Highlighters. Exactly one set is active at a time, so switching sets
re-colors the whole view at once.
_Avoid_: theme, profile, palette

**Color Label**:
A color the user assigns to a specific word on the fly, without editing a Highlighter Set.
Shorter-lived and more ad hoc than a Highlighter.
_Avoid_: quick highlighter, tag

**Decoration**:
The finished visual result for a piece of displayed text: an ordered, non-overlapping
sequence of colored spans. What every source of color — Highlighter Set, Search, QuickFind,
Color Label, selection, Line Verdict — is resolved *into*.
_Avoid_: styling, formatting, markup

**Line Verdict**:
The facts about a whole Log Line that affect how any part of it looks: whether a
whole-line Highlighter applies, whether the line is a Match, Mark or Context Line, and
whether it falls outside the Search Limits. Decided once per line.
_Avoid_: line state, line flags

**Line Decorator**:
The single owner of the precedence rule that turns a Line Verdict plus a piece of text
into a Decoration. It decides which color wins where; it does not draw.
_Avoid_: renderer, painter, highlighter (a Highlighter is a user's rule, not this)

### Structure

**Log Format**:
A description of how a Log Line is composed of named fields, used to present the file as
columns. Either built in or supplied by the user; which one applies to a Log File is decided
by Format Recognition.
_Avoid_: schema, parser, layout

**Log Format Catalog**:
Every Log Format available to choose from, built in and supplied by the user. A user's
Log Format replaces a built-in one of the same name. One Catalog serves the whole application.
_Avoid_: registry, library

**Format Recognition**:
The decision which Log Format, if any, applies to a Log File, taken from its first Log
Lines against the Log Format Catalog. Taken when a Log File has loaded, and again after it
is reloaded or truncated; in between, the Log File keeps the Log Format it was recognized
with, even when the Catalog changes.
_Avoid_: detection, sniffing

**Table View**:
The Presentation of a Log File as one column per Log Format field, as an alternative to
the Text View.
_Avoid_: grid, structured view

**Row**:
One Log Line as shown in the Table View. A Row is always addressed through its Log Line,
never through its position in the table.
_Avoid_: record, entry

**Chart Preset**:
A saved configuration of which Log Format fields to plot and how.

### Session and settings

**Settings Policy**:
The small set of settings one part of the application actually needs, taken as a snapshot
and handed to it when it is built — an Indexing Policy, a Search Policy, a Watch Policy, a
File Access Policy, a Recognition Policy. A part that holds a Policy cannot reach for a setting it did not declare.
_Avoid_: config object, options, preferences

**Axis**:
One Settings Policy, seen as the unit a change travels in. A changed setting is re-derived
into Policies and handed down one axis at a time, so changing a Highlighter Set does not
restart file watching, and changing the poll interval does not disturb a Search.
_Avoid_: category, group, domain

**Session**:
The set of Log Files currently open, their tabs, and the position and view state restored
for each on the next start.
_Avoid_: workspace, project, layout
