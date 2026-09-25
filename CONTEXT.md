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
One line of a Log File, addressed by its number in the file. Its text — what is displayed,
what a Search matches and what the grep CLI prints — is the line decoded, without its line
feed, a carriage return that ends it or a byte order mark that starts it.
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

**Load Rule**:
What a load, a change on disk and a reload mean for an Open Log File, decided in one place and without reading the Log File: whether the load that finishes brings only lines that were added, whether the Marks are cleared and the Log Format is recognized again, whether the Marks saved with the Session are applied, and whether a Search waiting for the first load runs now. Whether a Search continues or starts again after a truncation it asks the Search's auto-refresh, which keeps deciding that. The Open Log File carries out what it decides.
_Avoid_: follow rule (follow is the view following the end of the Log File), change tracker

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
The Open Log File settles it after every load and whenever one is chosen: the one chosen, else
the one detected, else the locale's. An Encoding the settings force is chosen from the start.
The engine names one by a `TextEncoding`, an interned, immutable value found by name or IANA MIB enum;
null means none chosen or unknown. It wraps Qt 6's `QStringConverter`; the engine has no
`QTextCodec` and links no Qt5Compat.

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
view added later starts with all of it. Which Search is current reaches every Presentation,
the Overview and that Search's Filtered View through it too, as the Kept Searches make one
current, and so does the pattern of the current Search; a kept Search's Filtered View keeps
coloring the pattern it ran with.
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

**Background Run**:
The one way a Search (and, later, an index job) runs off the UI thread: one run at a time
on a thread of its own. Starting a run supersedes the one in flight; each run gets a copy
of the Policy taken as it starts, keeps the Log File's reader attached for as long as it
lasts, and is reported finished exactly once — returned, superseded or failed. Shutting
it down stops the run in flight and reports nothing more. The job it runs is plain code;
which run supersedes which is still up to whoever starts them (the Search Session).
_Avoid_: background task, async job

**Search Session**:
The owner of everything whose correctness depends on the ordering of a Search: the current
pattern, the run in flight, its results, its progress and its cached results. A new request
supersedes the one in flight rather than waiting for it.
_Avoid_: search manager, search controller, search engine

**Kept Searches**:
The owner of every Search of one Log File, each shown in a Filtered View of its own: the
current Search, which runs, follows the Log File and takes the Marks, and those whose results
the user kept to start another. A Search is added, made current and dropped there alone.
Making one current tells the Open Log File and hands it to the View Set, so no view is left
showing the Marks and Matches of another; only the current Search's progress is reported. A
Search dropped goes with its Filtered View, and a Log File always keeps one.
_Avoid_: search tabs, filtered views data

**Filtered View**:
The lower pane, showing only the Log Lines a Search selected. Its selection, Marks and
Search Limits are Log Lines like the main view's; only its Scroll Position counts places
among the Log Lines it shows.
_Avoid_: results pane, filter window

**Search Line**:
The line above the Filtered View where a Search is typed: its pattern, the buttons that say how the pattern is read (case, regular expression, inverse, logical combination, auto-refresh), and what it says about the Search that runs — progress, the Matches found, an error in the pattern, a truncated Log File. Adding a word to the Search, excluding one or combining Predefined Filters edits its pattern. The search history offered while typing is not part of it.
_Avoid_: search bar, search box

**QuickFind**:
Interactive incremental search within the currently displayed lines. Distinct from Search:
it navigates, it does not filter.
_Avoid_: find, incremental search

**Predefined Filter**:
A saved, named search pattern the user can apply without retyping it.
_Avoid_: saved search, bookmark

**Filter Group**:
A named group of Predefined Filters, the counterpart of a Highlighter Set. The non-deletable
Default Filter Group always exists and carries the same id for every user. A Filter Group, like
a Highlighter Set, is handed to someone else as a file of its own: the Group Exchange proposes
its file name from the group's name and writes exactly that one group. Import reads every
group of a file as a group of its own. A group of the same id is a conflict, and so is one of
only the same name; the user answers Replace (the existing group keeps its position and id),
Keep both (the imported group gets a fresh id and the first free name `<name> (n)`) or Skip,
once or for all remaining conflicts of the import. An imported group with the Default Filter
Group's id never replaces the recipient's Default group: it arrives as a new group.
_Avoid_: filter set, filter list, folder

**Team Folder**:
A Git repository a team shares its Filter Groups and Highlighter Sets through. LogSquirl clones
it into its own data folder with the installed `git` and keeps it current: at startup, every
five minutes and on "Sync now", never blocking the user interface. It is the only part of the
application that runs Git, and Git's own authentication applies unchanged. Turning it off, or
pointing it at another repository, leaves the user's own groups alone.
_Avoid_: shared folder, team repository, sync folder

**Team group**:
A Filter Group or Highlighter Set that lives in the Team Folder, one file each. Team groups
show in their own section of the dialogs, sorted alphabetically, and are never written into the
user's own settings. Changing a Team group and pressing OK or Apply publishes that one file
to the team; when someone else changed the same group meanwhile the user chooses keep mine,
take theirs or save mine as a copy. Nothing is locked (ADR-0008). A personal group is shared
as a Team copy, a Team group is copied back into the personal groups, each with a fresh id.
_Avoid_: shared group, remote group

**Search Limits**:
An optional line range a Search is restricted to. Lines outside it are shown but visually
subdued. Half-open everywhere: from the first Log Line searched up to, not including, its
end — the Log Line after the last one searched. No Presentation converts the end.
They can be given as a time range, or as N minutes around the current Log Line: the times
are converted to Log Lines once, where the Limits are decided (the start is the first Log
Line with a Timestamp at or after the start time, the end the first at or after the end
time), and from then on they are ordinary line Limits. They do not follow the Log File as it
grows or is reloaded.
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
Shorter-lived and more ad hoc than a Highlighter. There are nine of them, and their colors
follow the Theme: a Color Label whose colors are a built-in Theme's takes the colors of the
Theme applied, one the user colored keeps them (ADR-0006).
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
The look of the application around the Log Lines: Light, Dark, High Contrast, Smyck or Smyck
Light, or System, which becomes Light or Dark from the operating system's color scheme and
follows it while the application runs. Choosing a Theme takes effect at once, in every open
window. A Theme is exactly one set of Tokens, and it carries the colors of the Color Labels;
the application's palette and stylesheet are both derived from it. Beyond the Color Labels a
Theme does not color Log Lines — Highlighters and Highlighter Sets are the user's alone.
_Avoid_: style, skin, palette (a palette is derived from a Theme)

**Token**:
One named value of a Theme — a color such as the border or hover color, or a size or icon
used by the stylesheet. Every Theme sets every Token; the colors of the Color Labels are not
Tokens, because they color Log Lines rather than the application around them. A user can override Dark Tokens by
name in the settings, and add a stylesheet of their own on top.
_Avoid_: variable, constant, design value

### Structure

**Log Format**:
A description of how a Log Line is composed of named fields, used to present the file as
columns. Either built in or supplied by the user; which one applies to a Log File is decided
by Format Recognition. A Log Format is of one kind: **regex**, whose fields are the named
capture groups of its patterns, **JSON** (`"file-type": "json"`), for Log Files whose Log
Lines are JSON objects and whose fields are members addressed by path (`src/file`), or
**logfmt** (`"file-type": "logfmt"`, our own extension of the lnav schema), for Log Files
whose Log Lines are key/value pairs (`time=... level=info msg="started"`) and whose fields
are the declared keys, in any order; an undeclared key is ignored, a missing one is an empty
cell.
_Avoid_: schema, parser, layout

**Log Format Catalog**:
Every Log Format available to choose from, built in and supplied by the user. A user's
Log Format replaces a built-in one of the same name. One Catalog serves the whole application.
_Avoid_: registry, library

**Format Recognition**:
The decision which Log Format, if any, applies to a Log File, taken from its first Log
Lines against the Log Format Catalog. Taken when a Log File has loaded, and again after it
is reloaded or truncated; in between, the Log File keeps the Log Format it was recognized
with, even when the Catalog changes. The kinds of Log Format are scored apart: a sample Log
Line that is a JSON object counts only for JSON Log Formats, every other one only for regex
Log Formats. A logfmt Log Format counts a Log Line that reads completely as key/value pairs
and holds its timestamp field as a key; it never wins over a regex or JSON Log Format that
would have been recognized.
_Avoid_: detection, sniffing

**Timestamp**:
The point in time a Log Line carries, read through its Log Format's timestamp field
(its timestamp format, and for epoch values its divisor). Continuation lines, such as a
stack trace, have none. A written offset (`+02:00`, `Z`) makes it the UTC instant it names;
one without a time zone is taken as written, and a year-less one gets its year from the Log
File's modification date (ADR-0010). Only what a Log Format declares or a common format covers can be
read; a Log File without a Log Format that has a timestamp field has no Timestamps.
_Avoid_: date, time (both name only a part of it)

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

**Value Count**:
How often each value of one Log Format field, or of one capture group of the Search,
occurs: value, count and share, most frequent first. A snapshot taken on request, not
followed as the Log File grows.
_Avoid_: summary, statistics, histogram

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
of its configuration dialog. The plugin layer calls it and knows no widgets; every main
window implements it. The Application Plugins hand each contribution on to every window: a
menu action shows in all of them, a widget, which exists once, in the most recently active
window, and it moves to another window when that one closes. Every contribution belongs to
one plugin, and all of them are taken away again when that plugin is unloaded.
_Avoid_: plugin UI bridge, widget signals, UI host

### Updates

**Update Feed**:
`latest.json` on master: the latest stable and beta release, the build each was published
from, every release name and its notes. The release workflow proposes a change to it through
a pull request; the application downloads it to decide whether to announce a newer release.
_Avoid_: version file, update manifest, metadata

**Release Page**:
The GitHub page of one LogSquirl release, below
`https://github.com/64x-lunicorn/LogSquirl/releases/`. It is where a user downloads a
release, its attestations and its signed checksum file.
_Avoid_: download link, release URL

**Update Offer**:
The newer release the update check announces to the user, with the notes of the releases
they skip. A release is only offered when the Update Feed points at its Release Page.
_Avoid_: update notification, new version

**Install Source**:
How the running LogSquirl was installed, as far as the Update Offer cares: Homebrew cask, the
LogSquirl APT repository, the LogSquirl DNF repository, or unknown. It is decided at run time
from what is on the machine, never at build time, because a cask and a dragged DMG (or a
repository and a hand-installed package) are the same bytes. Only positive evidence counts;
for an unknown source, and for every AppImage and Windows build, the Update Offer links to
its Release Page, and for a known one it names the package manager's upgrade command.
_Avoid_: distribution channel

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
to its own views; and SSL peer verification, one value read by the downloader alone, which
fetches a new release; the version checker does not read it.
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
