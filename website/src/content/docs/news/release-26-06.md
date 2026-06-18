---
title: Version 26.06.0
description: Auto log format detection, chart templates, table view, Filters Panel improvements, 15 CodeQL security fixes, and viewport rendering fixes.
---

## Version 26.06.0 (June 2026)

This is the first stable release of the v26.05/06 cycle, graduating the beta feature set to stable and shipping three additional months of improvements: auto log format detection with a structured table view, format-aware chart templates, Filters Panel usability improvements, 15 security fixes, and a comprehensive round of viewport and table view bug fixes.

### New features

- **Auto Log Format Detection**: Automatically detect lnav-compatible log formats and display logs in a structured table view with columns for timestamp, level, and custom fields. Supports 24 built-in format definitions. Toggle between text and table view with a toolbar button. User-defined format files can be placed in the platform data directory (`~/.local/share/logsquirl/formats/` on Linux, `~/Library/Application Support/LogSquirl/formats/` on macOS, `%APPDATA%\LogSquirl\formats\` on Windows). Enable via Options → Log Formats → "Auto-detect log format (table view)".
- **Chart Panel — Format-Aware Templates**: When a log format is auto-detected, the Chart Panel shows a **Templates** button with pre-configured chart series (log level distribution, message rate, numeric fields, field occurrence). X-axis is automatically configured from the format's timestamp regex. Supports all built-in and user-defined formats.
- **Native title bar theming**: The OS window chrome (macOS traffic lights, Windows title bar) matches the active theme via `QStyleHints::setColorScheme()` (Qt 6.8+).
- **Startup splash screen**: Shows the LogSquirl icon and version while the app initialises. Off by default; enable in Options → General.
- **Welcome dashboard**: A permanent "Dashboard" tab (pinned at position 0) with recent files, favorites, quick actions, loaded plugin status, and keyboard shortcut hints. Files can be dropped onto the dashboard.
- **Zstd/LZ4 decompression**: Transparently open `.zst` and `.lz4` compressed log files.
- **Index cache**: File indexes are persisted to disk so re-opening a previously indexed file skips the full indexing pass. Configure in Options → Performance. Disabled by default.
- **Command palette**: VS Code-style quick-command dialog (Ctrl+Shift+P / Cmd+Shift+P). Fuzzy-search all menu actions, recent files, favorites, highlighter sets, and plugin commands.
- **Translations**: Full UI translations for German, Ukrainian, Spanish, French, Brazilian Portuguese, and European Portuguese. Selectable in Options → View → Language.

### UI/UX improvements

- **Filters Panel — Double-click Solo**: Double-clicking a filter item now activates only that filter (or all filters in a group when double-clicking a group header), deactivating everything else.
- **Filters Panel — Debounced persistence**: Pinned-filter state is now written to disk via a 500 ms debounce timer, reducing I/O pressure when toggling multiple filters rapidly.
- **Filters Panel — O(1) filter lookup**: Replaced the O(n²) nested-loop filter resolution with a pre-built `QHash` index for large filter sets.
- **Complete theme overhaul**: All three themes (Light, Dark, High Contrast) rewritten with consistent color palettes. External QSS theme system — users can override themes by placing a `.qss` file in `<AppConfigDir>/themes/`.
- **High Contrast theme**: New style for users who need maximum contrast — bold borders, high-visibility focus indicators, black-on-white palette.
- **Toolbar icon size**: Increased default from 16×16 to 24×24 for modern displays. Configurable via settings.
- **Toggle button visibility**: Checkable toolbar buttons clearly show their active state across all themes.
- **Dark theme improvements**: Softened checkbox contrast, improved disabled text contrast (WCAG AA), red hover on tab close buttons.

### Accessibility

- Keyboard focus on all search filter buttons via Tab.
- Explicit tab order for the search bar and filter buttons.
- `setAccessibleName()` on main window, tab widget, search input, and filter buttons.
- `:focus` styles in all themes for keyboard navigation visibility.

### Table view (new in this release)

- Columns auto-sized by sampling up to 2000 rows for full content visibility.
- Column order matches the regex capture group order in the format definition.
- Full-fidelity highlighting: search matches, marks, highlighter sets, quickfind results, and color labels rendered via a custom `LogTableHighlightDelegate`.
- Pixel-level horizontal scrolling with a smooth 10 px single step.
- Last column stretches to fill the viewport when content is narrower than the view.
- Virtual/lazy model — fields are extracted on demand with an LRU cache (2000 rows) for fast performance on large files.

### Security

- **Resolved 15 CodeQL security alerts**: Suspicious pointer arithmetic (`sizeof` add) in `simdutf` (westmere/haswell SIMD paths), narrow-type loop comparison in `bzip2`, and multiplication-result truncation in `zlib`/`bzlib`. All alerts were in vendored third-party code inside `build/_deps/` and are suppressed via CodeQL path exclusion (`.github/codeql-config.yml`).

### Bug fixes

- **Viewport text clipping**: `getNbVisibleCols()` double-subtracted the vertical scrollbar width, clipping text on the right. Fixed.
- **Sidebar opens on startup**: The sidebar dock was shown whenever a plugin registered a tab. Now stays closed by default.
- **Table view extremely slow on large files**: The model was loading all lines upfront. Now virtual/lazy with LRU cache.
- **Table view column order non-deterministic**: `QHash` iteration order in Qt 6 caused columns to shuffle between restarts. Column order now follows regex capture group order.
- **Table view hidden fields shown**: Fields marked `hidden: true` in format definitions are no longer displayed as columns.
- **Memory leaks on indexing/search interrupt**: Per-block buffers released correctly when operations are interrupted in TBB flow graph.
- **Potential double-free in `AbstractLogView` destructor**: Restructured to guarantee exactly one delete.
- **Trailing quote in logical-combining filter**: Wrapping quotes are now stripped symmetrically.
- **`endOfLines.back()` undefined behavior on empty result**: Guarded with early return.
- **`refreshOverview` crash in release builds**: `assert` replaced with an explicit null guard.
- **`hs_scan` return code discarded**: Both Hyperscan matchers now log non-success codes.
- **Index cache crash on empty files**: Guarded `serialize()` against zero-size/null buffer.
- **Empty index cache files on every reindex**: `trySave` now skips zero-line indexes.
- **Decompressor silent output truncation on short writes**: Write failures now propagate correctly.
- **HTML injection in WelcomeDashboard**: Plugin metadata is now HTML-escaped.
- Multiple additional crash guards, null-check additions, and UBSan fixes.

### Build / CI

- Fixed broken header guard in `dispatch_to.h`.
- Renamed misspelled struct `WatchedDirecotry` → `WatchedDirectory`.
- Fixed `-Wshadow` error in `ChartPanel::exportPreset()` (GCC only).
- Added `test_audit_regressions.py` with 11 E2E regression tests.
- Fixed flaky Windows CI SEGFAULT in `logfiltereddata_test` (TBB flow-graph startup latency on slow runners). `waitUiState()` timeout raised from 20 s to 120 s.

### System requirements

- **Windows**: Windows 10 1809 or later (x64)
- **macOS**: macOS 12 Monterey or later (Apple Silicon and Intel)
- **Linux**: Ubuntu 24.04+, Fedora 43+, or equivalent (x64)
- **Qt**: 6.8 or later
