---
title: Version 26.12.0-beta2
description: A menu bar ordered by task, a shorter context menu, Name this value, Highlight this, Add as Predefined Filter and Chart this value from a selection, one Search tab in Preferences, one Group List, 8 bug fixes.
release:
  version: 26.12.0-beta2
  date: 2026-11-06
  channel: beta
---

The second beta of 26.12 reworks the menus: the menu bar is ordered by task, the context menu of the Log
Lines shows what you do with a selection first, and a value or a selection in a Log Line becomes a Named
Value, a Highlighter, a Predefined Filter or a chart series in one compact dialog. Preferences gathers every
Search setting on one tab, and the group dialogs share one Group List. 8 bug fixes.

This is a beta. Install it next to a stable version only if you want to try these changes early.

### Menus

- **The menu bar is ordered by task**: Seven menus instead of ten. The file actions are in *File*, the
  Search Limits in *Edit*, the panels, the sidebar and *Encoding* in *View*, and *Highlighters*, the plugins'
  items, *Plugin Management* and *Preferences* in *Tools*. Every action keeps its shortcut.
- **In the menu bar too**: *File → Merge Log Files...*, *File → Export as CSV...* and *View → Sidebar*, all
  three ready for keys of your own in the Shortcuts list.
- **The context menu of the Log Lines is shorter**: In the Text View, the Filtered View and the Table View
  it shows Copy, the Search entries and Mark first, then Highlighters and Color Labels; the rarely used
  entries are under *More*.
- **Named one way**: Entries that open a dialog end in three dots, menus use title case, and each command
  in the Shortcuts list is named as in its menu.
- **Translated throughout**: The context menu of the Log Lines and every menu follow the chosen language,
  also right after a change of language.

### From a selection

- **Name this value...**: Select or right-click a value in the Text View or a Filtered View. A compact dialog
  proposes a Naming Rule that captures it, a Name Table with the value as a key, a template and a Naming
  Group, and previews the Log Line. On a value an existing Naming Rule captures, the menu offers to name or
  rename it in that rule's Name Table instead.
- **Highlight this...**: Turns a selection into a Highlighter, with its colors, the whole line or only the
  Match, and the Highlighter Set to add it to; the views recolor at once.
- **Add as Predefined Filter...**: Turns a selection into a Predefined Filter of the Filter Group you choose,
  and can search with it right away.
- **Chart this value...**: Under *More*, turns a selected number into a chart series that captures the
  number in Log Lines like this one, and shows the Chart Panel.

### Dialogs

- **One Search tab in Preferences**: Every Search and indexing setting is on the new *Search* tab, and the
  dialog fits a 1366 x 768 screen, each tab scrolling when it needs to.
- **One Group List**: The Predefined Filters, Highlighters and Value Names dialogs list your own groups, the
  Team groups and the Plugin groups as sections of one list, with the same buttons.
- **Value Names in the order you fill them in**: Name Tables come before Naming Rules, and a capture group's
  Name Table choice ends with *New Name Table...*.
- **Highlighters side by side**: A set's Highlighters are listed beside the selected Highlighter's fields.
- **Show only this** in the Filters and Value Names tabs of the sidebar is in the context menu, with *Restore
  previous selection* to undo it.

### Bug fixes

8 fixes, among them: OK in Preferences keeps the default shortcuts, searching the Filters tab keeps the
filters checked, a Highlighter added to a set colors at once, refreshing the Filters panel keeps the Search
pattern, and closing the Value Names dialog with a name field in focus no longer reads freed memory.

LogSquirl also builds with Qt 6.12.

The complete list is in the release notes on GitHub.

### Already in 26.12.0-beta1

Sort Order and Column Layout in the Table View, Field Terms in a Search, Matches Only, next and previous
Match, plugins in the context menu and reading Log Lines, fields and Marks, 17 security fixes and 18 bug
fixes.

#### Table View

- **Sort Order**: A click on a column header sorts the Rows by that column, a second click the other way
  round, a third restores Log Line order. Numbers sort as numbers, the timestamp by its Timestamp, the Δt
  column by its duration, everything else by its text ignoring case. The sort runs off the UI thread with
  its progress in the header; a second click or Esc stops it.
- **A Row stays its Log Line**: The selection, Marks, Matches, Go to line, Go to timestamp and QuickFind
  land on the same Log Lines in a sorted table, and *Copy* and the CSV export follow the order shown. A
  followed Log File keeps its Sort Order, with appended Log Lines merged in at their place.
- **Column Layout**: The header's context menu hides and shows columns, a header dragged to another place
  moves its column, and *Reset columns* restores the Log Format's order. Which columns are shown, in which
  order and how wide is kept per Log Format, also after a restart.

#### Search

- **Field terms**: In the logical combination, `Protocol:"DNS"` matches the text of that field of the tab's
  Log Format alone, not the whole line. Field terms combine with plain sub-patterns and `not(...)`, work in
  Predefined Filters and in the Regex Lab, and a field the Log Format lacks is a pattern error naming it.
- **Search this value in a column**: The Table View's cell menu appends the field term and runs the Search.
- **Matches Only**: *View → Matches Only* narrows the Table View to the Rows of the current Search's
  Matches. It follows the Search, composes with the Sort Order and turns off when the Search is cleared.
- **Next and previous Match**: `}` jumps to the next matching line and `{` to the previous one, in the text
  view, the Filtered View and the Table View, skipping Marks and context lines.
- **As fast as before**: A Search without field terms runs at its old speed, and the benchmarks now time
  field term Searches and sorting too.

#### Log Formats

- **Recognized as it grows**: A Log File too short to be recognized is tried again with each load that
  brings more lines, up to 50 lines, so a file opened with only a header line shows its Table View once its
  first Log Lines arrive.
- **Shipped by plugins**: A plugin can name Log Format files in its manifest; they are part of the Log
  Format Catalog while the plugin is enabled. The Log Formats page in the options shows each format's
  *Source*: *Built-in*, *User* or the plugin's name.

#### Plugins

- **Context menu items**: A plugin adds items to the context menu of the Text View, the Table View and the
  Filtered View, optionally in submenus, and learns the Log File, the selected lines and any selected word
  when one is chosen. Plugins menu items can now be removed again too.
- **Log Lines, fields and Marks**: A plugin reads the Log Lines of any open Log File, on a thread of its
  own, numbered and decoded as the views show them, and hears when Log Lines are appended or the Log File
  is replaced. It reads the field values of Log Lines as the Table View shows them, and sets, removes and
  reads Marks, which behave like any other Mark.
- **The encoding of a Log File**: A plugin learns which text encoding LogSquirl reads a Log File in and
  hears when it changes, so a plugin that reads the file itself sees the same lines as the views.
- **Converters run in the background**: Opening a file a converter plugin handles no longer freezes the
  window. A long conversion shows a progress dialog with *Cancel*, and a failed one says which plugin could
  not convert which file. A converter can also claim a file by its first bytes, so a `.log`, `.bin` or
  extensionless capture is converted too.
- **Data sources**: A data source plugin is told when its stream starts and stops, and one that ended its
  own stream can be started again from the Sources menu.
- **Shipped with a plugin**: Besides Log Formats, a plugin's manifest can name Predefined Filters and
  Highlighter Sets; they are listed next to your own with the plugin as Source, read-only.
- **Built for the right Qt**: A plugin can name the Qt it was built against, and one built against a Qt
  LogSquirl cannot serve is not loaded; its card in Plugin Management says why.
- **Compatible**: The plugin API grows compatibly; plugins built for an earlier API load unchanged. The
  plugin SDK guide and its example plugin show the new functions.

#### Security

A security review of LogSquirl brought 17 fixes:

- **Archives**: Extracting a compressed Log File or a plugin archive never writes outside its folder and
  skips symbolic links, and decompression stops at a limit, 16 GiB by default, set under *Preferences >
  File > Archives*.
- **Plugins**: A plugin download is installed only when it matches the SHA-256 of its catalog entry, every
  catalog URL must be https, a plugin id can never be a path, and disabling every plugin stays disabled.
- **Untrusted text and files**: File names, Log Lines and update notices show as plain text everywhere,
  the CSV export neutralises formula characters, programs start by their absolute path, *Open containing
  folder* is safe for any file name, and the debug log and merged Log Files are private.
- **Robustness**: Overlong and pathological Log Lines are bounded and no longer stall the views, a
  corrupted Index Cache entry is dropped, and a Search never outlives its Log File.

#### Bug fixes

18 fixes, among them: `open_file` called from a plugin's worker thread opens the file on the UI thread, a
UTF-16 Log File no longer breaks a line inside two characters, a tar.zst or tar.lz4 archive extracts its
members whole, every read of a Log File checks its seek, a Log Line whose tab expansion is cut says so,
Quick Find's notices are translated, and the Highlighters and Predefined Filters dialogs no longer crash
when they close.

The crash reporter is sentry-native 0.17.4 and archives are read by KArchive 6.31.0. The build images
stop trusting a compromised emSign certificate when they download Qt.

The complete list is in the release notes of 26.12.0-beta1 on GitHub.
