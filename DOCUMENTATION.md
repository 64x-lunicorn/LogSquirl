# LogSquirl documentation

## Table of Contents

1. [Getting started](#Getting-started)
   - [Installing](#installing)
   - [The Dashboard](#the-dashboard)
1. [Exploring log files](#Exploring-log-files)
   - [Auto Log Format Detection (Table View)](#auto-log-format-detection-table-view)
   - [Chart Panel](#chart-panel)
   - [Tab groups](#tab-groups)
   - [Session files](#session-files)
1. [The menu bar](#the-menu-bar)
1. [Plugins](#Plugins)
1. [Settings](#Settings)
1. [Keyboard commands](#Keyboard-commands)
1. [Command line options](#Command-line-options)
1. [The command line tool](#The-command-line-tool)


## Getting started

*logsquirl* can be started from the command line, optionally passing the
file to open as an argument, or via the desktop environment's menu or
file association. If no file name is passed, *logsquirl* will initially open
the most recent file.

The main window is divided into three parts: the top displays the log
file. The bottom part, called the "filtered view", displays the results of
the search. The line separating the two contains the regular expression
used as a filter.

Entering a new regular expression or a simple search term will update
the bottom view, displaying the results of the search. The lines
matching the search criteria are listed in order in the results, and are
marked with a red circle in both windows.

### Installing

Download the package for your platform from the
[release page](https://github.com/64x-lunicorn/LogSquirl/releases/latest):
the NSIS installer for Windows, the DMG for macOS (Apple Silicon, macOS 15 or
later) or an AppImage, DEB or RPM for Linux. Two package managers keep
*logsquirl* up to date for you:

- On a Mac, with [Homebrew](https://brew.sh/):
  `brew install --cask 64x-lunicorn/tap/logsquirl`, then `brew upgrade`.
- On Ubuntu 24.04 (amd64), with the LogSquirl APT repository, which holds the
  last three stable releases and no betas, then `apt upgrade`. The commands
  that add the repository are in the
  [README](https://github.com/64x-lunicorn/LogSquirl#readme).

On Windows, administrators can deploy the installer without any dialog, for
example wrapped into an `.intunewin` package for Microsoft Intune:

- `logsquirl-win-x64-setup.exe /S` installs for all users of the machine into
  `C:\Program Files\logsquirl`, with the Start menu shortcut and without the
  `.log` association, and exits with 0. `/D=C:\Some Dir` as the last argument,
  without quotes even with spaces, installs into another directory. Run over an
  older version, it upgrades that in place.
- It needs administrator rights, which the SYSTEM account Intune installs with
  has. Started without them, Windows asks for elevation or refuses to start it;
  it never installs half.
- The installed version is the `DisplayVersion` value of
  `HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl`.
  The installer is a 32-bit program, so its key is in the 32-bit view of the
  registry: in an Intune detection rule, choose *Associated with a 32-bit app
  on 64-bit clients*.
- `"C:\Program Files\logsquirl\Uninstall.exe" /S` uninstalls silently. It
  returns at once and finishes in the background within a few seconds.
- The *Send to* shortcut goes to the profile of the account that installs, so
  after an install as SYSTEM, users have no *Send to* entry for *logsquirl*.

CI checks all of this, except the elevation prompt itself, on every build of
the installer.

For Windows there is also a portable package, `logsquirl-win-x64-portable.zip`:
unpack it anywhere and start `logsquirl_portable.exe`. It keeps everything it
stores beside the executable instead of in the user profile; any *logsquirl*
does so when it finds a `logsquirl.conf` beside itself:

| Beside the executable      | What it holds                                    |
|----------------------------|--------------------------------------------------|
| `logsquirl.conf`           | the settings                                     |
| `logsquirl_session.conf`   | the session                                      |
| `formats\`                 | your own log formats                             |
| `plugins\`                 | the plugins it comes with and those you install  |
| `plugin_config\`           | the plugins' own configuration                   |
| `teamfolder\`              | the Team Folder's clone of the team's repository |
| `themes\`                  | your own theme stylesheets                       |
| `logsquirl_dump\`          | crash dumps                                      |
| `logsquirl_taken_over.txt` | what it took over from an earlier package        |

Earlier portable packages kept your log formats, plugins, plugin configuration,
Team Folder and themes in `%APPDATA%\logsquirl_portable\` and
`%LOCALAPPDATA%\logsquirl_portable\`. On its first start, a portable
*logsquirl* with none of that beside its executable yet copies what it finds
there beside itself, once, and notes it in `logsquirl_taken_over.txt`. A plugin
it comes with is kept rather than replaced by the older copy. The old folders
are left as they are; delete them once you no longer need them. Nothing is
copied when your own formats, plugin configuration, Team Folder or themes
already sit beside the executable.

Only the index cache stays in the user profile, and a log file written with
logging turned on goes to the temporary folder. Unpack the package where you
may write, not under `Program Files`. Since the plugins you install share the
`plugins` folder with those it comes with, updating one from the catalog
replaces the copy it came with.

### The Dashboard

When *Show dashboard on startup* is enabled (`Settings->General`, it is by
default), the first tab is the Dashboard, which cannot be closed. It shows the
buttons *Open File* and *Load Session*, cards for your recent files, your
favorites and the status of your plugins, and the hint that log files can be
dropped on it to open them. Selecting a file in a card opens it.

## Exploring log files

Regular expressions are a powerful way to extract the information you
want from the log file. *logsquirl* uses *extended regular
expressions*.

One of the most useful regexp features when exploring logs is the
*alternation* feature, using parentheses and the | operator. It searches for
several alternatives and displays several line types in the
filtered window, in the same order they appear in the log file.

For example, to verify that every connection opened is also closed, one
can use an expression similar to:

`Entering (Open|Close)Connection`

Any 'open' call without a matching 'close' will immediately stand out
in the filtered window. The alternation also works with the whole search
line. If you would like to know what kind of connection has
been opened:

`Entering (Open|Close)Connection|Created a .* connection`

`.*` will match any sequence of characters on a single line, but *logsquirl*
will only display lines with a space and the word `connection` somewhere
after `Created a`

Sometimes alternation using regular expression syntax is cumbersome.
For such cases *logsquirl* can do logical search pattern combinations using
`and`, `or`, and `not` operators. This mode can be enabled using button
from search input panel. In this mode all patterns must be enclosed in `"`.
Following logic operations are supported:

|Operator        |Actions                                                                   |
|----------------|--------------------------------------------------------------------------|
|`and`           |Logical AND, True only if x and y both match input line. (eg: `"x" and "y"`)|
|`or`            |Logical OR, True if either x or y match input line. (eg: `"x" or "y"`)      |
|`&`             |Similar to AND but with left to right expression short circuiting optimization  |
|`\|`             |Similar to OR but with left to right expression short circuiting optimization   |
|`not`           |Logical NOT, Negate the logical sense of the input. Input must be enclosed in `()` (eg: `not("x")`)|

#### The search bar

The buttons left of the search line switch, for the next search:

- *Match case*, whether upper and lower case must match (`4`);
- *Use regex*, a regular expression or the text as written (`5`); it starts
  as `Settings->General` says;
- *Inverse match*, which shows the lines that do *not* match (`6`);
- the logical combining described above (`7`);
- *Auto-refresh*, which runs the search again as the file grows (`8`).

*Search* runs it, and the stop button, shown while a search runs, cancels it.
*Keep Results* (`9`) keeps the current results: the next search opens its
results in a new tab of the filtered view, beside the kept ones, so several
searches of one file can be compared. The keys are the defaults and can be
changed in the shortcut settings.

*logsquirl* keeps track of used search patterns and provides autocomplete
for them. This history can be edited or cleared from the search text box context menu.
Autocomplete is case-sensitive if this option is selected for matching 
regular expressions. The size of autocomplete history is configured in general options.

In addition to the filtered window, the match overview on the right-hand
side of the screen offers a view of the position of matches in the log
file. Matches are shown as small red lines.

In addition to regexp matches, *logsquirl* enables its users to mark any
interesting line in the log. To do this, click on the round bullet in
the left margin in front of the line that needs to be marked. Or, select
the line and press the `'m'` hotkey.
To mark several lines at once select them and use the `'m'` hotkey or context menu.

By default, the filtered view shows the marked lines as well as the matches.
The list at the left of the search line switches it between *Marks and
matches*, *Marks, matches + breadcrumbs*, *Matches + breadcrumbs*, *Marks* and
*Matches*; `v` and `Shift+V` go through them, and `1`, `2` and `3` pick
*Marks and matches*, *Marks* and *Matches*.

Marks also appear as blue lines in the match overview.

It is possible to quickly jump to a specific line using `Ctrl+L` shortcut.

#### Breadcrumbs

The breadcrumbs are the lines around each match and mark, shown dimmed in the
filtered view, so that it shows what happened around a match without going back
to the main view: 5 lines before and after by default. *Context lines around
matches* in `Settings->View` sets the number, and 0 turns them off. Where the
lines around two matches overlap, each line is shown once.

#### Exporting the filtered view as CSV

When the log file's format was recognized (see
[Auto Log Format Detection](#auto-log-format-detection-table-view)), the
filtered view's context menu offers **Export as CSV...** too. It writes the
lines the filtered view shows, split into the columns of the table view, with
the same dialog, file format and background export as
[the table view's export](#exporting-as-csv). Without a recognized format the
entry is absent. The dialog differs in these points:

- **Rows**: *All shown lines*, in the filtered view's order under its current
  mode (matches, marks or both), or *Selected lines*, the selected ones in
  that order.
- **Include Context Lines**: whether the context lines around matches and
  marks, the [breadcrumbs](#breadcrumbs), are written as well. It is off at
  first, and can only be checked while the filtered view shows them;
  unchecked, only matches and marks are written.
- **Columns**: *Line* and **Type** (both unchecked at first), then every
  column of the table view, **Δt** included. *Type* holds `Match`, `Mark`,
  `Match+Mark` or `Context`, always in English, so a spreadsheet can filter
  on it in any language.

Every value is what the table view shows for that line: **Δt** is the time
since the nearest earlier line of the log file with a timestamp, not since the
previous line exported.

#### QuickFind

QuickFind searches the view you are in, the main view or the filtered view,
without changing the search results, and jumps from one occurrence to the next.
`Ctrl+F` opens its bar, and so do `'` and `"`, which search forward and
backward. The bar has the text to find, *Ignore case*, and *Previous* and *Next*;
`Enter` finds the next occurrence, `Esc` closes the bar. `F3` and `n` find the
next occurrence, `Shift+F3` and `N` the previous one (`Cmd+G` and
`Cmd+Shift+G` on macOS). With incremental QuickFind (`Settings->General`, on by
default) it searches as you type.

#### Go to timestamp

`Edit->Go to timestamp...` (`Ctrl+Shift+L`, configurable) jumps to a point in
time instead of a line number. Type a time such as `14:02` or `14:02:30.250`,
optionally after a date (`2026-09-23 14:02`); without a date, the date of the
line you are at is used. The view goes to the first line whose timestamp is at
or after that time. If the time is before the first or after the last
timestamp, it goes to the first or last line and tells you so.

The action needs a Log File with a recognized Log Format that has a timestamp
field (see [Auto Log Format Detection](#auto-log-format-detection-table-view));
otherwise it is disabled and its tooltip says why. Lines without a timestamp,
such as stack traces, are skipped. A time zone offset written in the log
(`+02:00`, `Z`) is honoured: the timestamp counts as the instant it names, in
UTC. A timestamp without one is compared as written, so type the time as it
appears in the file. A timestamp without a year (syslog) takes the year of the
file's modification date, or the year before when its month and day lie later
in the year. The search assumes the file is in time order; if it is not, the
line found is only approximate, and the status bar says so when the timestamps
around it are out of order.

#### Search limits by time

Two more `Edit` actions set the search limits from times instead of from log
lines, for when you know the window of an incident but not its first and last
line:

- `Set search limits to time range...` (`Ctrl+Alt+T`, configurable) asks for a
  start and an end time, typed as for *Go to timestamp*. The search is limited
  to the lines from the first one at or after the start up to, but not
  including, the first one at or after the end.
- `Set search limits around current line...` (`Ctrl+Alt+W`, configurable)
  limits the search to N minutes before and after the line you are at. You
  choose N; it is remembered for the next time.

The times are turned into line numbers once, when you set the limits. From then
on they are ordinary search limits, exactly as if you had set them by hand
with *Set search start* and *Set search end*: they do not move when the file
grows or is reloaded, and *Clear search limits* removes them. Lines without a
timestamp, such as stack traces, stay with the line before them. If the time
range lies entirely before or after the log file, or holds no line, you are
told so and the limits stay as they were. Like *Go to timestamp*, the actions
need a Log File with a recognized Log Format that has a timestamp field and are
disabled, with a tooltip saying why, otherwise.

*logsquirl* uses Hyperscan library to perform regular expressions search. Hyperscan is very
fast, but it doesn't support some patterns, most notably any lookahead is not supported 
(check [hyperscan documentation](https://intel.github.io/hyperscan/dev-reference/compilation.html#pattern-support) for 
supported syntax). To overcome this *logsquirl* will switch to Qt regular expression engine with full PCRE syntax support
if Hyperscan can't handle the search pattern. However, in this case search will be significantly slower.

### Opening files

*logsquirl* provides several options for opening files:

* using dedicated open file item in `File` menu or toolbar
* dragging files from the file manager
* downloading files from a provided url
* providing one or many files via the command line
* piping a stream into `logsquirl -` (see [Reading standard input](#Reading-standard-input))
* running a command and following its output (see [Command output](#Command-output))
* using recent files or favorite menu items.

On Windows, the installer adds *logsquirl* to the *Open with* menu of the file manager, and makes it
the program that opens `.log` files when its component *Associate with .log files* is selected, which it
is not by default. On Mac OS, the *logsquirl* installer configures the operating system to open `.log`
files by clicking them in the file manager.

#### Reading standard input

`logsquirl -` opens what arrives on standard input, for example
`journalctl -f | logsquirl -`. It can be combined with files
(`… | logsquirl - other.log`): they open in the same window.

* When *logsquirl* is already running, what is piped in opens as a `stdin` tab
  in the running window, with the files given beside it. The `logsquirl`
  process at the end of the pipe keeps reading until the pipe closes, or its
  tab is closed, and then exits; `Ctrl+C` ends it early, and the tab keeps what
  arrived until then. Every `… | logsquirl -` opens a tab of its own. If the
  running *logsquirl* cannot be reached, or does not take standard input over
  within 5 s (one of another version does not), it says so, exits with a
  non-zero status and leaves nothing behind. With `--multi`, `-` opens in a
  window of its own instead.
* What arrives is kept in a temporary file in the application's temporary
  directory, and the tab (named `stdin`) follows it like any growing Log File:
  Search and Marks work as they do for a file on disk.
* The temporary file grows without bound for as long as the stream runs. It is
  removed when its tab or the window closes, or *logsquirl* exits; the data is
  not kept and there is no "save as" for it. On Windows, a file the piping
  process still writes is removed when *logsquirl* exits.
* When the writing end closes, following stops, every received byte is in the
  Log File (including a last line without a trailing newline) and the status bar
  says `Standard input closed`.
* `logsquirl -` started from a terminal with nothing piped in prints an error
  and exits with a non-zero status, without opening a window.
* Windows: reading standard input redirected into the GUI executable has not
  been verified. If it cannot be read, the window stays empty.

#### Command output

`File->Open Command Output...` runs a command and opens what it writes in a new
tab that follows it, like standard input. This covers remote and container logs
without anything else to install: `ssh host tail -f /var/log/syslog`,
`docker logs -f web`, `kubectl logs -f deploy/api`, `journalctl -f`.

* The dialog asks for the **Command** line, the **Working folder** it runs in
  (empty: your home folder) and whether to **Include standard error** (on by
  default). The last 10 commands are offered in the command's list, the most
  recent first; choosing one fills in its working folder and standard error
  choice too.
* The command line runs through your shell, as in a terminal, so pipes and
  quoting work (`kubectl logs -f api | grep ERROR`): on macOS and Linux as a
  login shell (`$SHELL -l -c "…"`, `/bin/sh` when `SHELL` is not set), so that
  the programs of your `PATH` are found even when *logsquirl* was started from
  the Finder or the Dock; on Windows through `cmd.exe /d /s /c "…"`.
* The tab is named after the command line, shortened in the middle to 40
  characters; its tooltip shows the whole command line, the working folder and
  the temporary file the output is kept in.
* On Windows, console programs such as `dir` write in the OEM code page of the
  console (CP850 on a German Windows, CP437 on a US one). The tab reads the
  output as UTF-8 when it is valid UTF-8, otherwise in that code page; the
  first output with more than plain ASCII decides. `Encoding` in the menu
  changes it as for any file.
* When the command ends, the tab stays with everything it received and is no
  longer followed: the file does not grow any more. Its name gets ` [exit N]`
  with the command's exit code, or ` [stopped]` when it was killed by a signal
  or crashed, and the status bar says so. Exit code 127 (9009 on Windows) is a
  command the shell did not find: the status bar says *command not found*.
* A working folder that does not exist, or a shell that cannot be started, is
  reported and no tab opens.
* Closing the tab, its window or *logsquirl* stops the command and every
  process it started: on macOS and Linux they get `SIGTERM`, and `SIGKILL` 2 s
  later if they are still there; on Windows they are ended together. Whatever
  the command left running is stopped when it ends, too. One tab is one run:
  run the command again from the dialog.
* The tab is not saved with the session: a start never runs a command by
  itself. Its temporary file is removed when the tab closes.
* The command does not run in a terminal, and many programs then keep their
  output in a buffer instead of writing each line at once (Python, for
  example): lines arrive late and in bursts. Ask the program to write each
  line: `python -u script.py`, `stdbuf -oL some-tool` (Linux),
  `grep --line-buffered ERROR`.
* Any number of commands and standard input can be followed side by side in
  one window. `File->Open Command Output...` can be given a key in
  `Settings->Shortcuts`, and it is in the command palette.

#### Archives

*logsquirl* can open archives (`zip`, `7z`, and `tar`). The archive is extracted
to a temporary directory and standard open file dialog is presented to
select files. The type of archive is determined automatically by file
content or extension.

*logsquirl* can open compressed files (`gzip`, `bzip2`, `xz`, `lzma`). Such files are
decompressed to a temporary folder and then opened. The compression type is
determined automatically by file content or extension.

A file opened from an archive or a compressed file comes back with the session:
on the next start *logsquirl* extracts the archive again, without asking, and
opens the same file where it stood. If the archive is gone by then, its tab is
left out. Its tab keeps the name you gave it and its tab group, and the recent
files list the archive, which asks for the file again when you open it from
there.

#### Remote URLs

*logsquirl* can open files from remote URLs. In that case, *logsquirl* will
download the file to a temporary directory and open it from there.
A downloaded file is not restored with the session: a start never downloads
anything you did not ask for.

#### Recent files

*logsquirl* saves a history of recent opened files, available from the `File`
menu: 5 by default, up to 25 as set in `Settings->File`. Standard input, a
command's output, a merged tab, what a data source writes and text opened from
the clipboard exist only while *logsquirl* runs, so they are not added to it.

#### Favorites

Opened files can be added to the `Favorites` menu either from
`Favorites->Add to Favorites` or from the toolbar.

This menu is used to provide fast access to files that are opened less
often and don't end up in the recent files section.

#### Clipboard

Pasting text from the clipboard to *logsquirl* also works. In this case, *logsquirl*
will save pasted text to a temporary file and open that file for
exploring.

#### Switching between opened files

Switching from one opened file to another can be done from the
`View->Opened files` menu or by using the `Ctrl+Shift+O` shortcut 
which displays special dialogue to choose between opened files.

### Encodings

*logsquirl* tries to guess the encoding of an opened file. If that guess happens to
be wrong, then the desired encoding can be selected from the `Encoding` menu.

### Predefined filters

If some search patterns are used very often they can be saved as predefined filters.
Predefined filters are configured from the `Tools` menu.

A predefined filter has a name, a pattern and a setting to treat the pattern as
a regular expression or as plain text. The filters are listed in the Filters
tab of the [Filters Panel](#filters-panel): checking filters there makes the
checked ones the search pattern, as alternatives of each other, and
double-clicking a filter or a group checks only that one.

It is possible to save the current search pattern as a predefined filter with
*Save as Filter* from the search input context menu.

Predefined filters are kept in filter groups. The dialog lists the groups
(*New Filter Group*, *Delete Filter Group*, *Move Group Up* and *Move Group Down*);
the Default group always exists and cannot be deleted. Groups are exchanged
the same way as highlighter sets: *Export* writes the selected group only, to a
file named `<name>_filter.conf` by default, and *Import* reads every group of
the selected files, asking *Replace*, *Keep both* or *Skip* when a group of the
same id or name exists (see [Using highlighters](#using-highlighters)). A group
that carries the id of the Default group never replaces your Default group; it
arrives as a group of its own.

### Importing filters from Chipmunk

*logsquirl* can import filters and highlighters from Chipmunk JSON export files.
This is available from the `Tools` menu. The imported filters are converted to
*logsquirl* predefined filters and highlighter sets.

### Using highlighters

*Highlighters* can colorize some lines of the log being displayed
to draw attention to lines indicating an error, or to associate
a color with a certain type of event. 

Highlighters are grouped into sets. Several sets can be active at once; they
are switched on and off in the context menu or the `Highlighters` menu, where
*None* switches them all off.

Any number of highlighters can be defined in a single set.
Highlighter configuration includes a regular expression to match
as well as color options. Another option is to use plain text patterns
in cases when complex regular expression are unnecessary.
Highlighters don't have support for logical search pattern combinations.

Each highlighter can be configured to apply foreground and 
background colors either to the whole line that matched its regular
expression or only to matching parts of the line. In the latter case,
if the regular expression contains capture groups then only the captured
parts of the matching line are highlighted.

It is possible to set a color variance. In that case different strings
that match the same regular expression will have slightly different color.

Any number of highlighters set can be applied to opened file using either 
the context menu or the main menu.

The order of highlighters in the set and the order of sets in configuration is important.
For each line all highlighters are tried from bottom to top. Each new matching 
highlighter overrides colors for the current line. 

The highlighter editor offers 20 ready-made color pairs, 12 soft pastels with
dark text and 8 strong colors with white text; one click sets both the text
and the background color.

A highlighter set can be handed to someone else: select it in the
`Highlighters` dialog and use *Export*, which writes that one set to a file
and proposes a file name from the set's name (`<name>_highlighter.conf`). Keep
the `.conf` extension so that *logsquirl* can import the file. *Import* reads
every set in the selected files. Each set is identified by a unique id. When a
set with the same id, or just the same name, already exists you choose
*Replace* (the existing set keeps its position and id, so an active set stays
active), *Keep both* (the new set gets the first free name `<name> (n)`) or
*Skip*, for that set or for all remaining conflicts. A file that cannot be
read or holds no set is reported. Nothing takes effect before OK or Apply.

### Color labels

In addition to predefined highlighters sets it is possible to create quick highlight rules
from selected text. These are called color labels. By default, *logsquirl* has 9
color labels enabled. Adding color label to selected text is done either
via context menu or with shortcuts `Ctrl+Shift+1-9`. Any number of 
strings can be marked with a single color label. Also the is `Ctrl+D` shortcut
that applies the next color label to selected text.

To remove color label from selected text either select the text and use 
context menu to set color label to `None` or use `Ctrl+Shift+0` shortcut that
will remove all color labels.

The colors that are used for text highlight can be configured from the color labels
tab of highlighters configuration dialog.

### Auto Log Format Detection (Table View)

*logsquirl* can automatically detect the format of a log file and display it
in a structured **table view** with separate columns for each field (timestamp,
level, body, and any custom fields defined by the format).

#### Enabling the feature

Enable auto-detection in **Options → Log Formats → "Auto-detect log format
(table view)"**. The same tab shows a scrollable list of all available format
definitions (built-in and user-defined) and provides an "Open Formats Folder…"
button to quickly access the user formats directory.

When enabled, *logsquirl* samples the first lines of each
opened file and matches them against its library of format definitions. If a
format matches, a table-view toggle button appears in the toolbar. Click it
to switch between the classic text view and the table view.

#### The elapsed-time column

When the format has a timestamp field, the table view shows one more column,
**Δt**, right after the timestamp. It holds the time since the nearest earlier
log line that has a timestamp, for example `+0.004s`, `+12.3s`, `+5m02s`,
`+1h05m` or `+2d03h`; a negative value (lines out of time order) keeps its
`-`. Continuation lines such as stack traces are skipped when looking back,
and their own cell is empty, so the line after a trace is compared with the
one before it. The first timestamped line, and a line with no timestamped line
within the 100 lines before it, have an empty cell. Formats without a
timestamp field have no such column. The column is not free: on a
10-million-line file a page of scrolling takes about 5.3 ms instead of 4.7 ms.

#### Format definitions

Format definitions are JSON files compatible with the
[lnav](https://lnav.org/) log format specification. Each file describes one
or more formats with:

- **File type** — `"file-type": "json"` marks a format for log files whose
  lines are JSON objects (NDJSON, Bunyan, Pino). Its fields are the members
  named by the `value` definitions, one column each in that order; a name can
  address a nested member by path (`src/file`). A line that is not a JSON
  object still gets a row, with empty fields, and the text view keeps the raw
  line. No JSON format is shipped, bring your own. A format without it is a
  regex format.
- **Regex patterns** — for regex formats, named capture groups define the fields
  (e.g. `(?<timestamp>...)`, `(?<level>...)`, `(?<body>...)`).
- **Value definitions** — metadata for custom fields (kind, hidden flag,
  identifier flag).
- **Timestamp format** — strftime-style pattern for parsing the timestamp
  field. An epoch timestamp (`%s`) is divided by the format's
  `timestamp-divisor` to get seconds (for example 1000 for milliseconds).
- **Level mapping** — maps format-specific level strings to standard
  severity levels.

*logsquirl* ships with 22 built-in format definitions. Additional user-defined
formats can be placed in the platform data directory:

| Platform  | Path                                                     |
|-----------|----------------------------------------------------------|
| Linux     | `~/.local/share/logsquirl/formats/`                      |
| macOS     | `~/Library/Application Support/logsquirl/formats/`       |
| Windows   | `%APPDATA%/logsquirl/formats/`                           |

A portable *logsquirl* reads them from the `formats` folder beside its
executable instead (see [Installing](#installing)).

Format definitions are read when *logsquirl* starts and again whenever the
Options dialog is applied. A file that is already open keeps the format it
was recognized with; reload it (or reopen it) to have an added or edited
user format picked up.

#### Column order

Columns appear in the order their corresponding named capture groups are
defined in the format's regex pattern. This means the table layout matches
the structure of the original log line. If no explicit order can be
determined, columns fall back to alphabetical order.

#### Column sizing

Columns are auto-sized by measuring the actual text content of up to 2000
rows using the view's font metrics. This ensures that cell content — including
very long body messages — is never clipped. When the total column width is
narrower than the viewport, the last column stretches to fill the remaining
space. A horizontal scrollbar appears automatically when columns exceed the
viewport width.

#### Highlighting in the table view

The table view renders the same highlighting as the main text view:

- **Search matches and marks** — matched/marked rows receive tinted
  backgrounds.
- **Highlighter sets** — the active highlighter set is applied per cell,
  coloring matching text segments with the configured foreground and
  background colors.
- **Color labels** — quick highlight rules are rendered in each cell.
- **QuickFind** — interactive search results are highlighted in real time.

#### Non-matching lines

Lines that do not match the detected format's regex are displayed in the
**body** column with all other columns empty. This ensures no data is lost
in the table view.

#### Exporting as CSV

Right-click the table and choose **Export as CSV...** to write the table to a
file for a spreadsheet, pandas and the like. The main text view has no such
entry; the filtered view has one
([Exporting the filtered view as CSV](#exporting-the-filtered-view-as-csv)).
Without a recognized format there is no table view and no export. A dialog
chooses what is written:

- **Rows**: *All rows*, every row the table shows, in its order, or
  *Selected rows*, the selected ones in line order. *Selected rows* can only
  be chosen while rows are selected, and is then chosen at first.
- **Columns**: *Line*, the 1-based line number as *Copy with line numbers*
  writes it (unchecked at first), then every column of the table, **Δt**
  included (all checked). At least one column must be checked.
- **Separator**: *Comma*, *Semicolon* or *Tab*.
- **Write column names as the first row**: the header row.

**Export...** then asks for the file, proposing the log file's name with
`.csv` added, and adds `.csv` to a name without it, asking first when a file
of that name exists. The separator and the header row are remembered for the
next export; the rows and columns are not.

Every value is exactly what the table shows, the elapsed time and the raw
text of a non-matching line included. The file is UTF-8 with a byte order
mark, so that Excel reads accented characters correctly when the file is
double-clicked (pandas reads it with `encoding="utf-8-sig"`), and its lines
end with CR LF. A field is quoted with `"` when it holds the separator, a
`"`, or a line break, and a `"` inside it is doubled (RFC 4180).

The export runs in the background with a progress dialog. Cancelling it, or
a failed write, leaves an existing file as it was and creates no new one.

### Chart Panel

The Chart Panel lets you plot numeric values extracted from log lines using
regex capture groups.  Toggle it from the **View** menu or the toolbar.

#### Format-aware templates

When a log format is auto-detected the Chart Panel toolbar shows a
**Templates** button.  Clicking it opens a menu with pre-configured series
that can be added with a single click:

- **Log Level Distribution** — one count-mode series per known log level
  (error, warning, notice, …) with selectable time buckets (1 s, 5 s, 1 min).
- **Message Rate** — count all matching lines over time (per second, 5 s,
  10 s, or minute).
- **Numeric Fields** — extract integer or float fields defined in the
  format as value series.
- **Field Occurrence** — count-mode series for each non-hidden custom field.

All templates automatically configure the X-axis with the format's
timestamp regex and parse format, so you get a time-based chart without
any manual configuration.

The **"+ Add Series" dialog** also pre-fills the X-axis timestamp fields
when a format is detected, making manual series creation easier.

Templates work in both **text view and table view** — format detection
feeds the chart panel regardless of which view mode is active.  They
support all built-in and user-defined format definitions.

#### Manual series

You can still create custom series with the **+ Add Series** button, which
opens the *Chart Series* dialog:

- *Name*, shown in the series list of the toolbar and in the tooltip of a
  point.
- *Regex pattern (Y)* chooses the lines to plot, and *Capture group (Y)* the
  group whose number is the Y value. Capture group 0 counts: every matching
  line adds a point with Y = 1, as does a line whose group holds no number.
- *Color* of the series.

#### The X axis and time buckets

The X axis is the line number unless *X-Axis (custom)* is checked. Then a
second *Pattern* and *Capture group* take the X value from each line. With
*Parse as timestamp* and a *Format* in Qt's date and time tokens (`yyyy`, `MM`,
`dd`, `HH`, `mm`, `ss`, `zzz`, for example `yyyy-MM-dd HH:mm:ss.zzz`) the X value
is a time; without them, a number. A line whose X value cannot be read is left
out. The axis shows the times in UTC.

*Aggregate*, available with *Parse as timestamp*, groups the points into time
buckets of 100 ms, 500 ms, 1, 5, 10 or 30 seconds, or 1 or 5 minutes, and adds
up their Y values. With capture group 0 that is the number of matching lines per
bucket, for example errors per minute:

|Field                |Value                               |
|---------------------|------------------------------------|
|Regex pattern (Y)    |`ERROR`                             |
|Capture group (Y)    |0                                   |
|Pattern (X)          |`^(\d{2}:\d{2}:\d{2})`              |
|Capture group (X)    |1                                   |
|Parse as timestamp   |checked, format `HH:mm:ss`          |
|Aggregate            |1 minute                            |

#### Working with the chart

The mouse wheel zooms in and out around the pointer, dragging with the middle
or the right mouse button moves the view, and *Fit* shows all points again.
Hovering over a point shows its series, its line or time and its value;
clicking a point goes to its line in the log. *Edit* and *Remove* act on the
series chosen in the toolbar's list. `View->Show Filter Frequency` adds one
counting series for every alternative of the current search.

The series of a file, and whether the panel is shown, are kept in the session
with the file and restored with it.

#### Presets

*Save Preset* keeps the current series under a name, for every file; a preset
of the same name is replaced. *Load Preset* replaces the series of the chart
with those of a preset, and *Delete Preset* deletes one without asking.

*Export…* writes the current series to a JSON file and *Import…* adds the
series of such a file to the chart, so a set of series can be shared. The file
is a list of series:

```json
[
  {
    "name": "Errors per minute",
    "color": "#e53935",
    "pattern": "ERROR",
    "captureGroup": 0,
    "visible": true,
    "xPattern": "^(\\d{2}:\\d{2}:\\d{2})",
    "xCaptureGroup": 1,
    "xTimestampFormat": "HH:mm:ss",
    "bucketSizeMs": 60000
  }
]
```

The `x…` fields are the custom X axis and `bucketSizeMs` the bucket in
milliseconds. A field left out takes its default: capture group 1, blue, and
visible; `"matchCase": false` makes the pattern ignore case. An exported file
also gives each series an `id`; an imported series without one gets a new one.

### Browsing changing log files

*logsquirl* can display and search through logs while they are written to
a disk. This might be the case when debugging a running program or
server. The log is automatically updated when it grows, but the
'Auto-refresh' option must be enabled if you want the search results to
be automatically refreshed.

The `'f'` key may be used to follow the end of the file as it grows (a
la `tail -f`).

*logsquirl* detects if new lines have been appended to the file or if the file has
been overwritten. In the former case, search results will be updated as new
matching lines appear in the file. If the file is overwritten, then
search results will be cleared. 

*logsquirl* has two options to distinguish appends from overwrites.
The general and more stable option is to recalculate the hash of the 
indexed part of the file and check if it matches current file on disk. 
This is reliable but can be slow for large files and for slow file systems
(e.g. network shares). The other option is to check hashes for only the 
first and last parts of the file. This usually works quickly 
but can skip over changes in the middle of the file. You can choose your 
preferred option in `Settings->File` tab.

The following file mode requires monitoring of the file system for any changes.
If native monitoring or polling are both disabled in settings, then the 
following file mode is also disabled.

### Merging Log Files

Right-click a tab and choose *Merge…* (offered while at least two files are
open) to combine files into one merged tab. The dialog *Merge Log Files* lists
every open file -- standard input, another merged tab and command output
included -- in tab order, all checked. Uncheck the files to leave out, and put
the others in the order they are written in: drag a file within the list, or
select it and use *Move Up* and *Move Down*. *Drop duplicate lines* (off by
default) leaves out a line identical to one already written, from any of the
files. *Merge* is enabled while at least two files are checked; it opens the
merged tab, named "Merged" or "Merged (dedup)". The sources are written one
after the other, in the order of the dialog; lines are not sorted by time. The
dialog remembers nothing: it opens in the current tab order every time.

The merged tab follows its sources. When a source changes, the merged file is
rebuilt after a short pause (300 ms) and the tab reloads. The rebuild always
starts from what the sources contain at that moment: if a source is truncated
or overwritten, its old lines disappear from the merged tab and the lines of
the other sources stay. A source that is deleted contributes nothing until it
exists again and changes. The merged file is a temporary file, removed when
its tab or the window closes; it is not restored with the session.

### Tabs

Every file opens in a tab of its own. Its context menu closes it, the others,
those to its left or right, or all, copies the file's full path and opens its
folder. *Rename tab* gives the tab a name of your own instead of the file name;
the name belongs to the file's path and comes back whenever that file is opened,
until *Reset tab name*. The tab of a file that exists only while *logsquirl*
runs -- standard input, a command's output, a merged tab, a data source, the
clipboard -- keeps its name until it closes. The icon of a tab shows when its
file has new lines, and when those lines hold new matches.

`Ctrl+Tab` and `Ctrl+Shift+Tab` (or `Ctrl+PgDown` and `Ctrl+PgUp`) go to the
next and the previous tab, `Ctrl+1` to `Ctrl+8` to the first eight and `Ctrl+9`
to the last, and `Ctrl+W` closes the current one.

### Tab groups

Open tabs can be organized into named, colored groups. Right-click a tab and
choose `Add to Group` to put it into an existing group or into a `New Group...`
(you are asked for a name and a color). A grouped tab shows a colored bullet
before its name and its text is tinted in the group's color. `Remove from Group`
takes a tab out again. For a grouped tab the context menu also has a `Group:`
submenu with `Rename Group...`, `Change Group Color...`, `Close All in Group`
and `Ungroup All`.

`Tools->Manage Tab Groups...` opens a dialog listing the groups with their
color, name and number of tabs, to rename, recolor or delete them without going
through a tab. Group membership is remembered by the file's path and restored
with the session; the tab of a file that exists only while *logsquirl* runs
stays in its group until it closes.

### Session files

*logsquirl* restores the files of every window on the next start by itself
(see [Session options](#session-options)). A window's session can also be kept
in a file of its own, to come back to an investigation later or to hand it to
a colleague.

`File->Save Session As...` writes the current window's session to a file with
the extension `.logsquirl-session` (its content is JSON). It holds:

- the open files in tab order, and which tab was in front;
- each tab's view state: the splitter position, the search options, `follow`
  mode, the marked lines, the charts and the line the view stands on;
- the custom tab names and the [tab groups](#tab-groups) (name and color) of
  these files;
- for a file opened from an archive, the archive and the member taken from it.

It does not hold the window's size and position or the sidebar width, which
depend on the screen, nor the search pattern, the kept searches or the search
limits. A file that exists only while *logsquirl* runs -- standard input, a
merged tab, text pasted from the clipboard, a file downloaded from a URL, the
output of a converter plugin or a data source -- is not written.

`File->Open Session...` opens a session file in a **new window**: the files in
their saved order, the saved tab in front, each with its view state, tab name
and group. A file from an archive is decompressed again. A group with the same
name as one you already have is that group and keeps its color; any other is
created. A file that is missing, or already open in another window, is left
out (it stays where it is open), and a notice names it; the others open. When
none of them can be opened, only the notice is shown. A file that is not a
session file, or one saved by a newer *logsquirl*, is refused with a message
and no window opens.

Every file is stored with its absolute path and with its path relative to the
folder of the session file. Opening tries the absolute path first, then the
relative one. So a folder holding the logs and the session file beside or above
them can be moved, or zipped and unpacked on another machine, and still opens.

Both entries have no key by default; one can be given in the shortcut settings,
and both are in the Command Palette.

### Filters Panel

The Filters Panel is a right sidebar dock that provides quick access to filters
and the Scratchpad. It contains two tabs:

*   **Filters tab** -- allows pinning frequently used search filters that persist
    across sessions. Toggling a pinned filter automatically triggers a search.
*   **Scratchpad tab** -- the same Scratchpad tool described below, accessible
    from the sidebar for convenience.

The Filters Panel can be toggled using the filter icon in the toolbar.

### Scratchpad

Sometimes in log files there are text in base64 encoding, unformatted
xml/json, etc. For such cases *logsquirl* provides Scratchpad tool. Text can
be copied to this window and transformed to human-readable form.
Use context menu to either add data to the current scratchpad tab or 
replace its content with selected text. There are shortcuts `Ctrl+Z` and `Ctrl+Shift+Z` for these actions.

New tabs can be opened in Scratchpad using the `Ctrl+N` hotkey.

The buttons above the text transform the selected text, or all of it when
nothing is selected: `From base64`, `To base64`, `From hex`, `To hex`,
`Decode url`, `Decode JWT`, `Format json` and `Format xml`. The result replaces
the text and is copied to the clipboard; a transformation that gives nothing
leaves the text as it is and says `Empty transformation`. `Format json` starts at
the first `{` or `[`, so a JSON object can be formatted straight from a log
line; text that is not valid XML stays unchanged.

Beside the text, and updated as you type or select, the Scratchpad shows the
same text read as numbers: `CRC32 hex` and `CRC32 dec` (its checksum),
`File time` (a Windows FILETIME as a UTC date), `Dec->Hex`, `Hex->Dec`, and
`Time`, which reads Unix seconds as a date in the time zone chosen below it.

#### JWT token decoder

The Scratchpad includes a JWT (JSON Web Token) decoder. `Decode JWT` finds the
token in the text, so a whole log line such as
`Authorization: Bearer eyJhbGciOi…` can be pasted as it is; it takes the first
token it finds, looking line by line. It decodes the Base64URL-encoded header
and payload, formats the JSON with indentation, and annotates epoch timestamp
fields (`iat`, `exp`, `nbf`, `auth_time`) with human-readable UTC dates, as in
`"exp": 1705316222  // 2024-01-15T10:57:02Z`. The signature follows as hex
bytes.

## The menu bar

Most of the menu bar is described where its feature is explained; this is the
whole list, with what the entries not explained elsewhere do.

- **File**: `New window`, `Open...`, `Open from clipboard`,
  `Open Command Output...` (see [Command output](#command-output)) and
  `Open from URL...` (see [Opening files](#opening-files)), `Open Recent`
  with `Clear List`, `Open Session...` and `Save Session As...` (see
  [Session files](#session-files)), `Close`, `Close All`, `Preferences...` and
  `Exit`.
- **Edit**: `Copy`, `Select All`, `Find...` (the QuickFind bar), `Go to line...`
  and `Go to timestamp...`, then `Copy full path` (of the current file to the
  clipboard), `Open containing folder`, `Open in editor` (in the default
  editor of the system) and `Clear file...`. `Clear file...` asks first and then
  empties the file on disk; this cannot be undone.
- **View**: `Opened files` (see
  [Switching between opened files](#switching-between-opened-files)),
  `Matches overview`, `Line numbers in main view`,
  `Line numbers in filtered view`, `Wrap text`, `Follow File`, `Reload`,
  `Chart Panel` and `Show Filter Frequency`, which charts how often the current
  search matched, one series for every alternative of the search pattern (it
  does nothing while the search line is empty).
- **Tools**: `Predefined filters...`, `Import Chipmunk filters...`,
  `Manage Tab Groups...`, `Scratchpad`, `Filters panel` and
  `Command Palette...` (`Ctrl+Shift+P`, `Cmd+Shift+P` on macOS), which lists
  every enabled menu command: type to filter, `Enter` runs the selected one.
  The key can be changed in the shortcut settings.
- **Highlighters**: `Configure highlighters...` and the list of highlighter
  sets to activate (see [Using highlighters](#using-highlighters)).
- **Encoding**: see [Encodings](#encodings).
- **Favorites**: `Add to favorites` and `Remove from favorites...`, followed by
  the favorite files, which open when chosen.
- **Plugins** and **Sources**: see [Plugins](#plugins).
- **Help**: `Documentation...` (this guide), `Report issue...` (opens a bug
  report on GitHub), `Generate crash dump` (after a confirmation, shuts
  *logsquirl* down and produces a diagnostic crash dump, see
  [Crash reporting](#crash-reporting)), `About` and `About Qt`.

The toolbar has the buttons `Open`, `Reload`, `Follow File` and
`Add to favorites`, the information about the current file (size, modification
date, encoding and the line number of the selection), `Stop` to stop a running
load, and a button that shows or hides the sidebar.

## Plugins

Plugins extend *logsquirl*. There are three kinds: *data source* plugins stream
log lines into a tab, *converter* plugins turn a file format into plain text
before it is shown, and *UI extension* plugins add menu items, status bar
widgets or panels. How to write one is described in the
[Plugin SDK guide](https://github.com/64x-lunicorn/LogSquirl/blob/master/docs/plugin-sdk.md).

A file a converter plugin turns into text is opened from a temporary copy of
that text. It is not restored with the session; the recent files keep the file
you opened, which is converted again when you open it from there.

`Plugins->Plugin Management...` opens the Plugin Management dialog. It lists the
plugins in the catalog and the ones installed on your machine under the tabs
*All*, *Installed* and *Updates*, and can be searched. Each plugin offers
*Install* (or *Update*) and *Enable* or *Disable*; an installed plugin also has
*Configure...*, which opens the plugin's own settings and works only while the
plugin is enabled and has settings. With *Auto-load enabled
plugins on startup* the enabled plugins are loaded when *logsquirl* starts, and
*Plugin Folder* opens the user plugin directory. The catalog
is fetched when the dialog opens; if that fails, the error is shown in the
status line at the bottom. Menu items that a UI extension plugin adds appear
in the `Plugins` menu, above `Plugin Management...`.

The official plugins are [Android Logcat](https://github.com/64x-lunicorn/LogSquirl-Logcat),
which streams logcat output from devices connected through ADB, and
[Serial Monitor](https://github.com/64x-lunicorn/LogSquirl-Serial), which shows
the output of serial ports. Both are in the catalog.

A plugin is a folder with a `plugin.json` manifest and a shared library.
*Install* and *Update* put a plugin from the catalog into the user plugin
directory, the one *Plugin Folder* opens. One that is not in the catalog is
installed by copying its folder there and restarting *logsquirl*:

| Platform  | Path                                                     |
|-----------|----------------------------------------------------------|
| Linux     | `~/.local/share/logsquirl/plugins/`                      |
| macOS     | `~/Library/Application Support/logsquirl/plugins/`       |
| Windows   | `%APPDATA%/logsquirl/plugins/`                           |

Plugins that come with *logsquirl* itself sit in the application folder
(`Contents/PlugIns` in the macOS app, the `plugins` folder next to the program
elsewhere) and are never changed. Updating one from the catalog puts the new
version into the user plugin directory, and a plugin there is used instead of
the one with the same id in the application folder. To go back to the version
that came with *logsquirl*, delete the plugin's folder in the user plugin
directory and restart *logsquirl*.

A portable *logsquirl* has one plugin folder, `plugins` beside its executable:
it holds the plugins it came with and is its user plugin directory too, so
*Install* and *Update* put plugins there, and an update replaces the copy it
came with.

The `Sources` menu lists the installed data source plugins. Choosing one loads
the plugin if needed and starts it; the stream opens as a new tab in the window
you chose it in. Without a data source plugin the menu says
"(no data source plugins)".

## Settings

### General

#### Search options

Determines which type of regular expression *logsquirl* will use when
filtering lines for the bottom window, and when using QuickFind.

*   Extended Regexp. The default, uses regular expressions similar to
    those used by Perl
*   Fixed Strings. Searches for the text exactly as it is written, no
    character is special

If incremental quickfind is selected, *logsquirl* will automatically restart
quickfind search when the search pattern changes.

Turning on highlight of matched text will cause the text that matched the
search pattern to be highlighted in both main view and filtered view.
Enabling color variation will cause the highlight color of different strings
that match the same pattern be slightly different.

Search size history controls the number of patterns that are saved for autocompletion
in the search input box.

Turning on option to run search on add or replace pattern will cause *logsquirl* to
immediately perform search when pattern is update from context menu.

#### Session options

*   Load last session -- if enabled, *logsquirl* will reopen files that were
    opened when *logsquirl* was closed. View configuration, marked lines and
    `follow` mode settings are restored for each file.
*   Follow file on load -- if enabled, *logsquirl* will enter `follow` mode
    for for all new opened files.
*   Minimize to tray -- if enabled, *logsquirl* will minimize to tray instead
    of closing main window. Use tray icon context menu of `File->Exit`
    to exit application. This option is not available on Mac OS.
*   Show splash screen on startup -- if enabled, a splash screen with the
    application icon and version is shown while *logsquirl* starts.
*   Show dashboard on startup -- if enabled, the [Dashboard](#the-dashboard)
    is the first tab.
*   Confirm before closing tabs -- if enabled, closing one tab or several asks
    first. The question has a "Don't ask again" box; this option turns it back
    on.
*   Enable multiple windows -- if enabled *logsquirl* will allow opening
    more than one main window using `File->New window`. In this mode last
    closed windows will be saved to open session on next *logsquirl* start.
    When exiting *logsquirl* using `File->Exit` all windows are saved and
    will be reopened.

#### Version checking options

If version checking is enabled then *logsquirl* will try to grab a version
information file from the GitHub repository and see if a new version has been released
once per week.

Stable builds will check if a new stable version is available and pop a dialogue about it.
Testing builds will check for new testing versions.

There is also an opt-in beta update channel. When "Check for beta updates" is enabled,
*logsquirl* checks for beta versions on every startup (bypassing the 7-day interval)
and shows notifications with a "(Beta)" label.

The Windows installer can turn the check off for the whole installation: untick the
component "Check for updates automatically". It leaves an empty file named
`logsquirl_no_update_check` beside the executable, and while that file is there the check
stays off and "Check for new version" is greyed out. Any other installation can be set up
the same way by creating that file. A silent install (`/S`) leaves the file as it finds it,
so an opt-out an administrator created survives silent upgrades. What the check sends is described in the
[privacy policy](https://github.com/64x-lunicorn/LogSquirl/blob/master/PRIVACY.md).

### View

#### Font

The font used to display the log file. A clear, monospace font (like the
free, open source, [DejaVu Mono](http://www.dejavu-fonts.org) for
example, is recommended.

Font antialiasing can be forced if auto-detected options result in low-quality
text rendering.

Font size can be changed from either main or filtered view using `Ctrl+Mouse wheel`
to zoom in/out.

#### Style

The theme *logsquirl* is drawn in. Choosing one applies it at once, in every
open window:

- **Light** and **Dark**, the two everyday themes.
- **High Contrast**, for maximum contrast: pure colors, thick borders, yellow
  for selection and focus.
- **Smyck**, a dark theme in the colors of the
  [SMYCK terminal color scheme](https://color.smyck.org/).
- **Smyck Light**, the same scheme on light surfaces, with the same color
  labels as Smyck.
- **System**, which becomes Light or Dark from the operating system's color
  scheme and follows it while *logsquirl* runs.

Icons follow the theme, dark or light. The colors of the nine color labels
follow it too: a color label you have not picked a color for yourself takes the
colors of the theme you choose, and keeps them when you choose another.

A theme can be extended with a stylesheet of your own: put a `.qss` file named
after the theme (`fusion-light.qss`, `dark.qss`, `high-contrast.qss`,
`smyck.qss` or `smyck-light.qss`) into the `themes` directory of the
configuration directory (`~/.config/logsquirl` on Linux,
`~/Library/Preferences/logsquirl` on macOS, `%LOCALAPPDATA%\logsquirl` on
Windows, the executable's folder for a portable *logsquirl*), and it is
appended to the theme's stylesheet.

#### Language

*logsquirl* is translated into English, German (Deutsch), Spanish (Español),
French (Français), Brazilian Portuguese (Português (Brasil)), European
Portuguese (Português (Portugal)), Ukrainian (Українська), Simplified Chinese
(中文 (简体)) and Traditional Chinese (中文 (繁體)). Choose the language under
`Settings->View->Language`. When you press OK or Apply with another language,
*logsquirl* tells you that it needs to be restarted to apply the change, so
restart it to have every part of the interface in the new language.

#### High DPI

Options in this group can be used in case *logsquirl* window looks 
bad on High DPI monitors. Usually, Qt detects the correct settings.
However, these options may be useful, especially for non-integer
scale factors manual overrides.

#### Miscellaneous

*Context lines around matches* sets the number of [breadcrumbs](#breadcrumbs)
shown before and after each match (5 by default, 0 turns them off). With
*Enable fast scrolling with Alt key*, on by default, the mouse wheel scrolls
*Fast scroll multiplier* times (5) as far while `Alt` is held.

Some log files contain ANSI color codes to be displayed by terminals with
color support: the output of `ls --color`, of a test runner or of a colored
logger. *ANSI color sequences* says what *logsquirl* does with them:

- *Show as text* (the default) shows the escape sequences as characters, as
  the file holds them.
- *Hide* removes them from every line: the main and the filtered view show the
  text without them, and Search, QuickFind, selection and copy work on that
  text.
- *Show colors* removes them as *Hide* does and paints the lines in the colors
  they ask for, in the main and the filtered view. Foreground and background
  colors are shown (the 16 basic colors, the 256-color palette and truecolor);
  bold, underline and the other attributes are not. Each line starts in its
  own colors. The 16 basic colors are the Theme's: Smyck and Smyck Light use
  the SMYCK scheme's, the other Themes xterm's, and a foreground too faint to
  read on its background is moved toward the Theme's text color. Text with
  only a background color, where the Theme's text color is too faint to read
  on it, is moved toward black or white instead. Highlighters, the search
  highlight, Color Labels, QuickFind and the selection paint over the ANSI
  colors. Search, QuickFind, selection and copy work exactly as under
  *Hide*, and switching between the two reloads nothing and runs no search
  again. The table view shows the text as under *Hide*, without ANSI colors.

*Hide* and *Show colors* make regular expression search slower: every line is
read without its sequences first.

### File

#### File change monitoring

If file change monitoring is enabled, *logsquirl* will use facilities
provided by the operating system to reload the file when data is changed on the
disk.

Sometimes this kind of monitoring is unreliable on
network shares or directories mounted via sftp. In that case, polling can
be enabled to make *logsquirl* check for changes, every 2000 ms by default
(*Polling interval*). Monitoring is on by default; polling only on Windows.

*logsquirl* tries to detect if the file was changed in the already indexed
area. This mechanism involves hash recalculation and can be slow for
large files and network filesystems. If fast modification detection
is enabled *logsquirl* will check hash for the first and last parts of
changed files. This is faster but can skip over changes in the middle of
the file. This feature should be used with caution.

It is possible to enable follow file mode by scrolling past the end of file.
This behavior can be disabled.

#### Encoding

*logsquirl* tries to detect file encoding automatically. If encoding detection
is not required then it is possible to specify the encoding that will be
used for all new opened files.

#### Archives

If extract archives is selected then *logsquirl* will detect if opened file
is of one of supported archives type or a single compressed file and
will ask user permission to extract archives content to a temporary folder.

If you do not want *logsquirl* to ask for permission, check 
"extract archives without confirmation" option.

#### File download

By default, *logsquirl* will not download files using HTTPS if certificates
can't be checked. In some development environments self-signed 
certificates are used. In this case, *logsquirl* can be instructed to ignore
SSL errors.

### Advanced options

These options refer to the customization of performance related settings.

*Regular expressions engine* chooses between Vectorscan, the default and the
fastest, and Qt's engine, which understands every pattern, lookahead included,
but is slower. Even with Vectorscan, a pattern it cannot handle is searched
with Qt's engine.

If parallel search is enabled, *logsquirl* will try to use several CPU cores
for regular expression matching. This does not work with quickfind.

*logsquirl* has several strategies for regular expression search based on file 
encoding. By default, it is optimized for files with UTF8 or single-byte
encodings. If most of the files are in multi-byte encodings then enabling
search optimization for non-latin encodings could improve performance.

If search results cache is enabled, *logsquirl* will store numbers of lines
that matched the search pattern in its memory. Repeating searches for the same
pattern will not go through all files but will use cached line numbers
instead.

In case there is an issue with *logsquirl*, logging can be enabled with
a desired level of verbosity. Log files are saved to a temporary directory.
A log level of 4 or 5 is usually enough. Enabling logging can slow down 
regular expressions search.

## Crash reporting

*logsquirl* uses Crashpad crash handler to collect minidump files in case of 
unexpected crashes. At startup, *logsquirl* checks for new minidumps and asks  the user
if these files should be sent to developers.

Crash report provides information about:

* operating system: name, version, architecture, cpu features, system memory
* Qt version
* modules that were loaded into *logsquirl* process: filename, size and hashes for symbols
* stacktraces for all running threads in *logsquirl* process

These minidumps do not include the full content of *logsquirl* process memory during the crash.

## Keyboard commands

*logsquirl* keyboard commands try to approximately emulate the default
bindings used by the classic Unix utilities *vi* and *less*.

The main commands are:

|Keys            |Actions                                                           |
|----------------|------------------------------------------------------------------|
|arrows          |move the selection one line up/down or scroll left/right          |
|j or k          |move the selection one line down/up                               |
|h or l          |scroll left/right                                                 |
|\^ or \$        |scroll to beginning or end of selected line                       |
|Ctrl+Home       |jump to the first line of the file (selecting it)                 |
|Ctrl+End        |jump to the last line of the file (selecting it)                  |
|Shift+G         |jump to the last line of the file (selecting it)                  |
|Ctrl+L          |show the go to line dialog                                        |
|Ctrl+Shift+L    |show the go to timestamp dialog                                   |
|' or "          |start a quickfind search in the current screen                    |
|                |(forward and backward)                                            |
|n or N          |repeat the previous quickfind search forward/backward             |
|\* or .         |search for the next occurrence of the currently selected text      |
|/ or ,          |search for the previous occurrence of the currently selected text  |
|f               |activate 'follow' mode, which keep the display as the tail of the |
|                |file (like "tail -f")                                             |
|m               |put a mark on current selected line                               |
|\[ or \]        |jump to previous or next marked line                              |
|+ or -          |increase/decrease main view size                                  |
|v or Shift+V    |switch filtered view visibility mode, forward or backward          |
|                |(see [Breadcrumbs](#breadcrumbs))                                 |
|F5              |reload current file                                               |
|Ctrl+S          |Set focus to search string edit box                               |
|Ctrl+Shift+O    |Open dialog to switch to another file                             |

Every key in this table is a default and can be changed in the shortcuts tab of
the options dialog, where the commands without a default key, such as
*Open scratchpad* or *Full Screen*, can be given one as well.

A count before a command repeats it, as in *vi*: type `0`, the number, then
the command. `05j` moves the selection five lines down, `012k` twelve lines up,
and the arrow keys take a count as `j` and `k` do; *Jump to line number* goes to
the line of the number typed, to the first line after a lone `0`, and stays
where it is when no number was typed. The count starts with `0` because `1` to `9` on
their own are the shortcuts of the filtered view's visibility and of the search
buttons. Once `0` has started a count, the digits after it belong to the count
until the command, or until two seconds pass without a key. `0` also scrolls to
the beginning of the line, with or without a count after it.

## Mouse navigation

Holding `Alt` while scrolling scrolls faster, by the *Fast scroll multiplier*
of `Settings->View`. Holding `Shift` while scrolling scrolls a page at a time.
Scrolling sideways, on a trackpad or a tilting wheel, scrolls horizontally.

## Command line options

|Switch             |Actions                                                   |
|-------------------|----------------------------------------------------------|
|-h, --help         |print help message and exit                               |
|-v, --version      |print version information                                 |
|-m,--multi         |allow multiple instance of logsquirl to run simultaneously (use together with -s)|                                    |
|-s,--load-session  |load the previous session (default when no file is passed)|
|-n,--new-session   |do not load the previous session (default when a file is passed) |
|-l,--log           |save the log to a file                                    |
|-f,--follow        |follow initial opened files                               |
|-                 |read a Log File from standard input, in the running *logsquirl* if there is one (in a window of its own with -m); can be combined with files |
|-d,--debug         |output more debug (include multiple times for more verbosity e.g. -dddd) |

## The command line tool

Beside the application, every package ships *logsquirl_grep*, a command line
tool that searches a log file with the same engine as *logsquirl* and prints
the lines it matched. It reads the log file once, the way it is on disk: it
does not follow the file, recognizes no log format and hides no ANSI color
sequences.

The matching lines are written to standard output, in the order they appear in
the file and with nothing else mixed in, so the output can be piped into
another tool. Everything else goes to standard error: the log messages, and
the reason a log file could not be loaded or a pattern was not a valid regular
expression, prefixed with `logsquirl_grep:`.

In the deb, the rpm and the Windows packages the tool is installed beside
*logsquirl*, so it is found wherever *logsquirl* is. On macOS it is a helper
inside the application bundle, and in the AppImage it sits in the image's own
`usr/bin`, which a shell only sees once the image is extracted
(`LogSquirl.AppImage --appimage-extract`):

|Platform|Command                                                                       |
|--------|------------------------------------------------------------------------------|
|Linux   |`logsquirl_grep -e 'ERROR' app.log`                                           |
|Windows |`logsquirl_grep.exe -e "ERROR" app.log`                                       |
|macOS   |`/Applications/LogSquirl.app/Contents/MacOS/logsquirl_grep -e 'ERROR' app.log`|

To call it by name on macOS, link it into a directory on the `PATH`:

```sh
ln -s /Applications/LogSquirl.app/Contents/MacOS/logsquirl_grep /usr/local/bin/logsquirl_grep
```

The pattern is the regular expression the Search Line of *logsquirl* takes,
and the file to search is passed as an argument:

|Switch             |Actions                                                   |
|-------------------|----------------------------------------------------------|
|-h, --help         |print help message and exit                               |
|-v, --version      |print version information                                 |
|-e, --pattern      |pattern to search for                                     |
|-d, --debug <level>|output more debug, a higher level is more verbose (default 0)|

A search that matched nothing is not a failure: the tool exits with 0 and
prints nothing. It exits with a non-zero code when no log file was passed, the
log file could not be loaded, or the pattern is not a valid regular
expression.

*logsquirl_grep* is always portable: it reads its settings from a
`logsquirl.conf` beside its own executable and not from the application's, and
honors a default Encoding pinned there over the one it detects.
