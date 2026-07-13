
# Unreleased

## Bug fixes:
 - **Windows dark mode: checkbox and combobox icons not rendered**: Under
   Windows dark mode the checkmark in checkboxes and the dropdown arrow in
   comboboxes were invisible or incorrectly drawn.  Root causes: the indicator
   size was only 12 px (with a 2 px border leaving just 8 px for the SVG image,
   too small for Windows DPI scaling) and no explicit `image: none` was set for
   the unchecked state, so Qt6's Fusion style bled through its native indicator
   drawing on Windows.  Indicators are now 16 px (matching the menu indicator),
   unchecked states explicitly suppress the image, and the combobox arrow size
   was bumped from 10 px to 12 px for better hi-DPI visibility.
 - **SpinBox up-arrow used the down-arrow icon**: `QSpinBox::up-arrow` and
   `QDoubleSpinBox::up-arrow` incorrectly referenced `arrow-down-dark.svg`.
   A new `arrow-up-dark.svg` was created and wired up.
 - **AppImage now runs on Ubuntu 22.04 (jammy) and other older distributions**:
   The Linux AppImage was built on Ubuntu 24.04, so it required glibc 2.39 and a
   GCC 13 `libstdc++`, neither of which ships on Ubuntu 22.04 (glibc 2.35).  The
   AppImage failed to launch there with `version 'GLIBC_2.38' not found`.  It is
   now built on a dedicated Ubuntu 22.04 image using GCC 12.  Because
   `linuxdeploy` intentionally never bundles glibc or libstdc++ (they come from
   the host), building on jammy pins the compatibility floor to glibc 2.35 and
   `GLIBCXX_3.4.30`, so a single AppImage runs on Ubuntu 22.04 and every newer
   distribution.  See `BUILD.md` → "Docker Build Containers" for details.

---

# v26.06.1 (2026-06-18)

## New features:
 - **FiltersPanel — Double-click to solo-activate**: Double-clicking a filter
   item unchecks all other filters and activates only that single filter.
   Double-clicking a group item activates all filters in that group exclusively
   while unchecking every filter in all other groups.
 - **FiltersPanel — Persistent pinned state**: The set of checked (active)
   filters is now saved to and restored from QSettings so the selection
   survives application restarts.  Writes are debounced (500 ms) to avoid
   synchronous disk I/O on every checkbox click.
 - **Chart Panel — Format-Aware Templates**: When a log format is auto-detected,
   the Chart Panel now shows a **Templates** button in the toolbar with
   pre-configured chart series that can be added with a single click:
   - **Log Level Distribution**: One count-mode series per known log level
     (error, warning, notice, …) with configurable time buckets (1 s, 5 s, 1 min).
   - **Message Rate**: Count all matching lines over time (per second, per 5 s,
     per 10 s, or per minute).
   - **Numeric Fields**: Automatically extract integer/float fields defined in
     the format as value series.
   - **Field Occurrence**: Count-mode series for each non-hidden field.
   - The **X-axis is automatically configured** with the format's timestamp regex
     and timestamp format — no manual regex input needed.
   - The **"+ Add Series" dialog** also pre-fills X-axis timestamp fields when
     a format is detected, so manually created series get timestamp support
     for free.
   - Works in both **text view and table view** — format detection feeds the
     chart panel regardless of which view mode is active.
   - Supports all built-in and user-defined format definitions.
   - strftime/lnav timestamp formats are automatically converted to Qt format
     strings (`%Y-%m-%d` → `yyyy-MM-dd`, `%L` → `zzz`, etc.).
 - **Auto Log Format Detection**: Automatically detect log formats (lnav-compatible)
   and display logs in a structured table view with columns for timestamp, level,
   and other fields. Supports 24 built-in format definitions. Toggle between
   text and table view with a toolbar button. User-defined format files can be
   placed in the platform data directory
   (`~/.local/share/logsquirl/formats/` on Linux,
   `~/Library/Application Support/LogSquirl/formats/` on macOS,
   `%APPDATA%/LogSquirl/formats/` on Windows). Enable via Options → Log Formats →
   "Auto-detect log format (table view)". Available formats are listed in the
   same tab, and the user formats folder can be opened directly from there.
   - Columns are auto-sized by sampling up to 2000 rows to ensure all cell
     content is fully visible without clipping.
   - Column order matches the order capture groups appear in the format's regex,
     so columns reflect the original log line layout.
   - Full-fidelity highlighting: search matches, marks, highlighter sets, quickfind
     results, and color labels are rendered in the table view using a custom
     `LogTableHighlightDelegate`.
   - Pixel-level horizontal scrolling with a smooth scroll step.
   - The last column stretches to fill the viewport when content is narrower
     than the view, and shrinks back when scrolling is needed.
 - **Native title bar theming**: The OS window chrome (macOS traffic lights,
   Windows title bar) now matches the active theme.  Uses
   `QStyleHints::setColorScheme()` (Qt 6.8+) for cross-platform support.
 - **Startup Splash Screen**: A splash screen with the LogSquirl app icon and
   version is now shown while the application initialises (plugin discovery,
   session restore).  Can be disabled via Options → General →
   "Show splash screen on startup".
 - **Welcome Dashboard**: A permanent "Home" tab (pinned at position 0)
   displays the app icon, recent files, favorites, quick action buttons
   (Open File, Load Session), loaded plugin status, and keyboard shortcut
   hints.  Files can also be dropped onto the dashboard.  The Home tab
   cannot be closed and is always available.
 - **Zstd/LZ4 Decompression**: Transparently open `.zst` and `.lz4`
   compressed log files.  The file is streamed through a decompressor to a
   temporary file before indexing, so the original compressed file is never
   modified.
 - **Index Cache**: File indexes are persisted to disk so re-opening a
   previously indexed file skips the full indexing pass.  Enable/disable and
   configure the maximum cache size (MB) in Options → Performance.  The
   cache is keyed by file path and size; stale entries are evicted
   automatically.
 - **Command Palette**: VS Code-style quick-command dialog
   (Ctrl+Shift+P / Cmd+Shift+P or Tools → "Command Palette…").  Fuzzy-search
   all menu actions, recent files, favorites, highlighter sets, and plugin
   commands from a single input field.
 - **German, Ukrainian, Spanish, French, Brazilian Portuguese, European Portuguese translations**: 
   Added full UI translations for German (de), Ukrainian (uk), and Spanish (es), 
   French (fr), Brazilian Portuguese (pt_BR), and European Portuguese (pt_PT).  
   Selectable in Options → View → Language.
 - **Dashboard toggle**: The welcome dashboard can now be disabled via
   Options → General → "Show dashboard on startup" (enabled by default).
 - **Splash screen disabled by default**: The startup splash screen is now
   off by default.  Users who want it can re-enable it in
   Options → General → "Show splash screen on startup".
 - **Index cache disabled by default**: The index cache is now disabled by
   default to avoid unexpected disk usage.  Users can opt in via
   Options → Performance → "Use index cache".

## UI/UX improvements:
 - **Theme rename**: "Fusion" theme renamed to "Light" for clarity.
   Existing user settings are migrated automatically.
 - **Complete theme overhaul**: All three themes (Light, Dark, High Contrast)
   have been fully rewritten with consistent color palettes based on the
   PRD design specs (Fusion Refined, Modern Slate, Accessibility First).
 - **Checkbox and indicator visibility**: Tree widget checkboxes
   (`QTreeWidget::indicator`) now have explicit styling in all themes with
   2px borders, proper indeterminate/disabled states, and hover feedback.
   Menu and standalone checkboxes updated to match.
 - **Tree item spacing**: Added 3px vertical padding to all tree, list, and
   table view items for improved readability.
 - **Container background consistency**: `QSplitter`, `QScrollArea`,
   `QDockWidget`, `QDialog`, and `QMainWindow::separator` now have explicit
   backgrounds matching each theme's surface color.
 - **Dark theme softened**: Checkbox contrast in the dark theme reduced from
   harsh `#888888`-on-`#1E1E1E` to a softer `#777777`-on-`#2D2D30`.
 - **Light theme white surface fix**: Replaced pure white (`#FFFFFF`)
   container backgrounds with off-white (`#F8F9FA`) for `QTabWidget::pane`,
   `QTabBar::tab:selected`, splitters, and dock widgets.  Pure white is now
   reserved for content/input areas only.
 - **Toggle button visibility**: Checkable toolbar buttons (filter bar: match
   case, regex, inverse, boolean, auto-refresh; and main toolbar: follow,
   text wrap) now clearly show their checked/active state across all themes
   (Dark, Fusion, macOS, Windows).
 - **Light/Fusion theme**: Added a proper light palette and baseline QSS
   stylesheet for the Fusion style covering buttons, tab bar, scroll bars,
   tooltips, menus, group boxes, and dock widgets.  Text contrast is now
   consistent and widgets no longer look washed out.
 - **Platform themes (macOS, Windows)**: Common QSS for checked-state
   buttons is now applied to all platform-native themes so toggle buttons
   are always visually distinguishable.
 - **Tab close buttons**: Increased tab height from 24px to 28px and close
   button size from 12px to 14px to prevent clipping.
 - **Dark theme tab close**: The tab close (X) button now shows a red
   background (#C42B1C) on hover for clear affordance.
 - **High-res logo**: Splash screen and dashboard now use a 1024×1024
   logo image for crisp rendering on all display densities.
 - **Toolbar icon size**: Increased default toolbar icon size from 16×16 to
   24×24 for better visibility on modern displays.  Configurable via
   `toolbarIconSize` setting.
 - **Dark theme contrast**: Improved disabled text contrast from #808080 to
   #A0A0A0 (~4.6:1 ratio on dark background, meeting WCAG AA).  Changed
   highlighted text from dark #212121 to white for better readability on
   the blue selection highlight.
 - **Dark theme stylesheet**: Added a baseline QSS stylesheet for the dark
   theme covering buttons, tab bar, scroll bars, tooltips, menus, group
   boxes, and dock widgets for a more polished appearance.
 - **External QSS theme system**: Theme stylesheets are now loaded from
   external `.qss` files instead of inline C++.  Built-in themes (Dark,
   Fusion Light, High Contrast) are embedded as Qt resources.  Users can
   override any theme by placing a `.qss` file in
   `<AppConfigDir>/themes/`.
 - **High Contrast theme**: New "High Contrast" style option for users
   who need maximum contrast — bold borders, high-visibility focus
   indicators, and a black-on-white palette.
 - **Dashboard title bar**: The title bar now shows "Dashboard" instead
   of "Untitled" when the dashboard tab is active.
 - **Dashboard tab label**: The dashboard tab is now labelled "Dashboard"
   instead of "Home" for consistency.

## Accessibility:
 - **Keyboard focus on filter buttons**: The five search filter buttons
   (Match case, Regex, Inverse, Boolean, Auto-refresh) are now keyboard-
   focusable via Tab.  Previously they were set to `NoFocus`.
 - **Tab order**: Added explicit tab order for the search bar and filter
   buttons so keyboard navigation follows a logical left-to-right sequence.
 - **Accessible names**: Added `setAccessibleName()` to the main window,
   tab widget, search input, and all filter buttons for screen reader
   compatibility.
 - **Focus indicators**: All themes now include `:focus` styles for
   buttons, combo boxes, and line edits so the currently focused widget
   is always visible.
 - **Tab bar styling**: Tabs now have rounded top corners, a blue bottom
   border on the active tab, and distinct hover states.
 - **Toolbar layout**: Added separator between action buttons and the path
   info line.  Info fields (size, date, encoding, line count) now have
   consistent horizontal padding.
 - **Scroll bars**: Dark-mode scroll bars are now clearly visible with
   rounded handles and hover highlighting.

## Bug fixes:
 - **Viewport text clipping and overflow**: Corrected rendering bugs in the
   log view that caused text to be clipped at the right edge of the viewport
   and scroll offsets to be miscalculated on wide log lines.

## Tests:
 - Stabilized the `logfiltereddata_test` search helper by draining queued Qt
   events after asynchronous searches complete, preventing Windows teardown
   races in the integration test suite.
 - **Table view extremely slow on large files**: `populateTableModel()` read ALL
   lines into a `QStringList` and then regex-extracted every row upfront, making
   the table view unusable on files with hundreds of thousands of lines.
   The model is now virtual/lazy — it stores a pointer to `AbstractLogData` and
   extracts fields on demand in `data()` with an LRU cache (2000 rows).
 - **Table view column order ignored JSON definition**: Value field columns were
   always sorted alphabetically, ignoring the order defined in the format JSON.
   Column order is now derived from the regex capture group order, which is
   deterministic and matches the order fields appear in the log line.
   Alphabetical sort is used as a fallback when no explicit order is defined.
 - **Table view missing highlighting**: Search matches, marks, highlighter sets,
   quickfind results, and color labels were not rendered in the table view.
   Added a custom `LogTableHighlightDelegate` that paints row-level match/mark
   backgrounds and cell-level text highlighting consistent with the main log view.
 - **Table view missing horizontal scrollbar**: The table view had no horizontal
   scrollbar, making it impossible to see columns that extended beyond the viewport.
   Added `ScrollBarAsNeeded` policy with pixel-level horizontal scrolling and a
   controlled single-step of 10 px per scroll tick.
 - **Table view horizontal scroll too fast**: `ScrollPerPixel` mode with the default
   single step caused the table to scroll much faster horizontally than other views.
   Set `horizontalScrollBar()->setSingleStep(10)` for consistent scroll speed.
 - **Table view columns clipped after reopening file**: Programmatic column width
   changes (auto-sizing, last-column stretching) triggered `saveTableColumnWidths()`,
   persisting inflated widths. On next load these stale widths were restored,
   causing content to be clipped. Added a `programmaticColumnResize_` guard so only
   user-initiated resizes are persisted. Column widths are now always auto-sized
   from actual data on load.
 - **Table view column order non-deterministic**: `LogFieldExtractor::columnNames()`
   iterated over a `QHash` which has random iteration order in Qt 6, causing
   column layout to change between restarts and breaking saved column widths.
   Value definition columns are now sorted alphabetically.
 - **Table view recompiled regex per row**: `LogFormatTableModel::extractRow()`
   created a new `LogFieldExtractor` on every call, recompiling all regex patterns.
   Now reuses the member extractor for significantly better performance on large files.
 - **Hidden fields shown as table columns**: Fields marked `hidden: true` in lnav
   format definitions were still displayed as table columns. They are now excluded
   from `columnNames()` but remain extractable via `extractFields()`.
 - **opid-field missing from table columns**: Formats defining an `opid-field`
   (e.g. syslog's `log_syslog_tag`) never had that field appear in the table view.
   The operation-id field is now included in `columnNames()` alongside `thread-id`.
 - **Text clipping in log view**: `getNbVisibleCols()` double-subtracted the
   vertical scrollbar width from the viewport, causing text to be clipped
   too early on the right side.  Qt's `viewport()->width()` already excludes
   the scrollbar; the redundant subtraction has been removed.
 - **Sidebar opens on startup when plugins register tabs**: The sidebar dock
   was automatically shown whenever a plugin registered a sidebar tab via
   `handlePluginSidebarTab()`.  The sidebar now stays closed by default and
   only opens on explicit user action.
 - Fixed bright green (`#54c01a`) background on input fields in the Light
   theme caused by a corrupted QSS replace.
 - Fixed SIGSEGV crash in `MainWindow::closeTab()` when tab widget returns a
   null widget pointer.  Added null-check guard before casting.
 - Hardened `Session::close()` against double-close scenarios.
 - Removed hardcoded colors from `WelcomeDashboard` — link buttons, hint
   text, and version labels now use `palette()` functions.
 - **Memory leaks on indexing/search interrupt**: `IndexOperation::readFileInBlocks`
   and `FullSearchOperation::run` leaked their per-block buffers when the operation
   was interrupted while a block was awaiting capacity in the TBB flow graph, and
   the indexer additionally leaked on the `read` error and end-of-stream paths.
   The owning paths now release the buffers explicitly when the block is never
   published or accepted.
 - **Potential double-free in `AbstractLogView` destructor**: if `quickFind_->stopSearch()`
   threw, the catch handler deleted the same pointer the `try` block had already
   freed. Restructured to guarantee exactly one delete and reset the pointer to
   `nullptr` afterwards.
 - **Trailing quote not stripped in logical-combining filter**: the
   predefined-filters combobox stripped only the leading `"` before splitting on
   `" or "`, leaving the trailing `"` attached to the last token. The wrapping
   quotes are now stripped symmetrically (with a length guard).
 - **Edge-case underflow in `AbstractLogView::convertViewportPosition`**: when the
   data was cleared while a viewport conversion was in flight, `getNbLine()` could
   return `0` and `LineNumber{0} - 1_lcount` would underflow. Added an explicit
   early return for the empty case.
 - **`endOfLines.back()` on empty result in `LogData::doGetLinesRaw`**: a concurrent
   index truncation could yield an empty offset vector, making the `back()` call
   undefined behavior. The function now returns the partially-populated raw lines
   instead.
 - **`refreshOverview` could crash in release builds**: the function relied on a
   plain `assert(overviewWidget_)` which is stripped in release. Replaced with an
   explicit null guard and a warning log.
 - **`hs_scan` return code was discarded**: the Hyperscan single- and multi-matchers
   ignored the return value, silently producing false negatives on `HS_*` errors.
   Both matchers now log non-success codes.
 - **Empty `catch(...)` in `MainWindow::loadFile` lost diagnostics**: the
   exception type and `what()` are now captured separately for `std::exception` and
   the unknown-exception path.
 - **Chart preset export ignored write failures**: `chartpanel.cpp` now reports
   short writes from `QFile::write` to the user instead of silently producing a
   truncated JSON file.
 - **Index cache crash on empty/truncated files**:
   `CompressedLinePositionStorage::serialize()` invoked `QDataStream::writeRawData`
   with a null buffer pointer when no lines were stored, which is undefined
   behavior (memcpy from nullptr).  ASAN reproduced the crash on the integration
   suite as soon as `perf.useIndexCache=true` was set.  Guarded against zero-size
   and null buffer.
 - **Empty index cache files were created and re-tried on every reindex**:
   `FullIndexOperation::run` now skips `IndexCache::trySave` when the resulting
   index has zero lines so we never write a useless cache entry.
 - **Decompressor silently truncated output on short writes**: the gzip /
   zstd / lz4 decompression worker treated a short `QFile::write` as success.
   It now logs the underlying error and aborts with `success=false` so the user
   sees the dialog error rather than a corrupted temporary file.
 - **`Ctrl+W` on a dashboard-only window did nothing**: when the Welcome
   Dashboard was the only open tab, the close-tab handler short-circuited
   without closing the window.  It now closes the main window in that
   scenario, matching user expectation.
 - **Command Palette could dereference a dangling `QAction`**: the lambda
   stored in each command captured a raw `QAction*` and `triggered()` it on
   accept.  If the source menu rebuilt before the user pressed Enter the
   pointer was already deleted.  Wrapped each capture in `QPointer<QAction>`
   and made `acceptCurrent()` copy the action callback before
   `close()` so the surrounding palette state cannot be invalidated mid-call.
 - **`WelcomeDashboard` HTML-injected plugin name and version**: rich-text
   formatting was applied to plugin metadata strings without escaping, so a
   malicious `plugin.json` could inject markup or links.  Names and versions
   are now passed through `QString::toHtmlEscaped` and the rich-text label
   has `Qt::NoTextInteraction`.
 - **Undefined behavior in `AbstractLogView::lineNumberToVerticalScroll`**:
   for a sentinel `LineNumber` (max `UnderlyingType`), the multiplied double
   exceeded `INT_MAX`, making the `static_cast<int>` undefined behavior
   (caught by UBSan: "1.84467e+19 is outside the range of representable
   values of type 'int'").  The result is now clamped to `[INT_MIN, INT_MAX]`
   before the cast.
 - **`Session::close()` undefined behavior on unknown view**: calling
   `openFiles_.erase( openFiles_.find( view ) )` without checking whether
   `find()` returned `end()` caused undefined behavior when the view was
   not in the map.  Now guarded with an `end()` check.
 - **`Lz4Device::readData()` missing `finished_` check**: after a
   decompression error or end-of-stream, subsequent `readData()` calls
   could re-enter the decompression loop with an invalid context.  Added
   `finished_` guard consistent with `ZstdDevice`.
 - **`Lz4Device` / `ZstdDevice` decompression context leaked on error**:
   when `LZ4F_decompress()` or `ZSTD_decompressStream()` returned an error
   the decompression context was left allocated in an invalid state.  Now
   freed and set to `nullptr` on error.
 - **`Lz4Device::open()` did not clear `dctx_` on failure**: if
   `LZ4F_createDecompressionContext()` failed, the potentially non-null
   pointer was left dangling.  Now explicitly set to `nullptr`.
 - **`ZstdDevice::open()` leaked context on double-open**: calling `open()`
   twice without `close()` overwrote the existing `ZSTD_DCtx*`, leaking it.
   Now frees the old context before creating a new one.
 - **Decompressor infinite loop on stalled device**: if the decompression
   `QIODevice` returned an empty buffer while `atEnd()` was `false`, the
   read loop spun forever.  Now breaks and logs the error.
 - **`indexCacheMaxSizeMb` overflow**: no bounds validation on the
   QSettings value meant a negative or excessively large integer caused
   overflow when multiplied by `1024 * 1024`.  Clamped to `[0, 8 000 000]`.
 - **`IndexCache::evict()` silently ignored failed file removals**:
   `QFile::remove()` return value was not checked, and `totalSize` was
   decremented even when the deletion failed, breaking the eviction
   accounting.  Now checks the return value and logs failures.
 - **`IndexCache::trySave()` ignored `QDir::mkpath()` failure**: the cache
   directory creation was not checked, producing a confusing "cannot open
   for writing" error.  Now returns early with a descriptive warning.
 - **Crash in `MainWindow::openFileByName` on foreign window cast**: the
   `qobject_cast<MainWindow*>(existing_crawler->window())` result was
   dereferenced without a null check; if the crawler widget was reparented
   into a non-`MainWindow` top-level, the cast returned `nullptr` and the
   subsequent method calls crashed.  Added a null guard.
 - **Crash in `MainWindow::updateInfoLine` on null crawler**: three
   consecutive calls to `currentCrawlerWidget()` were made without a null
   check.  When no tab was open, `encodingField->setText(nullptr->…)` caused
   a segfault.  The result is now stored once and guarded at the top of the
   function.
 - **Out-of-bounds access in `updateRecentFileActions`**: the loop condition
   checked `j < recent_files_max_items` but not `j < recent_files.size()`,
   so a stale config with a higher display count than the actual list size
   would read past the end.  Added the missing size guard.
 - **Invalid highlighter regex caused silent match failures**:
   `Highlighter::compile()` never checked `QRegularExpression::isValid()`
   after construction, so a malformed user regex was silently passed to
   `globalMatch()` which returned zero matches.  Now validates and falls
   back to an escaped literal with a warning log.
 - **`LogFilteredData::doGetLineString` passed sentinel to source data**:
   when `findLogDataLine()` returned `maxValue<LineNumber>()` (out-of-bounds
   sentinel), it was forwarded to `sourceLogData_->getLineString()`, which
   eventually fell through to a warning-string path but did unnecessary work.
   Both `doGetLineString` and `doGetExpandedLineString` now return an empty
   `QString` immediately on sentinel.

## Build / internal:
 - Fixed broken header guard in `src/utils/include/dispatch_to.h` (missing
   `#define` after `#ifndef`); the header was effectively re-included in every
   translation unit that pulled it in.
 - Renamed the misspelled struct `WatchedDirecotry` → `WatchedDirectory` in
   `src/filewatch/src/filewatcher.cpp`.

## Tests:
 - Added `tests/e2e/test_audit_regressions.py` with 11 regression tests covering
   the bug fixes above (empty/tiny files, repeated grep cycles, GUI clean
   shutdown, quote-strip specification, missing-file diagnostics, regex
   correctness).
 - Extended the audit regression suite with coverage for the round-2 fixes
   (dashboard-only close behaviour, command-palette safety smoke test, and a
   placeholder for the gzip short-write path which is GUI-only).
 - Refreshed `tests/e2e/baseline.json` to reflect the updated grep startup
   cost introduced upstream by the compression-dashboard feature work
   (PR #53).  All performance tests pass within the 5 % tolerance again.

## CI/CD:
 - **Sequential build pipeline**: Restructured GitHub Actions to run builds
   sequentially by runner cost (Linux → Windows → macOS) instead of in
   parallel.  A failure in a cheaper stage prevents burning expensive
   runner minutes (Linux 1×, Windows 2×, macOS 10×).
 - **E2E artifact name alignment**: Fixed E2E test failures caused by stale
   artifact names that no longer matched the build matrix (`jammy` → `noble`,
   `macos` → `mac-arm64`, `windows` → `windows-x64`).
 - **Windows E2E compression tools**: Removed non-existent `lz4` Chocolatey
   package from the Windows E2E install step.  LZ4 decompression tests are
   skipped gracefully on Windows.

---

# 26.04.2 (2026-04-19):

## Bug fixes:
 - Fixed "Clear File" (Ctrl+X) not clearing the file.  `QMessageBox::warning()`
   only shows an OK button, so the `== QMessageBox::Yes` check always failed.
   Replaced with `QMessageBox::question()` using explicit Yes/No buttons
   (No as default).  (Fixes [#44](https://github.com/64x-lunicorn/LogSquirl/issues/44))
 - Fixed SIGSEGV crash during `LogFilteredData` destruction on Windows CI.
   The `KDSignalThrottler` member could emit a pending signal during teardown,
   invoking a slot on the partially-destroyed object.  Added an explicit
   destructor that disconnects all signals before member destruction.

---

# 26.04.1 (2026-04-19):

## New features:
 - **Tab Group Manager dialog**: Tools → "Manage Tab Groups…" opens a
   dedicated dialog to rename, recolor, and delete tab groups without
   navigating the per-tab context menu.
 - **Log Merge**: right-click a tab → "Merge All Left" / "Merge All Right"
   to concatenate logs from neighboring tabs into a virtual merged tab.
   Supports optional exact-line deduplication and live-updates when source
   files change (300 ms debounce).
 - **Breadcrumbs (Context Lines)**: configurable ±N context lines around
   matches/marks in the filtered view.  Set via Options → View →
   "Context lines around matches" (QSpinBox, 0–50, default 0 = off).
   Context lines are dimmed (50 % opacity) and include the `Context`
   line-type flag.  Overlapping contexts merge automatically.
 - **Chart Panel (Chipmunk-style)**: View → "Chart Panel" toggles an
   interactive chart pane below the filtered view.  Define regex-based
   series with numeric capture groups to extract and plot values across the
   log file.  Features: line/scatter chart with zoom (mouse wheel), pan
   (middle-drag), click-to-navigate (left click jumps to the source line),
   hover tooltips, multiple series with independent colors, and automatic
   data refresh on file reload.  No external dependencies — uses a custom
   QPainter-based rendering engine.
 - **Chart X-Axis Extraction**: chart series can now use a custom regex to
   extract X-axis values from log lines (timestamp or numeric).  Configure
   via the "X-Axis" group in the series dialog.
 - **Timestamp X-Axis**: parse timestamps with configurable QDateTime format
   (e.g. `MM-dd HH:mm:ss.zzz`).  Auto-defaults to current year for formats
   without a year component.
 - **Time Aggregation / Bucketing**: group data points into configurable time
   buckets (100 ms, 500 ms, 1 s, 5 s, 10 s, 30 s, 1 min, 5 min) and sum
   Y values per bucket — ideal for spotting activity peaks.
 - **Per-Document Chart Persistence**: chart series and panel visibility are
   automatically saved and restored when reopening a file.  Stored in the
   existing session context (JSON keys `CS` and `CV`).
 - **App-Level Chart Presets**: save named chart configurations via
   Save Preset / Load Preset / Delete Preset toolbar actions.  Presets
   persist across sessions in the application settings.
 - **Chart Export / Import**: export current series to a JSON file and import
   from JSON files — share chart configurations between machines or users.
 - **Filter Frequency Chart**: View → "Show Filter Frequency" creates
   count-mode chart series (one per search sub-pattern) from the active
   search text and auto-shows the chart panel.
 - **JWT Decoder improvements**: `extractToken()` now uses regex-based
   extraction to find JWTs in arbitrary text.  Fixed multi-line input
   handling and added support for tokens embedded in log lines.
 - Replaced Hyperscan with Vectorscan as the SIMD regex backend on Linux
   and macOS.  Vectorscan is a maintained, API-compatible fork of Intel
   Hyperscan with native ARM/NEON support.  On Windows the MSVC-compatible
   variar/hyperscan fork is used (same hs_* API as Vectorscan).
 - Enabled Vectorscan FAT_RUNTIME: the binary now auto-selects the fastest
   SIMD path at runtime (SSE2 → SSE4.2 → AVX2 → AVX512).
 - Enabled AVX2 and AVX512 code generation in Vectorscan for higher
   throughput on modern CPUs.
 - Overhauled E2E performance benchmark infrastructure:
   - Increased default runs from 5 to 21 with 3 warmup iterations
   - Added IQR-based outlier filtering for stable measurements
   - Full statistical analysis: median, mean, std, CV%, P5/P95, IQR
   - Welch's t-test for statistically significant regression detection
   - Auto-generated benchmark report (Markdown or JSON) with throughput
     metrics (MB/s, lines/sec), stability analysis, and system info
   - 8 new benchmarks: case-insensitive, alternation, no-match overhead,
     10/50/100 MB large-file tests, and UTF-16 at scale (14 total)
   - Configurable via --bench-runs, --bench-warmup, --bench-report
 - Added generate_test_data.py script for creating large test files
   (10/50/100 MB) from existing 1 MB test data
 - Tab Grouping: organize open tabs into named, colored groups via the tab
   context menu.  Grouped tabs display a colored bullet prefix (●) and tinted
   text.  Groups support rename, recolor, close-all-in-group, and ungroup-all
   operations.  Group membership persists across sessions.
 - Tab Close Confirmation: optional confirmation dialog when closing tabs
   (single or bulk).  Includes a "Don't ask again" checkbox; the preference
   can be re-enabled under Options > General > Session options.

## Performance:
 - Windows: enabled MSVC `/arch:AVX2` code generation for local builds
   (non-generic CPU).  This allows the compiler to emit AVX2 instructions
   for hot loops in the log parser and search engine.
 - Windows: enabled Hyperscan `BUILD_AVX2` for regex pattern matching with
   AVX2 SIMD instructions on non-generic builds.
 - Added Windows-specific performance baselines (`baseline-windows.json`)
   and OS-aware baseline loading in E2E tests.

## Bug fixes:
 - Fixed ambiguous `Roaring64Map::contains()` / `add()` overload on
   GCC/Linux where `unsigned long long` differs from `uint64_t`.
 - Windows: embedded application manifest (`asInvoker`) to eliminate
   SmartScreen "unknown publisher" security warnings on first launch.
   Manifest also enables per-monitor DPI v2, long-path awareness, and
   Windows Segment Heap for improved memory performance.
 - Windows portable: fixed TLS/HTTPS errors when connecting to the GitHub
   plugin repository or checking for updates.  The Qt TLS backend plugins
   (`qopensslbackend.dll`, `qschannelbackend.dll`) are now included in
   both the portable ZIP and NSIS installer distributions.
 - Fixed missing assert_performance call in UTF-16 1MB benchmark
 - Fixed typo in Configuration::setRegexpEnging → setRegexpEngine
 - Fixed typo in Vectorscan CMake option names (BUIlD_AVX2 → BUILD_AVX2)
 - Plugin Management dialog now displays the SPDX license identifier for
   each plugin (read from plugin.json metadata and remote catalog)
 - Added license field to CatalogEntry and RepositoryEntry structs
 - Added license field to plugins.json registries (v1 and v2 schemas)

## Build:
 - Replaced legacy `codesign_client.exe` CI hooks with `signtool.exe`-based
   code signing.  Signing is now opt-in via `sign-cert-pfx` and
   `sign-cert-password` inputs to the Windows packaging action.
 - Bulk tab close operations (close others / left / right / all) now present
   a single confirmation dialog instead of per-tab prompts.

## Website:
 - Migrated website from Hugo to Starlight (Astro 6). Modern documentation
   site with full-text search, responsive design, and sitemap generation.
 - Updated deploy workflow for Node.js/npm build pipeline.

## Refactoring:
 - Removed LOGSQUIRL_USE_HYPERSCAN CMake option (replaced by LOGSQUIRL_USE_VECTORSCAN)
 - Removed cmake/Findhyperscan.cmake find module
 - Removed Hyperscan CPM dependency (variar/hyperscan fork)
 - Renamed RegexpEngine::Hyperscan enum to RegexpEngine::Vectorscan
 - Updated UI options dialog label from "Hyperscan" to "Vectorscan"
 - Updated NOTICE attribution for Vectorscan
 - Updated Gentoo ebuild dependency from hyperscan to vectorscan
 - Updated CI workflows to remove explicit Hyperscan flags
 - Upgraded baseline.json to schema v2 with system info and raw run data

---


# 26.03.2-beta (2026-03):

## New features:
 - Unified Plugin Management dialog: replaced separate "Manage Plugins" and
   "Browse Plugins" dialogs with a single card-based "Plugin Management" dialog.
   Shows installed, available, and updatable plugins with icons, status badges,
   and one-click install/update/enable/disable actions.
 - Decentralized plugin registry (schema v2): the central plugins.json now contains
   only lightweight catalog entries.  Per-plugin releases.json files hosted in each
   plugin repository provide version and platform details.
 - Plugin icon support: plugins can specify an "icon" field in plugin.json.
   Icons are fetched from the registry and displayed in the management dialog.
 - Schema v1 backward compatibility: the host gracefully falls back to the
   legacy flat plugins.json format when schema_version is 1.
 - Added Plugin Sidebar Tabs: plugins can now register sidebar tabs via
   `register_sidebar_tab()` / `unregister_sidebar_tab()` in the Host API.
   Tabs appear in a dockable sidebar panel next to the main view.

## Bug fixes:
 - Fixed "Help → Report Issue" opening GitHub with percent-encoded body text
   (e.g. `Details%20for%20the%20issue`) instead of readable content

---

# 26.03.1-beta (2026-03):

## New features:
 - Added Plugin Infrastructure: C ABI-based plugin system supporting data source,
   converter, and UI extension plugins. Includes plugin discovery, loading,
   lifecycle management, and a host API for plugins to interact with the application.
   See `docs/plugin-sdk.md` for the developer guide.
 - Added Plugins menu in the menu bar with "Manage Plugins..." dialog
 - Added Sources menu: start data source plugins directly from the menu bar;
   streamed log lines appear in a follow-mode tab backed by a temp file
 - Added converter plugin support: file extensions registered by converter plugins
   are shown in the Open File dialog; files are converted to a temp file before display
 - Added Plugin Repository: "Browse Plugins..." dialog fetches a remote plugins.json
   index, displays available plugins, and downloads archives with SHA-256 verification
 - Automatic plugin installation: "Browse Plugins..." now downloads, extracts,
   discovers, and loads plugins in a single click — no manual extraction or
   restart required. Updates unload the existing version before overwriting.
   Failed extractions are rolled back automatically.
 - Added optional Lua scripting layer (LOGSQUIRL_USE_LUA=ON): write plugins as Lua
   scripts using sol2; supports DataSource, Converter, and UI Extension plugin types
 - Added plugin auto-load: enabled plugins are automatically loaded on startup
   based on persisted configuration; toggle via "Manage Plugins..." dialog
 - Improved "Manage Plugins..." dialog: now uses checkboxes per plugin with
   auto-load toggle; plugin enable/disable state persists across restarts
 - DataSource menu entries now auto-load the plugin on first use if not loaded

## Build:
 - Upgraded C++ standard from C++17 to C++23
 - Dropped Qt5 support — Qt6 is now the only supported version
 - Removed all `QT_VERSION_MAJOR` conditionals and Qt5 compatibility code paths
 - Updated minimum compiler requirements: GCC 13, Clang 17, MSVC 19.36

## Continuous integration:
 - Upgraded macOS CI runner from macos-15 to macos-26
 - Dropped macOS Intel build — now ARM64 only
 - Updated macOS deployment target to 15.0
 - Upgraded Windows CI runner to windows-2025

## Tests:
 - Added E2E integration test suite (pytest-based, 40 tests covering search correctness, encoding handling, edge cases, GUI smoke tests, and performance regression detection)

---

# 26.03.0 (2026-03):

This is the first release of LogSquirl, a GPL-3.0 fork of [klogg](https://github.com/variar/klogg).

## New features:
 - Added Filter Groups: organize predefined filters into named groups (like highlighter groups) with a redesigned management dialog, expandable tree sidebar with tri-state checkboxes, and group-aware Chipmunk import
 - Rebranded project from klogg to LogSquirl with new bundle identifier `io.github.logsquirl`
 - Added JWT token decoder to Scratchpad: decodes Base64URL header/payload, formats JSON with indentation, and annotates epoch timestamps (iat, exp, nbf, auth_time) with human-readable UTC dates
 - Added Filters Panel: right sidebar dock with tabbed Filters and Scratchpad panels, toolbar filter icon, auto-search on toggle, and pinned filters that persist across sessions
 - Added Chipmunk filter import: import filters and highlighters from Chipmunk JSON export files via Tools menu
 - Added opt-in beta update channel: new "Check for beta updates" checkbox in Settings > General. When enabled, the app checks for beta versions on every startup (bypassing the 7-day interval) and shows notifications with "(Beta)" label
 - Replaced toolbar Filter/Scratchpad buttons with a single Sidebar toggle button

## Bug fixes:
 - Fixed crash on shutdown with Qt 6.10 on Windows (QThreadPool::waitForDone SEGFAULT) — resolved mutex deadlock in LogDataWorker and LogFilteredDataWorker destructors where waitForDone() was called while holding operationsMutex_, preventing pool threads from completing
 - Fixed LogData destructor not disconnecting FileWatcher before shutdown, preventing late fileChanged signals from enqueuing operations during teardown
 - Fixed BOM not written when saving search results to file for UTF-16 encoded logs
 - Fixed OpenSSL upgraded from 1.1.x to 3.x (CVE-2022-1292)
 - Fixed Float (undock) button icon not visible in dark mode

## Build system:
 - Upgraded to Qt 6.10.3 as primary build target (Qt 5 still supported)
 - Bumped minimum CMake version guidance; added CPM dependency management
 - Added Fedora 43 and Oracle Linux 10 build targets
 - Upgraded robin_hood to 3.11.5 with GCC 14 compatibility patch
 - Patched KArchive for Qt 6.10 `.arg()` compatibility
 - Replaced unreliable AppleScript DMG layout with pre-built DS_Store file
 - Fixed ragel being shadowed by CI workspace mount on Oracle Linux 10

## Continuous integration:
 - Upgraded deprecated GitHub Actions to latest versions (actions/checkout v4, etc.)
 - Upgraded CodeQL to v4
 - Upgraded CI runners: macOS 15 (ARM + Intel), Windows 2025, Ubuntu 24.04
 - Fixed Windows packaging: removed stale Qt/TBB DLLs, updated NSIS installer - Fixed release workflow triggering on tags from non-master branches - Added `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` to suppress Node.js 20 deprecation warnings
 - Fixed macOS DMG appearing empty by replacing CPack DragNDrop with create-dmg (proper Applications symlink and icon layout)
 - Fixed macOS Gatekeeper rejection by signing each nested component individually with hardened-runtime entitlements instead of --deep
 - Added entitlements.plist for hardened runtime code signing
 - Added LSMinimumSystemVersion to Info.plist for deployment target correctness
 - Added notarization status validation — CI now fails when notarization is rejected
 - Modernized CI/CD pipeline: pre-built Docker images on GHCR, tag-based releases (stable + beta), concurrency groups, CPM dependency caching, non-blocking clang-tidy lint job, reusable build workflow via workflow_call
 - Replaced deprecated `marvinpinto/action-automatic-releases` with `softprops/action-gh-release@v2` — single release per tag with all platforms bundled
 - Replaced `mathrix-education/setup-sentry-cli` with direct sentry-cli install; Sentry jobs now run as non-blocking (`continue-on-error`) and are guarded by token presence
 - Split release workflow into separate jobs: build, prepare, sentry, release, update-metadata
 - Dropped Qt5 from CI: Jammy Dockerfile upgraded to Qt6, removed Qt5 logic from prepare-workspace-env
 - Removed dead CI files: docker/ubuntu20.04, docker/ubuntu20.04_qt5.15, docker/oracle7, docker/oracle8, test_env.yml
 - Extracted macOS framework symlink repair into standalone script (`scripts/fix_macos_frameworks.sh`)
 - Added auto-changelog generation in release workflow using `scripts/gen_changelog.py`

## Tests:
 - Added unit tests for regex, encoding, line types, and configuration modules

---

# 2022-06:
## Documentation:
 - [d711ddeb](https://github.com/variar/klogg/commit/d711ddeb): update build documentation [skip ci] (Anton Filimonov)
## Code refactoring:
 - [cf3947a2](https://github.com/variar/klogg/commit/cf3947a2): move io thread for regex matching outside of TBB (Anton Filimonov)
 - [35772f81](https://github.com/variar/klogg/commit/35772f81): simplify operations queue (Anton Filimonov)
## Build system:
 - [f61a2dc4](https://github.com/variar/klogg/commit/f61a2dc4): fix qt6 windows build (Anton Filimonov)
 - [12b36c62](https://github.com/variar/klogg/commit/12b36c62): use host version for osx deployment target by default (#481) (Anton Filimonov)
 - [d1ecc595](https://github.com/variar/klogg/commit/d1ecc595): fix deprecated QFontDatabase use (#481) (Anton Filimonov)
 - [29b40f99](https://github.com/variar/klogg/commit/29b40f99): use osx target 10.15 for Qt6 on Mac [skip ci] (#451) (Anton Filimonov)
## Continuous integration workflow:
 - [cca81764](https://github.com/variar/klogg/commit/cca81764): build packages for ubuntu 22.04 and fixes for Qt6 packaging (Anton Filimonov)
 - [af4ef1c3](https://github.com/variar/klogg/commit/af4ef1c3): install qt5compat for qt6 build on windows (Anton Filimonov)
 - [b55fde62](https://github.com/variar/klogg/commit/b55fde62): add qt6 packages for windows (Anton Filimonov)
 - [e3e9b246](https://github.com/variar/klogg/commit/e3e9b246): split workflow to smaller actions (Anton Filimonov)
 - [56d08c2c](https://github.com/variar/klogg/commit/56d08c2c): fix macos artifacts publishing [skip ci] (Anton Filimonov)
 - [043e864d](https://github.com/variar/klogg/commit/043e864d): use correct paths for Qt6 on mac (Anton Filimonov)
 - [de85fe7f](https://github.com/variar/klogg/commit/de85fe7f): switch for Qt6 LTS release for mac qt6 build (Anton Filimonov)
 - [8b522407](https://github.com/variar/klogg/commit/8b522407): install qt5compat for mac qt6 build (Anton Filimonov)
 - [3fe69f1c](https://github.com/variar/klogg/commit/3fe69f1c): try building with Qt6 on Mac (Anton Filimonov)
 - [ec58a1d3](https://github.com/variar/klogg/commit/ec58a1d3): fix sed for mac distribution.xml (Anton Filimonov)
 - [32c34b10](https://github.com/variar/klogg/commit/32c34b10): make better macos pkg (Anton Filimonov)
 - [6503e127](https://github.com/variar/klogg/commit/6503e127): fix mac pkg code sign (Anton Filimonov)
 - [553d2937](https://github.com/variar/klogg/commit/553d2937): import mac certs in one action (Anton Filimonov)
 - [0632e2fc](https://github.com/variar/klogg/commit/0632e2fc): let dpkg figure out dependencies for deb package (Anton Filimonov)
 - [e41b3353](https://github.com/variar/klogg/commit/e41b3353): add mac keys to same keychain (Anton Filimonov)
 - [91d5844b](https://github.com/variar/klogg/commit/91d5844b): try make better RPM deps (Anton Filimonov)
 - [51617738](https://github.com/variar/klogg/commit/51617738): add install cert to separate keychain (Anton Filimonov)
 - [d8e244a3](https://github.com/variar/klogg/commit/d8e244a3): make better deb packages (Anton Filimonov)
 - [6b50bf92](https://github.com/variar/klogg/commit/6b50bf92): fix mac codesign (Anton Filimonov)
 - [e6ac5502](https://github.com/variar/klogg/commit/e6ac5502): build pkg for mac (Anton Filimonov)
 - [789ad321](https://github.com/variar/klogg/commit/789ad321): use native compilers for each linux container (#480) (Anton Filimonov)
 - [b11582b9](https://github.com/variar/klogg/commit/b11582b9): fix path for macdeployqt (Anton Filimonov)
 - [cf41e9db](https://github.com/variar/klogg/commit/cf41e9db): fix mac build in CI (Anton Filimonov)
 - [a852c8e4](https://github.com/variar/klogg/commit/a852c8e4): add 3rdparty sources snapshot to release artifacts (Anton Filimonov)
 - [d4f7b34f](https://github.com/variar/klogg/commit/d4f7b34f): add 3rdparty sources snapshot to release artifacts (Anton Filimonov)
 - [e83ace65](https://github.com/variar/klogg/commit/e83ace65): don't depend on tests libs if not building tests (Anton Filimonov)
## Code refactoring:
 - [f88365f5](https://github.com/variar/klogg/commit/f88365f5): Code refactor, utilize unique_ptr and unused vars, lower scope code and using std::move() (#478) (Herman Semenov)
# 2022-05:
## Documentation:
 - [fb645849](https://github.com/variar/klogg/commit/fb645849): add AppImage to readme [skip ci] (Anton Filimonov)
## Tests:
 - [d92cae8d](https://github.com/variar/klogg/commit/d92cae8d): fix tests (Anton Filimonov)
 - [d759c438](https://github.com/variar/klogg/commit/d759c438): fix tests (Anton Filimonov)
## Build system:
 - [4f03e86a](https://github.com/variar/klogg/commit/4f03e86a): fix mac build (Anton Filimonov)
 - [17a87941](https://github.com/variar/klogg/commit/17a87941): fix build containers (Anton Filimonov)
 - [78a79c87](https://github.com/variar/klogg/commit/78a79c87): fix build containers (Anton Filimonov)
 - [3008f573](https://github.com/variar/klogg/commit/3008f573): add missing dep (Anton Filimonov)
## Continuous integration workflow:
 - [9d9215ee](https://github.com/variar/klogg/commit/9d9215ee): upload all artifacts to releases [skip ci] (Anton Filimonov)
 - [a86c5dba](https://github.com/variar/klogg/commit/a86c5dba): make different names for linux packages (Anton Filimonov)
 - [f5dc701f](https://github.com/variar/klogg/commit/f5dc701f): fix appimage packaging (Anton Filimonov)
 - [f0b6325b](https://github.com/variar/klogg/commit/f0b6325b): remove jammy, add oracle linux 8 (Anton Filimonov)
 - [9203ec54](https://github.com/variar/klogg/commit/9203ec54): build packages for more platforms (Anton Filimonov)
# 2022-04:
## New features:
 - [bcb71b50](https://github.com/variar/klogg/commit/bcb71b50): add a replace data in scratchpad action (#408) (Anton Filimonov)
## Documentation:
 - [156f4c77](https://github.com/variar/klogg/commit/156f4c77): update documentation for new release (Anton Filimonov)
## Build system:
 - [97189cf2](https://github.com/variar/klogg/commit/97189cf2): update tbb sha (Anton Filimonov)
 - [9d401a57](https://github.com/variar/klogg/commit/9d401a57): macdeployqtfix still uses python2 (Anton Filimonov)
 - [d40e78f4](https://github.com/variar/klogg/commit/d40e78f4): use absolute path for macdeployqtfix (Anton Filimonov)
 - [057dfd0e](https://github.com/variar/klogg/commit/057dfd0e): copy tbb libs from new location (Anton Filimonov)
 - [05aaadc2](https://github.com/variar/klogg/commit/05aaadc2): use python to call macdeployqt script (Anton Filimonov)
 - [8121e34a](https://github.com/variar/klogg/commit/8121e34a): use logsquirl fork of tbb for static build (Anton Filimonov)
 - [05d9a867](https://github.com/variar/klogg/commit/05d9a867): fix macdeployqtfix path (Anton Filimonov)
 - [0d6f9f13](https://github.com/variar/klogg/commit/0d6f9f13): copy tbb shared lib for win32 (Anton Filimonov)
 - [c5efb31f](https://github.com/variar/klogg/commit/c5efb31f): use shared tbb only for Win (Anton Filimonov)
 - [96d8b6aa](https://github.com/variar/klogg/commit/96d8b6aa): fix hyperscan includes (Anton Filimonov)
 - [286e78cc](https://github.com/variar/klogg/commit/286e78cc): don't use deprecated method (Anton Filimonov)
 - [8ba6b304](https://github.com/variar/klogg/commit/8ba6b304): fix uchardet include (Anton Filimonov)
 - [4cb6a1a6](https://github.com/variar/klogg/commit/4cb6a1a6): add git to build containters (Anton Filimonov)
 - [0c0fde02](https://github.com/variar/klogg/commit/0c0fde02): use own fork of hyperscan (Anton Filimonov)
 - [8d8ef200](https://github.com/variar/klogg/commit/8d8ef200): fix uchardet (Anton Filimonov)
 - [0ae6d42a](https://github.com/variar/klogg/commit/0ae6d42a): use CPM to get external dependencies (Anton Filimonov)
## Continuous integration workflow:
 - [2e3ee10c](https://github.com/variar/klogg/commit/2e3ee10c): workaround for Github broken elastic index for workflow runs [skip ci] (Anton Filimonov)
 - [45dc0279](https://github.com/variar/klogg/commit/45dc0279): add more tracing to debug failing test (Anton Filimonov)
 - [d7d890bb](https://github.com/variar/klogg/commit/d7d890bb): copy tbb only on win32 (Anton Filimonov)
# 2022-03:
## New features:
 - [fed53dee](https://github.com/variar/klogg/commit/fed53dee): show scratchpad when send data to it (#408) (Anton Filimonov)
## Documentation:
 - [901e4ade](https://github.com/variar/klogg/commit/901e4ade): update list of sponsors and contributors [skip ci] (Anton Filimonov)
## Build system:
 - [4c39210f](https://github.com/variar/klogg/commit/4c39210f): fix qt6 build on windows (Anton Filimonov)
 - [8b66d11a](https://github.com/variar/klogg/commit/8b66d11a): fix crashpad build on windows (Anton Filimonov)
 - [c5c70229](https://github.com/variar/klogg/commit/c5c70229): don't include 3rdparty readme into packages (Anton Filimonov)
 - [16225288](https://github.com/variar/klogg/commit/16225288): fix build for qt 5.9 (Anton Filimonov)
## Continuous integration workflow:
 - [ef8456bc](https://github.com/variar/klogg/commit/ef8456bc): add ts to codesign requests (Anton Filimonov)
 - [683c1c35](https://github.com/variar/klogg/commit/683c1c35): revert to windows 2019 runner (Anton Filimonov)
 - [b855c762](https://github.com/variar/klogg/commit/b855c762): update vcredist paths (Anton Filimonov)
 - [5aa5f8ce](https://github.com/variar/klogg/commit/5aa5f8ce): check new paths on windows [skip ci] (Anton Filimonov)
## Other commits:
 - [400da159](https://github.com/variar/klogg/commit/400da159): Revert "chore: update sentry to 0.4.15" (Anton Filimonov)
# 2022-02:
## Build system:
 - [8002ed8a](https://github.com/variar/klogg/commit/8002ed8a): fix build on old Qt (Anton Filimonov)
## Continuous integration workflow:
 - [4e403031](https://github.com/variar/klogg/commit/4e403031): remove rpm check for now as centos is obsolete (Anton Filimonov)
 - [243b958f](https://github.com/variar/klogg/commit/243b958f): use rhel8 to check rpm (Anton Filimonov)
 - [8fe1b1d6](https://github.com/variar/klogg/commit/8fe1b1d6): update centos8 docker to point to vault.centos.org (Anton Filimonov)
# 2022-01:
## Continuous integration workflow:
 - [aff1f1ce](https://github.com/variar/klogg/commit/aff1f1ce): sign uninstaller (Anton Filimonov)
## Other commits:
 - [3a561a2a](https://github.com/variar/klogg/commit/3a561a2a): [skip ci] update latest version (Anton Filimonov)
# 2021-12:
## New features:
 - [e874eb12](https://github.com/variar/klogg/commit/e874eb12): add more shortcuts (#407, #408) (Anton Filimonov)
 - [fb478e62](https://github.com/variar/klogg/commit/fb478e62): quick send selection to scratchpad (#408) (Anton Filimonov)
 - [80771eeb](https://github.com/variar/klogg/commit/80771eeb): reset color labeling cycle when clearing all labels (#270) (Anton Filimonov)
 - [f48dda65](https://github.com/variar/klogg/commit/f48dda65): allow to configure dark palette (#430) (Anton Filimonov)
 - [7bb565a3](https://github.com/variar/klogg/commit/7bb565a3): allow to set default encoding (#431) (Anton Filimonov)
## Continuous integration workflow:
 - [5a07c58e](https://github.com/variar/klogg/commit/5a07c58e): add win codesigning back (Anton Filimonov)
 - [a8a4eb06](https://github.com/variar/klogg/commit/a8a4eb06): add verbose output for macos notarize actions (Anton Filimonov)
## Build system:
 - [1a7a5a4d](https://github.com/variar/klogg/commit/1a7a5a4d): Revert "fix: fix link order for new tbb" (Anton Filimonov)
# 2021-11:
## Bug fixes:
 - [e75f29ab](https://github.com/variar/klogg/commit/e75f29ab): Set preferences dialog for Mac (#428) (Anton Filimonov)
## Continuous integration workflow:
 - [f1380935](https://github.com/variar/klogg/commit/f1380935): Remove codesign on windows (Anton Filimonov)
 - [94408bb7](https://github.com/variar/klogg/commit/94408bb7): Fix openssl link (Anton Filimonov)
# 2021-10:
## Build system:
 - [6d4535cd](https://github.com/variar/klogg/commit/6d4535cd): Use Qt6 if available (Stephan Vedder)
# 2021-09:
## New features:
 - [5c6c4008](https://github.com/variar/klogg/commit/5c6c4008): use better color labels (#270) (Anton Filimonov)
 - [01d942b8](https://github.com/variar/klogg/commit/01d942b8): add colors to context menu on win (Anton Filimonov)
## Code refactoring:
 - [6f503b99](https://github.com/variar/klogg/commit/6f503b99): make serial graph node explicit (Anton Filimonov)
 - [72cfdc8c](https://github.com/variar/klogg/commit/72cfdc8c): make indexing graph more readable and add traces (Anton Filimonov)
## Continuous integration workflow:
 - [d7f39db6](https://github.com/variar/klogg/commit/d7f39db6): add manual release actions (#380)[skip ci] (Anton Filimonov)
## Other commits:
 - [a7cf50f5](https://github.com/variar/klogg/commit/a7cf50f5): Fixing typo in README (#400)  (by @Lightjohn) (Jonathan)
# 2021-08:
## New features:
 - [5ba7ba91](https://github.com/variar/klogg/commit/5ba7ba91): add manifest for scoop installer [skip ci] (Anton Filimonov)
 - [9c30baa3](https://github.com/variar/klogg/commit/9c30baa3): improve boolean expression error messages (#389) (Anton Filimonov)
 - [b9ded7d9](https://github.com/variar/klogg/commit/b9ded7d9): add dark style based on windows qt style (#394) [ci release] (Anton Filimonov)
 - [cfe29e6d](https://github.com/variar/klogg/commit/cfe29e6d): allow to activate multiple highlighter sets (#360) (Anton Filimonov)
 - [c2422374](https://github.com/variar/klogg/commit/c2422374): allow to edit color label names (Anton Filimonov)
 - [939e1121](https://github.com/variar/klogg/commit/939e1121): remove single line selection fill (#385) (Anton Filimonov)
 - [4963c2c0](https://github.com/variar/klogg/commit/4963c2c0): allow to select cycling color labels (#270) (Anton Filimonov)
 - [c104fd24](https://github.com/variar/klogg/commit/c104fd24): add context menu for color labels (#270) (Anton Filimonov)
 - [0aa6f8cf](https://github.com/variar/klogg/commit/0aa6f8cf): allow to turn off quick highlight with cycle key (#270) (Anton Filimonov)
 - [4331885b](https://github.com/variar/klogg/commit/4331885b): allow to hide ansi color sequences in displayed text (#338) (Anton Filimonov)
 - [857ddf8a](https://github.com/variar/klogg/commit/857ddf8a): better copy text handling for path line (Anton Filimonov)
 - [355412ac](https://github.com/variar/klogg/commit/355412ac): allow to configure quick highlighters (#270) (Anton Filimonov)
## Documentation:
 - [f2a42b98](https://github.com/variar/klogg/commit/f2a42b98): add section about docs to contributing guide [skip ci] (Anton Filimonov)
 - [1b727814](https://github.com/variar/klogg/commit/1b727814): merge changes from lilventi/logsquirl [skip ci] (Anton Filimonov)
 - [c483723d](https://github.com/variar/klogg/commit/c483723d): fix typos (Anton Filimonov)
## Code refactoring:
 - [567df0c9](https://github.com/variar/klogg/commit/567df0c9): add logsquirl_ prefix to all logsquirl libs (Anton Filimonov)
 - [8f9feef1](https://github.com/variar/klogg/commit/8f9feef1): move logdata headers to top (Anton Filimonov)
 - [c8e011ea](https://github.com/variar/klogg/commit/c8e011ea): move regex wrappers to its own library (Anton Filimonov)
 - [9fc68073](https://github.com/variar/klogg/commit/9fc68073): fix unneeded initialization (Anton Filimonov)
 - [cfd4ed0c](https://github.com/variar/klogg/commit/cfd4ed0c): make parseDataBlock code more readable (Anton Filimonov)
## Build system:
 - [b3e7d673](https://github.com/variar/klogg/commit/b3e7d673): try to disable lto to fix build error on centos (Anton Filimonov)
## Continuous integration workflow:
 - [1e46c9c2](https://github.com/variar/klogg/commit/1e46c9c2): test discord webhook (#380)[skip ci] (Anton Filimonov)
 - [f9bec82a](https://github.com/variar/klogg/commit/f9bec82a): move release flow to manual workflow (#380)[skip ci] (Anton Filimonov)
 - [88665c19](https://github.com/variar/klogg/commit/88665c19): put version into separate artifact (#380) (Anton Filimonov)
 - [33e7f616](https://github.com/variar/klogg/commit/33e7f616): extract version from artifacts (#380) [skip ci] (Anton Filimonov)
 - [c1d18dea](https://github.com/variar/klogg/commit/c1d18dea): add draft for ci release workflow (#380) [skip ci] (Anton Filimonov)
 - [1502489c](https://github.com/variar/klogg/commit/1502489c): move cmake lto def to other action (Anton Filimonov)
 - [b06efccd](https://github.com/variar/klogg/commit/b06efccd): diagnose lto error (Anton Filimonov)
 - [a45ad09e](https://github.com/variar/klogg/commit/a45ad09e): disable lto only for centos (Anton Filimonov)
 - [ccf71c21](https://github.com/variar/klogg/commit/ccf71c21): try to avoid gcc lto bug [ci release] (Anton Filimonov)
 - [63a26634](https://github.com/variar/klogg/commit/63a26634): try building in containers for linux (Anton Filimonov)
 - [003f3d37](https://github.com/variar/klogg/commit/003f3d37): simplify boost preparation (Anton Filimonov)
 - [fa2a4053](https://github.com/variar/klogg/commit/fa2a4053): remove old workflows (Anton Filimonov)
 - [7e14983c](https://github.com/variar/klogg/commit/7e14983c): actually disable tests in build action (Anton Filimonov)
 - [1b472d72](https://github.com/variar/klogg/commit/1b472d72): move test to separate action (Anton Filimonov)
# 2021-07:
## New features:
 - [ccbd0b28](https://github.com/variar/klogg/commit/ccbd0b28): allow to edit search history (#309) (Anton Filimonov)
 - [1707ef8c](https://github.com/variar/klogg/commit/1707ef8c): add file to recent files list on manual close (#131) (Anton Filimonov)
 - [d7ccfe31](https://github.com/variar/klogg/commit/d7ccfe31): adjust horizontal scroll page step (#163) (Anton Filimonov)
 - [7b5feec4](https://github.com/variar/klogg/commit/7b5feec4): add more search and follow settings (#283, #363) (Anton Filimonov)
 - [09419c54](https://github.com/variar/klogg/commit/09419c54): allow to configure search history (#364) (Anton Filimonov)
 - [cac0afef](https://github.com/variar/klogg/commit/cac0afef): deselect quick highlighters on same shortcut (#270) (Anton Filimonov)
 - [ac36a09e](https://github.com/variar/klogg/commit/ac36a09e): allow to change font size with mouse wheel (#359) (Anton Filimonov)
## Documentation:
 - [02bf08ea](https://github.com/variar/klogg/commit/02bf08ea): document commit message prefixes (Anton Filimonov)
## Build system:
 - [3987a4e5](https://github.com/variar/klogg/commit/3987a4e5): fix build on older Qt (Anton Filimonov)
## Other commits:
 - [38084e32](https://github.com/variar/klogg/commit/38084e32): [skip ci] chore: update minimal required compilers (Anton Filimonov)
# 2021-06:
## New features:
 - [e84ad461](https://github.com/variar/klogg/commit/e84ad461): allow alpha-channel for highlighters (#337) (Anton Filimonov)
 - [cd6efb9e](https://github.com/variar/klogg/commit/cd6efb9e): [ci release] feature: add experimental color labels (#270) (Anton Filimonov)
 - [aee7244d](https://github.com/variar/klogg/commit/aee7244d): add list of prominent  features in dev builds (Anton Filimonov)
 - [be2d40d9](https://github.com/variar/klogg/commit/be2d40d9): allow files with more than 2147483647 lines (#339, #341) (Anton Filimonov)
 - [4657f4ae](https://github.com/variar/klogg/commit/4657f4ae): add runtime cpu check (#343) (#344) (Anton Filimonov)
 - [4c29f49f](https://github.com/variar/klogg/commit/4c29f49f): [ci release] feature: Add go to line action in menu (#334) (Anton Filimonov)
 - [406e4ba4](https://github.com/variar/klogg/commit/406e4ba4): Allow to configure main window shortcuts (#26) (Anton Filimonov)
 - [9d5f252d](https://github.com/variar/klogg/commit/9d5f252d): improve shortcuts edit dialog (#26) (Anton Filimonov)
 - [9f89d3b9](https://github.com/variar/klogg/commit/9f89d3b9): allow to set predefined filters pattern mode (#243, #305) (Anton Filimonov)
 - [6ed90ee3](https://github.com/variar/klogg/commit/6ed90ee3): allow to change predefined filters order (#243) (Anton Filimonov)
 - [564bd1db](https://github.com/variar/klogg/commit/564bd1db): Add links to Discord and Telegram groups (Anton Filimonov)
 - [f63ad53c](https://github.com/variar/klogg/commit/f63ad53c): [ci release] Allow to configure main search highlight jitter (Anton Filimonov)
 - [b9b25ff0](https://github.com/variar/klogg/commit/b9b25ff0): [ci release] Add option to variate highlight colors (Anton Filimonov)
## Bug fixes:
 - [bb2d9faa](https://github.com/variar/klogg/commit/bb2d9faa): [ci release] fix: save settings from View menu (#349) (Anton Filimonov)
 - [9711a68a](https://github.com/variar/klogg/commit/9711a68a): [ci release] fix: initialize regex checkbox (#342) (Anton Filimonov)
 - [c5482ffd](https://github.com/variar/klogg/commit/c5482ffd): Remove unintentional variance for main match (Anton Filimonov)
 - [854e9b00](https://github.com/variar/klogg/commit/854e9b00): Update SingleApplication from upstream (#228, #329) (Anton Filimonov)
## Documentation:
 - [d73edf10](https://github.com/variar/klogg/commit/d73edf10): [skip ci] add links to chats (Anton Filimonov)
 - [6cff6e9b](https://github.com/variar/klogg/commit/6cff6e9b): [skip ci] profreading (Anton Filimonov)
 - [eae77815](https://github.com/variar/klogg/commit/eae77815): [skip ci] Add article for boolean expressions (Anton Filimonov)
 - [2246de3c](https://github.com/variar/klogg/commit/2246de3c): Add mimalloc to notice (Anton Filimonov)
## Performance:
 - [24cc11b6](https://github.com/variar/klogg/commit/24cc11b6): Use more direct access to variables in boolean evaluator (Anton Filimonov)
 - [d89b561a](https://github.com/variar/klogg/commit/d89b561a): Switch to robinhoog sets in exrptk (Anton Filimonov)
## Code refactoring:
 - [f5f24395](https://github.com/variar/klogg/commit/f5f24395): [ci release] Move boolean evaluator to its own TU (Anton Filimonov)
## Build system:
 - [30c9ee8a](https://github.com/variar/klogg/commit/30c9ee8a): fix build (Anton Filimonov)
## Other commits:
 - [afec3cbf](https://github.com/variar/klogg/commit/afec3cbf): [skip ci] chore: update latest version (Anton Filimonov)
# 2021-05:
## New feature:
 - [154b8994](https://github.com/variar/klogg/commit/154b8994): [ci release] Add user-configurable shortcuts (wip) (Anton Filimonov)
 - [4acfd7bc](https://github.com/variar/klogg/commit/4acfd7bc): Add highlight for matching patterns (Anton Filimonov)
 - [c687f271](https://github.com/variar/klogg/commit/c687f271): [ci release] Add/Replace/Exclude for combination mode (Anton Filimonov)
 - [0aae3b02](https://github.com/variar/klogg/commit/0aae3b02): [ci release] Add boolean pattern combination (Anton Filimonov)
 - [cf1b8456](https://github.com/variar/klogg/commit/cf1b8456): Allow to cancel archives extraction (Anton Filimonov)
## Bug fixes:
 - [70799af6](https://github.com/variar/klogg/commit/70799af6): [ci release] Adapt shortcuts to older Qt (Anton Filimonov)
 - [7228c9b5](https://github.com/variar/klogg/commit/7228c9b5): [ci release] Fix leading zeroes count on msvc (Anton Filimonov)
 - [2a0eb5e6](https://github.com/variar/klogg/commit/2a0eb5e6): Do not touch config too frequently (Anton Filimonov)
 - [f64d8461](https://github.com/variar/klogg/commit/f64d8461): Fix crash on reloading file during search (Anton Filimonov)
 - [b0c00418](https://github.com/variar/klogg/commit/b0c00418): [ci release] Fix lifetime issues and code style (Anton Filimonov)
 - [2e25c222](https://github.com/variar/klogg/commit/2e25c222): Try use mimalloc v2 (#323) (Anton Filimonov)
 - [b4df7404](https://github.com/variar/klogg/commit/b4df7404): [ci release] Handle more error cases when copy to clipboard (Anton Filimonov)
 - [24012d20](https://github.com/variar/klogg/commit/24012d20): Make matches overview usable for a lot of matches (Anton Filimonov)
 - [283e9fc5](https://github.com/variar/klogg/commit/283e9fc5): Use proper throttling for search progress (Anton Filimonov)
 - [b3a3c100](https://github.com/variar/klogg/commit/b3a3c100): Capture exceptions in tbb threads (Anton Filimonov)
 - [51f6eb80](https://github.com/variar/klogg/commit/51f6eb80): Fix plog and QString (Anton Filimonov)
 - [60fd9a46](https://github.com/variar/klogg/commit/60fd9a46): [skip ci] fix discord link (Anton Filimonov)
 - [402a6975](https://github.com/variar/klogg/commit/402a6975): Try use Qt command line parser (#223) (Anton Filimonov)
 - [f3dd91da](https://github.com/variar/klogg/commit/f3dd91da): [ci release] Enabled crash reporting back (Anton Filimonov)
 - [ba058366](https://github.com/variar/klogg/commit/ba058366): Allow incremental quickfind mode for regex (Anton Filimonov)
 - [f2b1b998](https://github.com/variar/klogg/commit/f2b1b998): Do not break selection on Punctuation_Connector (Anton Filimonov)
 - [b39641f6](https://github.com/variar/klogg/commit/b39641f6): [ci release] Fix crash on last line (Anton Filimonov)
 - [c09d0cc7](https://github.com/variar/klogg/commit/c09d0cc7): [ci release] disable malloc override on mac (Anton Filimonov)
 - [cbcdc4ce](https://github.com/variar/klogg/commit/cbcdc4ce): Switch back to TBB malloc (Anton Filimonov)
 - [886e0de9](https://github.com/variar/klogg/commit/886e0de9): [ci release] Make Esc reset focus to main view (#168) (Anton Filimonov)
 - [8ce767af](https://github.com/variar/klogg/commit/8ce767af): Close QuickFind by Esc (#168) (Anton Filimonov)
 - [50c27808](https://github.com/variar/klogg/commit/50c27808): Refuse to index files with too long lines (Anton Filimonov)
 - [fadb25fb](https://github.com/variar/klogg/commit/fadb25fb): Fix matching with parentheses (#303) (Anton Filimonov)
 - [385c7dce](https://github.com/variar/klogg/commit/385c7dce): [skip ci] Fix latest version (Anton Filimonov)
 - [3d725b3d](https://github.com/variar/klogg/commit/3d725b3d): Set bearer timeout to very large value (Anton Filimonov)
 - [94925d40](https://github.com/variar/klogg/commit/94925d40): Fix typo (#295) (Anton Filimonov)
 - [96ba60f5](https://github.com/variar/klogg/commit/96ba60f5): Do not allow character height to be zero (Anton Filimonov)
 - [a22c7f9a](https://github.com/variar/klogg/commit/a22c7f9a): Add back option to select preferred regex engine (Anton Filimonov)
 - [9826b628](https://github.com/variar/klogg/commit/9826b628): Fix crash when enqueing new operation in worker destructor (Anton Filimonov)
 - [0baf9546](https://github.com/variar/klogg/commit/0baf9546): Actually save default splitter to config (#174) (Anton Filimonov)
 - [7e839891](https://github.com/variar/klogg/commit/7e839891): Start new window session for file from command line (Anton Filimonov)
## Documentation:
 - [c6701beb](https://github.com/variar/klogg/commit/c6701beb): [ci release] Add documentation for logical patern combination (Anton Filimonov)
 - [a354c71c](https://github.com/variar/klogg/commit/a354c71c): update notice (Anton Filimonov)
 - [51f53772](https://github.com/variar/klogg/commit/51f53772): [skip ci] try add image to readme (Anton Filimonov)
 - [ec6028ee](https://github.com/variar/klogg/commit/ec6028ee): Add Gitter badge (#317) (The Gitter Badger)
 - [3d8deb10](https://github.com/variar/klogg/commit/3d8deb10): [skip ci] Add post about memory allocation (Anton Filimonov)
 - [fc6324a6](https://github.com/variar/klogg/commit/fc6324a6): [skip ci] add KO FI link (Anton Filimonov)
 - [59816030](https://github.com/variar/klogg/commit/59816030): [skip ci] add patreon link (Anton Filimonov)
 - [6105d627](https://github.com/variar/klogg/commit/6105d627): [skip ci] provide list of fixed glogg issues on separate page (Anton Filimonov)
 - [4337ac51](https://github.com/variar/klogg/commit/4337ac51): [skip ci] add c++17 in build requirements (Anton Filimonov)
 - [27d75262](https://github.com/variar/klogg/commit/27d75262): [skip ci] fix video comment (Anton Filimonov)
 - [fc8d5699](https://github.com/variar/klogg/commit/fc8d5699): [skip ci] add list of fixed glogg issues and perf comparison (Anton Filimonov)
 - [11233a00](https://github.com/variar/klogg/commit/11233a00): [skip ci] add visible link to github releases (Anton Filimonov)
## Performance:
 - [7e80d4ac](https://github.com/variar/klogg/commit/7e80d4ac): Use faster digits count (Anton Filimonov)
 - [6aa7ee43](https://github.com/variar/klogg/commit/6aa7ee43): [ci release] Make fast path for regex matching (Anton Filimonov)
 - [dcbc65a8](https://github.com/variar/klogg/commit/dcbc65a8): Switch exprtk to robin_hood maps (Anton Filimonov)
 - [229daec3](https://github.com/variar/klogg/commit/229daec3): Decode whole search block at once (Anton Filimonov)
 - [8de8cdad](https://github.com/variar/klogg/commit/8de8cdad): Replace tbb malloc with mimalloc (Anton Filimonov)
 - [50bae545](https://github.com/variar/klogg/commit/50bae545): Using roaring bitmaps to store marks and matches (Anton Filimonov)
## Code refactoring:
 - [6c4262c8](https://github.com/variar/klogg/commit/6c4262c8): More code cleanup (Anton Filimonov)
 - [b2e5b1cf](https://github.com/variar/klogg/commit/b2e5b1cf): Add back memory stats (Anton Filimonov)
 - [58aed568](https://github.com/variar/klogg/commit/58aed568): Code cleanup (Anton Filimonov)
 - [cd5ced3b](https://github.com/variar/klogg/commit/cd5ced3b): Remove unused operations (Anton Filimonov)
 - [f00725b5](https://github.com/variar/klogg/commit/f00725b5): Reduce nesting (Anton Filimonov)
 - [6054d5ff](https://github.com/variar/klogg/commit/6054d5ff): More QtConcurrent cleanup (Anton Filimonov)
 - [9616e63f](https://github.com/variar/klogg/commit/9616e63f): [ci release] Replace some QtConcurrent uses with TBB (Anton Filimonov)
 - [10ba5fed](https://github.com/variar/klogg/commit/10ba5fed): Add more accurate resource wrapper (Anton Filimonov)
 - [ed982634](https://github.com/variar/klogg/commit/ed982634): Remove abseil dependency (Anton Filimonov)
 - [c7b86edb](https://github.com/variar/klogg/commit/c7b86edb): Use standard mutext (Anton Filimonov)
 - [469a3b8b](https://github.com/variar/klogg/commit/469a3b8b): Code cleanup (Anton Filimonov)
 - [33760b0c](https://github.com/variar/klogg/commit/33760b0c): Code cleanup (Anton Filimonov)
 - [95472955](https://github.com/variar/klogg/commit/95472955): Fixes for C++ 17 (Anton Filimonov)
 - [d5ea5954](https://github.com/variar/klogg/commit/d5ea5954): Switch to C++ 17 (Anton Filimonov)
 - [1e8c20ac](https://github.com/variar/klogg/commit/1e8c20ac): Remove immer after switching to roaring bitmaps (Anton Filimonov)
 - [c962ba93](https://github.com/variar/klogg/commit/c962ba93): Code cleanup (Anton Filimonov)
 - [881a67ec](https://github.com/variar/klogg/commit/881a67ec): Use string_view for safety and clarity (Anton Filimonov)
## Tests:
 - [a27f0ae6](https://github.com/variar/klogg/commit/a27f0ae6): Fix tests (Anton Filimonov)
 - [6736068f](https://github.com/variar/klogg/commit/6736068f): [ci release] fix tests (Anton Filimonov)
 - [72425357](https://github.com/variar/klogg/commit/72425357): [ci release] Fix test on mac (Anton Filimonov)
 - [5cdfd91f](https://github.com/variar/klogg/commit/5cdfd91f): [ci release] Fix test on mac (Anton Filimonov)
 - [cb3710f1](https://github.com/variar/klogg/commit/cb3710f1): Enable polling on Mac for tests (Anton Filimonov)
 - [d4840a9b](https://github.com/variar/klogg/commit/d4840a9b): Try wait for ui state (Anton Filimonov)
 - [dcee04ef](https://github.com/variar/klogg/commit/dcee04ef): Revert "bypass flaky tests" (Anton Filimonov)
 - [96aec79f](https://github.com/variar/klogg/commit/96aec79f): [ci release] bypass flaky tests (Anton Filimonov)
 - [305a4944](https://github.com/variar/klogg/commit/305a4944): More flaky tests (Anton Filimonov)
 - [892bda7f](https://github.com/variar/klogg/commit/892bda7f): Try fix flaky test (Anton Filimonov)

## Build system:
 - [0480506a](https://github.com/variar/klogg/commit/0480506a): Fix build (Anton Filimonov)
 - [ffe73be3](https://github.com/variar/klogg/commit/ffe73be3): [ci release] fix build (Anton Filimonov)
 - [c8ea47dd](https://github.com/variar/klogg/commit/c8ea47dd): [WIP] fix build (Anton Filimonov)
 - [c93241b6](https://github.com/variar/klogg/commit/c93241b6): Fix x86 build (Anton Filimonov)
 - [663998f4](https://github.com/variar/klogg/commit/663998f4): Fix mac build (Anton Filimonov)
 - [c03488bf](https://github.com/variar/klogg/commit/c03488bf): Use system KArchive if available (Anton Filimonov)
 - [f8286785](https://github.com/variar/klogg/commit/f8286785): Allow to use system abseil (#300) (Anton Filimonov)
 - [d5d2107c](https://github.com/variar/klogg/commit/d5d2107c): [skip ci] logsquirl will try to use system libraries (Anton Filimonov)
 - [a1c4c8f9](https://github.com/variar/klogg/commit/a1c4c8f9): Try using system provided libraries (#300) (Anton Filimonov)
 - [b5b00ded](https://github.com/variar/klogg/commit/b5b00ded): [ci release] fix mac build (Anton Filimonov)
 - [1793ba7b](https://github.com/variar/klogg/commit/1793ba7b): Fix x86 builds (Anton Filimonov)
## Continuous integration workflow:
 - [7fddde58](https://github.com/variar/klogg/commit/7fddde58): Build portable version only on Windows (Anton Filimonov)
 - [dbe45f7d](https://github.com/variar/klogg/commit/dbe45f7d): Use same version of boost for all CI builds (Anton Filimonov)
 - [12aa6196](https://github.com/variar/klogg/commit/12aa6196): Better visibility for Centos build (Anton Filimonov)
 - [95295ae0](https://github.com/variar/klogg/commit/95295ae0): Revert "Try to reduce pre-release notification" (Anton Filimonov)
 - [1a0e9a2c](https://github.com/variar/klogg/commit/1a0e9a2c): Try to reduce pre-release notification (Anton Filimonov)
 - [a77bf323](https://github.com/variar/klogg/commit/a77bf323): Simplify CI workflows (Anton Filimonov)
 - [3ceff618](https://github.com/variar/klogg/commit/3ceff618): Add openssl to centos build container (Anton Filimonov)
 - [cee1f57f](https://github.com/variar/klogg/commit/cee1f57f): Try to build rpm in centos docker container (Anton Filimonov)
 - [93818b11](https://github.com/variar/klogg/commit/93818b11): Try use gcc-8 to stay centos-compatible (Anton Filimonov) 
 - [eba242cc](https://github.com/variar/klogg/commit/eba242cc): Use recent appimage (Anton Filimonov)
 - [52971526](https://github.com/variar/klogg/commit/52971526): Disable rpm verification on CI builds (Anton Filimonov)
 - [0d864dfd](https://github.com/variar/klogg/commit/0d864dfd): Switch to Ubuntu 18.04 (Anton Filimonov)
 - [50fcd2f9](https://github.com/variar/klogg/commit/50fcd2f9): Do not use hyperscan on 32 bit Win builds (Anton Filimonov)
## Other commits:
 - [c16f41db](https://github.com/variar/klogg/commit/c16f41db): Add console grep-like utility (Anton Filimonov)
 - [633f177d](https://github.com/variar/klogg/commit/633f177d): Update minidump_dump (Anton Filimonov)
 - [b221456f](https://github.com/variar/klogg/commit/b221456f): [ci release] Add more memory stats to crashdumps (Anton Filimonov)
# 2021-04:
## New features:
 - [d4825701](https://github.com/variar/klogg/commit/d4825701): Move from PCRE to Hyperscan (Anton Filimonov)
 - [ae9a80b3](https://github.com/variar/klogg/commit/ae9a80b3): Add intel hyperscan (Anton Filimonov)
 - [59284807](https://github.com/variar/klogg/commit/59284807): Use automatic fallback to Qt regular expressions (Anton Filimonov)
 - [74187cd3](https://github.com/variar/klogg/commit/74187cd3): Add patch for hyperscan fat runtime build (#291) (Anton Filimonov)
 - [8e26da80](https://github.com/variar/klogg/commit/8e26da80): Switch to more simple dark style and require restart (Anton Filimonov)
 - [198f7ebe](https://github.com/variar/klogg/commit/198f7ebe): Relax add|replace search (Anton Filimonov)
 - [bf1b710e](https://github.com/variar/klogg/commit/bf1b710e): Add context menu item to search with the current selection (#285) (Dan Berindei)
 - [806b2b0d](https://github.com/variar/klogg/commit/806b2b0d): Add button to treat pattern as exclude filter (#22) (Anton Filimonov)
 - [aee2fe84](https://github.com/variar/klogg/commit/aee2fe84): Prepare for excluding patterns (Anton Filimonov)
## Bug fixes:
 - [1c686a43](https://github.com/variar/klogg/commit/1c686a43): Add KDAB to NOTICE and reduce debounce timeout (Anton Filimonov)
 - [8bbd446b](https://github.com/variar/klogg/commit/8bbd446b): Try more sofisticated signal debouncer (#286) (Anton Filimonov)
 - [8da81f49](https://github.com/variar/klogg/commit/8da81f49): Add custom tab close icons for Fusion style on Windows (#288) (Anton Filimonov)
 - [fe210507](https://github.com/variar/klogg/commit/fe210507): Fix non-monospace highlight and selection (#246) (Anton Filimonov)
 - [60267121](https://github.com/variar/klogg/commit/60267121): Fix line number area rendering (#249) (Anton Filimonov)
 - [3e7a413f](https://github.com/variar/klogg/commit/3e7a413f): Don't prevent horizontal scroll in follow mode (#247) (Anton Filimonov)
 - [1f1801d5](https://github.com/variar/klogg/commit/1f1801d5): Disable FSEvents backed (Anton Filimonov)
 - [22bdbbd2](https://github.com/variar/klogg/commit/22bdbbd2): Use common dispatch to threads (Anton Filimonov)
 - [97886ac7](https://github.com/variar/klogg/commit/97886ac7): Get rid of cmake warning (Anton Filimonov)
 - [0c77863f](https://github.com/variar/klogg/commit/0c77863f): Fix 0 for goto line (#244) (Anton Filimonov)
 - [85ac7605](https://github.com/variar/klogg/commit/85ac7605): Update selection on right click (#281) (Anton Filimonov)
 - [80f332da](https://github.com/variar/klogg/commit/80f332da): Add some bad_alloc catching (#235) (Anton Filimonov)
 - [18747786](https://github.com/variar/klogg/commit/18747786): Fix file monitor notifications spam (#286) (Anton Filimonov)
 - [b340f25e](https://github.com/variar/klogg/commit/b340f25e): Fix passing list of files to primary instance (Anton Filimonov)
 - [892494a4](https://github.com/variar/klogg/commit/892494a4): Fix command line help (Anton Filimonov)
 - [48b32815](https://github.com/variar/klogg/commit/48b32815): Made regex error text readable (#264) (Anton Filimonov)
 - [e627e1e2](https://github.com/variar/klogg/commit/e627e1e2): Fix icon reloading for dark theme (#264) (Anton Filimonov)
 - [10ce9b10](https://github.com/variar/klogg/commit/10ce9b10): Track context menu positon (#242) (Anton Filimonov)
 - [8ff6d7d8](https://github.com/variar/klogg/commit/8ff6d7d8): Fix deadlock on indexing (Anton Filimonov)
 - [30015015](https://github.com/variar/klogg/commit/30015015): Fix some dataraces from tsan (Anton Filimonov)
 - [9b0c3611](https://github.com/variar/klogg/commit/9b0c3611): Allow to set selection start and end (#242) (Anton Filimonov)
 - [6567a138](https://github.com/variar/klogg/commit/6567a138): Calculate head/tail hash after all indexing is done (Anton Filimonov)
 - [05241283](https://github.com/variar/klogg/commit/05241283): Create dump dir if not exist (Anton Filimonov)
 - [63474a05](https://github.com/variar/klogg/commit/63474a05): Replace null chars with spaces for clipboard (#227) (Anton Filimonov)
 - [c64da293](https://github.com/variar/klogg/commit/c64da293): Fix single line copy to clipboard (Anton Filimonov)
 - [a7fa2ada](https://github.com/variar/klogg/commit/a7fa2ada): Add retry to get data from clipboard (Anton Filimonov)
 - [4934d720](https://github.com/variar/klogg/commit/4934d720): Allow some time to upload dumps (Anton Filimonov)
 - [d58d5255](https://github.com/variar/klogg/commit/d58d5255): Try to recover from bad_alloc (Anton Filimonov)
 - [82cb0104](https://github.com/variar/klogg/commit/82cb0104): Enforce limit on line length (Anton Filimonov)
 - [b8865b6c](https://github.com/variar/klogg/commit/b8865b6c): Strict check for raw buffer length (#268) (Anton Filimonov)
 - [be27b07c](https://github.com/variar/klogg/commit/be27b07c): Check for bytes read from file (Anton Filimonov)
 - [4ff11665](https://github.com/variar/klogg/commit/4ff11665): Check for not full read from file (Anton Filimonov)
 - [947ebb0a](https://github.com/variar/klogg/commit/947ebb0a): Check for too long lines (Anton Filimonov)
 - [2d1d5c47](https://github.com/variar/klogg/commit/2d1d5c47): Fix race when file changes during being indexed (Anton Filimonov)
 - [88f06e7a](https://github.com/variar/klogg/commit/88f06e7a): Try fix race during encoding change (Anton Filimonov)
 - [a6b0481b](https://github.com/variar/klogg/commit/a6b0481b): Use QPlainTextEdit for crash reports (#245) (Anton Filimonov)
## Documentation:
 - [d5b372d3](https://github.com/variar/klogg/commit/d5b372d3): [skip ci] Mention options to disable Hyperscan (Anton Filimonov)
 - [d7da0341](https://github.com/variar/klogg/commit/d7da0341): [skip ci] Add link to repositories in linux install section (Anton Filimonov)
 - [76882034](https://github.com/variar/klogg/commit/76882034): [skip ci] Add current milestone badges (Anton Filimonov)
 - [230f1adf](https://github.com/variar/klogg/commit/230f1adf): Add badge from repology (Anton Filimonov)
 - [554d7215](https://github.com/variar/klogg/commit/554d7215): add more badges (Anton Filimonov)
 - [49a0f1d3](https://github.com/variar/klogg/commit/49a0f1d3): Add hyperscan to Notice file (Anton Filimonov)
 - [8c0a49c0](https://github.com/variar/klogg/commit/8c0a49c0): [skip ci] Update build instructions (Anton Filimonov)
## Performance:
 - [7480b18e](https://github.com/variar/klogg/commit/7480b18e): Move conversion to ucs to worker threads (Anton Filimonov)
 - [3b8a581e](https://github.com/variar/klogg/commit/3b8a581e): Reduce allocation on indexing (Anton Filimonov)
## Code refactoring:
 - [e921e726](https://github.com/variar/klogg/commit/e921e726): Use structure for raw lines (Anton Filimonov)
 - [e573bdc0](https://github.com/variar/klogg/commit/e573bdc0): Simplify valid regex check (Anton Filimonov)
 - [3b8454ec](https://github.com/variar/klogg/commit/3b8454ec): User smart pointers for Hyperscan RAII (Anton Filimonov)
 - [cb2838cb](https://github.com/variar/klogg/commit/cb2838cb): Allow to select regexp engine in runtime and compile time (#280) (Anton Filimonov)
 - [28161da2](https://github.com/variar/klogg/commit/28161da2): Try simplify indexing operations enPqueing (Anton Filimonov)
 - [dd87888b](https://github.com/variar/klogg/commit/dd87888b): Remove some includes (Anton Filimonov)
 - [bd32d8e9](https://github.com/variar/klogg/commit/bd32d8e9): Simplify logging macros (Anton Filimonov)
## Build system:
 - [088b9598](https://github.com/variar/klogg/commit/088b9598): Fix build without hyperscan (Anton Filimonov)
 - [678481b7](https://github.com/variar/klogg/commit/678481b7): Make cmake better (Anton Filimonov)
 - [12a93e22](https://github.com/variar/klogg/commit/12a93e22): Fix some install paths (Anton Filimonov)
 - [7440d6ad](https://github.com/variar/klogg/commit/7440d6ad): Remove some additional hyperscan targets (Anton Filimonov)
 - [32238c8d](https://github.com/variar/klogg/commit/32238c8d): Update mac target to 10.13 (Anton Filimonov)
 - [282eaf9c](https://github.com/variar/klogg/commit/282eaf9c): Try fix build (Anton Filimonov)
 - [4df167ae](https://github.com/variar/klogg/commit/4df167ae): Try use fat runtime for hyperscan (Anton Filimonov)
 - [324a3c73](https://github.com/variar/klogg/commit/324a3c73): Remove unneeded installed libs (Anton Filimonov)
 - [4bb9f388](https://github.com/variar/klogg/commit/4bb9f388): Switch to oneAPI TBB version (Anton Filimonov)
 - [25d0cb45](https://github.com/variar/klogg/commit/25d0cb45): Do not install abseil files (Anton Filimonov)
 - [f4a24b8e](https://github.com/variar/klogg/commit/f4a24b8e): Try fix build (Anton Filimonov)
 - [8e6077ea](https://github.com/variar/klogg/commit/8e6077ea): [skip ci] add ebuild for Gentoo (Anton Filimonov)
 - [22818236](https://github.com/variar/klogg/commit/22818236): Do not install efsw files (Anton Filimonov)
## Continuous integration workflow:
 - [bd87f20c](https://github.com/variar/klogg/commit/bd87f20c): Try fix win ci (Anton Filimonov)
 - [a03bfd26](https://github.com/variar/klogg/commit/a03bfd26): Switch to notarize action with longer timeout (Anton Filimonov)
 - [ca106b7b](https://github.com/variar/klogg/commit/ca106b7b): Restructure workflow (Anton Filimonov)
 - [e9d3c1d8](https://github.com/variar/klogg/commit/e9d3c1d8): Add hyperscan deps to workflow (Anton Filimonov)
 - [565a44f3](https://github.com/variar/klogg/commit/565a44f3): [skip ci] fix codeql build (Anton Filimonov)
 - [9aa74db7](https://github.com/variar/klogg/commit/9aa74db7): Build releases for more generic cpu (#290) (Anton Filimonov)
 - [72afe6a7](https://github.com/variar/klogg/commit/72afe6a7): [skip ci] Fix codeql build (Anton Filimonov)
 - [0525c6e7](https://github.com/variar/klogg/commit/0525c6e7): Revert "Move build number to patch version position after bintray incident" (Anton Filimonov)
 - [801d4471](https://github.com/variar/klogg/commit/801d4471): Move build number to patch version position after bintray incident (Anton Filimonov)
 - [dbbb4c88](https://github.com/variar/klogg/commit/dbbb4c88): Simplify codeql build (Anton Filimonov)
## Other commits:
 - [86666e73](https://github.com/variar/klogg/commit/86666e73): update site (Anton Filimonov)
 - [336529f4](https://github.com/variar/klogg/commit/336529f4): [skip ci] News about Hyperscan (Anton Filimonov)
 - [d006bca4](https://github.com/variar/klogg/commit/d006bca4): Update qdarkstyle to 3.0.2 (Anton Filimonov)
 - [9f1f8762](https://github.com/variar/klogg/commit/9f1f8762): Collect current vm use in dumps (Anton Filimonov)
# 2021-03:
## New features:
 - [1c8ad2b4](https://github.com/variar/klogg/commit/1c8ad2b4): Add context menu to save current search as filter (#253) (Marcin Twardak)
## Bug fixes:
 - [7ad885fd](https://github.com/variar/klogg/commit/7ad885fd): Some fixes for predefined filters (Anton Filimonov)
# 2021-02:
## New features:
 - [1a13b4be](https://github.com/variar/klogg/commit/1a13b4be): Add import/export feature to predefined filters (#248) (Marcin Twardak)
  - [e1af31ef](https://github.com/variar/klogg/commit/e1af31ef): Do not codesign on pull request checks (Anton Filimonov)
## Bug fixes:
 - [6b5a6e69](https://github.com/variar/klogg/commit/6b5a6e69): Sync data before reading and check window ptr (#255) (Anton Filimonov)
 - [b4c8548f](https://github.com/variar/klogg/commit/b4c8548f): Add safety check for qChecksum (#228) (Anton Filimonov)
## Continuous integration workflow:
 - [5bd5a7ed](https://github.com/variar/klogg/commit/5bd5a7ed): Update codesign action (Anton Filimonov)
 - [18a66e9e](https://github.com/variar/klogg/commit/18a66e9e): Remove more codsign from PR validation builds (Anton Filimonov)

 - [0974b578](https://github.com/variar/klogg/commit/0974b578): Make predefined filters dialog more similar to highlighters (Anton Filimonov)
# 2021-01:
## New features:
 - [be9bb39b](https://github.com/variar/klogg/commit/be9bb39b): Refactor predefined filters (#191) (Anton Filimonov)
 - [869a09a5](https://github.com/variar/klogg/commit/869a09a5): Add drop-down menu with predefined filters (#241) (Marcin Twardak)
## Build:
 - [d6a78a41](https://github.com/variar/klogg/commit/d6a78a41): Add std::hash<QString> for older Qt versions (Anton Filimonov)
## Documentation:
 - [2b27b1f9](https://github.com/variar/klogg/commit/2b27b1f9): [skip ci] Add a screenshot to readme (Anton Filimonov)
 - [83a94380](https://github.com/variar/klogg/commit/83a94380): [skip ci] Add some screenshots (Anton Filimonov)
 - [4ef7f145](https://github.com/variar/klogg/commit/4ef7f145): Update DOCUMENTATION.md (lilventi)
## Continuous integration workflow:
 - [04333561](https://github.com/variar/klogg/commit/04333561): Another codesign fix (Anton Filimonov)
 - [6f880565](https://github.com/variar/klogg/commit/6f880565): Fix codesign (Anton Filimonov)
 - [00f4d90a](https://github.com/variar/klogg/commit/00f4d90a): Fix codeql workflow (Anton Filimonov)

# 2020-12:
## Other commits:
 - [e6a2c422](https://github.com/variar/klogg/commit/e6a2c422): Add build arch to crash report (Anton Filimonov)
