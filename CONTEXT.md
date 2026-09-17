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

**Index Cache**:
The Indexes of earlier sessions, kept on disk so a Log File opened again need not be
indexed again. It hands out an Index only while that Index still fits its Log File, and
it decides for itself what it keeps and what it lets go.
_Avoid_: index store, cache file

**Open Log File**:
A Log File from the moment it is opened until it is closed, together with what follows it
as it changes on disk: its Index, its Searches and their auto-refresh, its Marks, and its
Log Format. It decides what growing, truncation and reloading mean — a Search continues over
lines that were added and starts again when the Log File was truncated or reloaded, Marks do
not survive a truncation or a reload, and Format Recognition is taken again after either. The
Marks saved with the Session are handed to it when the Log File is opened and applied once,
after the first load; saving them stays with the user interface. A Search requested before
the Log File has first loaded waits for that load and then runs over the whole Log File.
It hears of changes on disk through the File Watch Port handed to it when it is built.
The desktop application and the command line tool follow a Log File the same way because
both use it.
_Avoid_: document, loaded file, file session

**File Watch Port**:
Everything an Open Log File needs from file watching: to have its Log File watched from its
first load until it is closed, and to hear that a watched file changed on disk. It does not
say what changed — the log data checks the file for growth, truncation or replacement. The
efsw watcher, which follows the Watch Policy, is the adapter the application hands over; the
tests hand over a fake that reports a change when they say so. Nothing in the engine looks a
watcher up by itself.
_Avoid_: file watcher singleton, watch service

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

**View Set**:
Every view of one Log File: its Presentations and its Filtered Views, those of kept
Searches included. Whatever all of them must show alike — the Policies, the font, the Color
Labels, the Search Limits — is handed to the View Set, which hands it to every view, and a
view added later starts with all of it.
_Avoid_: views, panes, tabs

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
The lower pane, showing only the Log Lines a Search selected. Its selection, Marks and
Search Limits are Log Lines like the main view's; only its Scroll Position counts places
among the Log Lines it shows.
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
subdued. Half-open everywhere: from the first Log Line searched up to, not including, its
end — the Log Line after the last one searched. No Presentation converts the end.
_Avoid_: search range, scope

**Match**:
A Log Line selected by the current Search.

**Mark**:
A Log Line the user has flagged by hand. Independent of Match: a line can be either, both,
or neither.
_Avoid_: bookmark, flag, pin

**Context Line**:
A Log Line shown in the Filtered View only because it neighbours a Match or a Mark, not
because it matched itself.

**Displayed Lines**:
The Log Lines the Filtered View shows, in order: the Matches, the Marks, and — while they
are shown — the Context Lines around them. The Search Session owns the Matches; the Displayed
Lines own the Marks and the Context Lines and combine all three, so the Filtered View asks
them which Log Line sits at which position.
_Avoid_: filtered lines, results, visible lines

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
sequence of colored spans that covers the whole text, where text no source colors carries
the line's own colors. What every source of color — Highlighter Set, Search, QuickFind,
Color Label, selection, Line Verdict — is resolved *into*. Both Presentations draw a
Decoration as it is; neither decides a color for itself.
_Avoid_: styling, formatting, markup

**Line Verdict**:
The facts about a whole Log Line that affect how any part of it looks: whether a
whole-line Highlighter applies, whether the line is a Match, Mark or Context Line, whether
it falls outside the Search Limits, and whether it is selected as a whole. A line selected
as a whole shows the selection colors with only its QuickFind matches on top, in either
Presentation. Decided once per line.
_Avoid_: line state, line flags

**Line Decorator**:
The single owner of the precedence rule that turns a Line Verdict plus a piece of text
into a Decoration. It decides which color wins where; it does not draw.
_Avoid_: renderer, painter, highlighter (a Highlighter is a user's rule, not this)

### Appearance

**Theme**:
The look of the application around the Log Lines: Light, Dark or High Contrast, or System,
which becomes Light or Dark from the operating system's color scheme and follows it while the
application runs. Choosing a Theme takes effect at once, in every open window. A Theme is
exactly one set of Tokens; the application's palette and stylesheet are both derived from
it. A Theme does not color Log Lines — that is the Highlighter Set's job.
_Avoid_: style, skin, palette (a palette is derived from a Theme)

**Token**:
One named value of a Theme — a color such as the border or hover color, or a size or icon
used by the stylesheet. Every Theme sets every Token. A user can override Dark Tokens by
name in the settings, and add a stylesheet of their own on top.
_Avoid_: variable, constant, design value

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

### Plugins

**Plugin Catalog**:
Which plugins are installed: the metadata read from the `plugin.json` manifests found in
the plugin directories, one entry per plugin id. The catalog never loads a plugin library;
it is what the plugin dialog and the welcome dashboard list, and where the Plugin Host
looks a plugin up by id.
_Avoid_: plugin manager, plugin registry, plugin list

**Plugin Host**:
Loads and initialises the plugins the Plugin Catalog lists, shuts them down again, and
answers what a loaded plugin calls back: its data-source stream, its converter, the active
file, opening files and notifications. What a plugin shows goes through the Plugin UI Port.
A plugin is loaded when the host has initialised it, and enabled when the configuration
says to load it.
_Avoid_: plugin manager, plugin loader (the loader only opens one library)

**Application Plugins**:
The one Plugin Catalog and the one Plugin Host of the application, shared by every window.
The plugins are discovered and loaded once, after the first window is on screen; a window
opened later neither rescans nor loads them again. What depends on a loaded plugin, such as
opening a Log File a converter plugin handles, waits until they have loaded.
_Avoid_: plugin registry, per-window plugins

**Plugin UI Port**:
Everything the plugin layer needs from the user interface to show what a plugin
contributes — status widgets, sidebar tabs, footer widgets, menu actions and the parent
of its configuration dialog. The plugin layer calls it and knows no widgets; the main
window implements it. Every contribution belongs to one plugin, and all of them are
taken away again when that plugin is unloaded.
_Avoid_: plugin UI bridge, widget signals, UI host

### Session and settings

**Settings Policy**:
The small set of settings one part of the application actually needs, taken as a snapshot
and handed to it when it is built — an Indexing Policy, a Search Policy, a Watch Policy, a
File Access Policy, a Recognition Policy, a Decoding Policy, a Decoration Policy, a
Presentation Policy, a QuickFind Policy. A part that holds a Policy cannot reach for a
setting it did not declare.

How that is held differs by half. An engine library that consumes a Policy links the
Policy types and not the settings store, so reaching for an undeclared setting there is a
link error. The widget layer cannot be held that way — the Options Dialog, the Theme
wiring, the Shortcuts and the Highlighter Set collection all live in it and all
legitimately need the store — so its half is held by a build-time check that `ctest` runs:
only an allowlisted file, each entry carrying its reason, may name the settings store, and
the check names every other one that does. The allowlist is meant to shrink as the
remaining Axes get Policies, but not to zero. Besides the writers (the Options Dialog among
them) and window chrome with one consumer each, some Axes deliberately keep a direct read:
the Shortcuts, a keyed table of actions with a codec of its own rather than a flat snapshot,
registered by each widget that owns them; logging, which configures the process's logger
outside the lifetime of any Log File; follow-file-on-load, which the main window alone reads
once as a file is opened; the font, assembled in one place by the Crawler Widget and handed
to its own views; and SSL peer verification, one value read by the version checker alone.
_Avoid_: config object, options, preferences

**Decoration Policy**:
The Settings Policy that coloring Log Lines needs: whether what the main Search matched is
colored at all, whether each distinct match gets a shade of its own, and the backgrounds a
main-search and a QuickFind match are painted in. Held by the one module that builds a
Decoration's sources, so that neither Presentation reads those settings for itself and that
module can be exercised without a settings store. The Highlighter Set, the Color Labels and
the QuickFind pattern are not part of it: they are the user's current coloring, not settings
this Policy carries.
_Avoid_: highlight settings, color config, theme (a Theme does not color Log Lines)

**Presentation Policy**:
The Settings Policy a Presentation needs to show and scroll a Log File: whether text is
wrapped, whether fast scrolling is on and by what multiplier, whether scrolling may engage
follow, whether a recognized Log Format opens as a Table View, whether line numbers are
drawn in the Text View and, separately, in the Filtered View, and whether the overview is
shown. The View menu's toggles for the last three write the setting and take the same
re-derive the Options Dialog does, so a toggle reaches every open Log File, not only the
active tab. What a Log Line is colored in is not part of it — that is the Decoration
Policy.
_Avoid_: view settings, display config, scroll options

**QuickFind Policy**:
The Settings Policy searching interactively needs: how a QuickFind pattern and a pattern
typed into the Search line are read, whether case is ignored, whether QuickFind is
incremental, and whether changing the pattern runs the Search. It also carries the state a
Search's button row starts in: whether case is ignored, whether the Search auto-refreshes,
and whether the pattern is read as a logical combination. Those are starting state, not live
state — they seed the buttons when a Log File is opened, and a Policy arriving later does not
set a button the user has since changed by hand. It carries how typed text is read, not how
a Search runs — that is the Search Policy. The QuickFind bar belongs to a window, not to a
Log File, so the window takes this Policy from its session: the same one whichever tab or
Filtered View is in front, and taken again whenever a setting changes.
_Avoid_: find settings, search options

**Axis**:
One Settings Policy, seen as the unit a change travels in. A changed setting is re-derived
into Policies and handed down one axis at a time, so changing a Highlighter Set does not
restart file watching, and changing the poll interval does not disturb a Search. Every change
travels through the Session: a writer only says that the settings, or the Highlighter Sets,
changed; the Session re-derives the Policies and hands each changed Axis to the file watcher,
every window and every open Log File, and tells every open Log File to read what has no
Policy — the font, the shortcuts — again. Bringing a tab to the front applies nothing.
_Avoid_: category, group, domain

**Session**:
The set of Log Files currently open, their tabs, and the position and view state restored
for each on the next start.
It builds the views of every Log File it opens in one call, from one value: the Open Log
File, the QuickFind pattern, the Policies, the saved Searches and the view state to restore,
if any — opening a Log File by hand and restoring it on start take the same path. After that
it hands the views only what changed, one change per open Log File, and asks for their view
state when it is saved.
_Avoid_: workspace, project, layout
