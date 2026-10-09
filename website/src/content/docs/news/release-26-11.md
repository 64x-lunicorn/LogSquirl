---
title: Version 26.11.0-beta1
description: Value Names, the Regex Lab, File Associations on Windows, macOS and Linux, Open Command Output, Session files that bring their Searches back, CSV export, ANSI colors, 18 bug fixes.
release:
  version: 26.11.0-beta1
  date: 2026-10-09
  channel: beta
---

LogSquirl now shows the values of a Log Line by name, tests a pattern live on your own Log Lines in the
Regex Lab, opens the output of any command as a followed Log File, and becomes the app for your log files
on Windows, macOS and Linux. A Session can be saved to a file and brings its Searches back, without slowing
the restore. Every pull request is measured for performance, and master every night. 18 bug fixes.

This is a beta. Install it next to a stable version only if you want to try these changes early.

### Value Names

- **Values shown by name**: A Naming Rule picks values out of a Log Line by the capture groups of its regex
  and looks each one up in a Name Table, so `BAP << ECU 0x15` shows as `BAP << ECU Beispiel(0x15)`. The Log
  File is never changed, and search, filters, QuickFind and highlighters still match the raw text.
- **Turned on per tab**: *View → Show Value Names* (`Ctrl+Shift+N`). *Copy* copies the raw text, *Copy as
  Shown* what you see, and *Save to file* can save *With Value Names*.
- **Edited in one place**: The *Value Names* sidebar tab and *Tools → Value Names...* edit rules and Name
  Tables. Tables import from and export to CSV, and can be pasted from a spreadsheet.
- **Shared**: Naming Groups export and import like filter groups, and the Team Folder shares them.

### Regex Lab

- **A pattern tested on your Log Lines**: *Tools → Regex Lab…* matches a pattern live against the selected
  Log Lines, the lines around the current one, or pasted text. It marks the matches, lists the capture
  groups, counts the matching lines exactly as a Search would, and warns of a pattern that is slow.
- **From where the pattern is**: *Test…* next to a highlighter or a filter group, and *Open in Regex Lab...*
  in the search line's context menu, open the Lab with the pattern and its options. *Apply* writes the
  changes back.
- **For plugins**: A plugin can open the Regex Lab, go to a Log Line and read the selected Log Lines. The
  plugin API grows compatibly; plugins built for the earlier API load unchanged.

### File Associations

- **LogSquirl opens your log files**: A new *File Associations* page in the options lists the file types
  LogSquirl opens, `.log` and Android Logcat traces first, `.out`/`.err`, `.trace` and `.txt` as options,
  and makes LogSquirl their default on Windows, macOS and Linux. On Windows the choice is confirmed in
  *Default apps*, on macOS in the system's own dialog.
- **Asked once**: On the first start, LogSquirl asks which file types it should open.
- **Kept**: When another application takes a chosen type over, the status bar says so and offers to
  restore it.
- **Open with LogSquirl on any file**: On Windows, right-clicking any file offers *Open with LogSquirl*, so
  rotated logs like `app.log.1` open too. The installer chooses the file types on a page of its own.
- **A document icon**: Files LogSquirl opens show a sheet with the squirrel.

### Opening and saving

- **Open Command Output**: *File → Open Command Output…* runs a command such as `ssh host tail -f
  /var/log/syslog`, `docker logs -f web` or `kubectl logs -f deploy/api` and follows its output in a new
  tab. When it ends, the tab shows its exit code.
- **Piping into a running LogSquirl**: `… | logsquirl -` opens a tab in the window already running.
- **Session files**: *File → Save Session As…* and *File → Open Session…* save and open a window's files,
  tabs and view states, found again after a folder of logs moved to another machine.
- **Searches come back**: A restored tab gets back every Search it kept, each in its own Filtered View,
  and runs them again once every Log File of the Session has loaded, the tab in front first. A tab you
  click meanwhile runs its Searches at once, and a Search you start runs at once. The restore is as fast
  as without them.
- **CSV export**: The Table View and the Filtered View export as CSV, with the columns you choose.
- **Merge… picks and orders the files**: One dialog instead of four menu entries.

### View

- **ANSI colors**: Color sequences can now be shown as colors, 16 basic colors, the 256-color palette and
  truecolor, with highlighters, Search and the selection painted over them.

### Team Folder

- **A status you can read**: The Team Folder tab names the step that failed, says what to do about common
  failures such as a failed sign-in or SSO authorization, and keeps Git's own output in a collapsible
  section.
- **Last synced and Open Folder**: It shows when it last synced and opens its folder in the file manager.
- **Sync Now tests the connection**: It syncs the repository the fields show, before the first *Apply*.

### Performance

- **Measured in every pull request**: A pull request that costs more instructions than master fails CI,
  and one that touches a hot path gets a before-and-after of every benchmark as a comment.
- **Watched every night**: Master is measured nightly against its Budgets, and a regression opens an
  issue. The trend is on the new [Performance](/performance/) page.
- **Faster indexing**: A Log File that starts with ASCII is read once instead of twice while it is indexed.
- **Release builds can use PGO**: Profile-guided optimization, and BOLT on Linux, are built into the
  release pipeline, switched on per platform once the measurements show a gain.

### Bug fixes

18 fixes, among them: a Search says when the regex engine gave up on Log Lines, Windows command output
shows its umlauts, a growing Log File that starts with ASCII shows later UTF-8 correctly, Open Command
Output works with csh and tcsh, one Return runs one Search, a tab brought to the front shows its selected
Log Line, a Log File that grows while it first loads shows all of it, and disabling a plugin while one of
its dialogs is open no longer crashes LogSquirl.

The complete list is in the release notes on GitHub.
