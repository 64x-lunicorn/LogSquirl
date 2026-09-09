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
columns. Either detected automatically from the file's first lines or supplied by the user.
_Avoid_: schema, parser, layout

**Table View**:
The presentation of a Log File as one column per Log Format field, as an alternative to
the plain text presentation.
_Avoid_: grid, structured view

**Chart Preset**:
A saved configuration of which Log Format fields to plot and how.

### Session and settings

**Settings Policy**:
The small set of settings one part of the application actually needs, taken as a snapshot
and handed to it when it is built — an Indexing Policy, a Search Policy, a Watch Policy, a
File Access Policy. A part that holds a Policy cannot reach for a setting it did not declare.
_Avoid_: config object, options, preferences

**Session**:
The set of Log Files currently open, their tabs, and the position and view state restored
for each on the next start.
_Avoid_: workspace, project, layout
