---
title: Version 26.12.0-beta1
description: The Table View sorts by a column and keeps its Column Layout per Log Format, a Search narrows by column with field terms, Matches Only, next and previous Match, converter plugins run in the background, plugins ship Log Formats, 3 bug fixes.
release:
  version: 26.12.0-beta1
  date: 2026-10-21
  channel: beta
---

The Table View sorts its Rows by a column, hides and reorders its columns and keeps that layout per Log
Format. A Search can match the text of one field alone, and the table can show only the Rows that match.
`}` and `{` jump between Matches. Opening a file a converter plugin handles no longer freezes the window,
plugins can ship Log Formats and learn which encoding a Log File is read in. 3 bug fixes.

This is a beta. Install it next to a stable version only if you want to try these changes early.

### Table View

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

### Search

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

### Log Formats

- **Recognized as it grows**: A Log File too short to be recognized is tried again with each load that
  brings more lines, up to 50 lines, so a file opened with only a header line shows its Table View once its
  first Log Lines arrive.
- **Shipped by plugins**: A plugin can name Log Format files in its manifest; they are part of the Log
  Format Catalog while the plugin is enabled. The Log Formats page in the options shows each format's
  *Source*: *Built-in*, *User* or the plugin's name.

### Plugins

- **Converters run in the background**: Opening a file a converter plugin handles no longer freezes the
  window. A long conversion shows a progress dialog with *Cancel*, and a failed one says which plugin could
  not convert which file. Plugin authors get a new entry point that reports progress and polls for
  cancellation.
- **The encoding of a Log File**: `get_log_file_encoding` tells a plugin which text encoding LogSquirl reads
  a Log File in, and `register_encoding_callback` tells it whenever that changes, so a plugin that reads the
  file itself sees the same lines as the views. The plugin API grows compatibly; plugins built for the
  earlier API load unchanged.

### Bug fixes

3 fixes: `open_file` called from a plugin's worker thread opens the file on the UI thread, a plugin gets a
Regex Lab answer that came while it was being disabled, and a UTF-16 Log File no longer breaks a line
inside two characters.

The build images also stop trusting a compromised emSign certificate when they download Qt.

The complete list is in the release notes on GitHub.
