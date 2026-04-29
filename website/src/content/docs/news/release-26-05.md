---
title: Version 26.05.0-beta1
description: Title bar theming, welcome dashboard, zstd/lz4 decompression, index cache, command palette, translations, 33 bug fixes.
---

## Version 26.05.0-beta1 (April 2026)

The second feature release of LogSquirl delivers a polished first-run experience, transparent compressed-log support, an index cache for instant re-opens, a VS Code-style command palette, full UI translations for six languages, a complete theme overhaul, and 33 bug fixes found in a thorough code audit.

### New features

- **Native title bar theming**: The OS window chrome (macOS traffic lights, Windows title bar) now matches the active theme via `QStyleHints::setColorScheme()` (Qt 6.8+).
- **Startup splash screen**: Shows the LogSquirl icon and version while the app initialises. Off by default; enable in Options → General.
- **Welcome dashboard**: A permanent "Home" tab (pinned at position 0) with recent files, favorites, quick actions, loaded plugin status, and keyboard shortcut hints. Files can be dropped onto the dashboard.
- **Zstd/LZ4 decompression**: Transparently open `.zst` and `.lz4` compressed log files. The file is streamed through a decompressor to a temporary file before indexing.
- **Index cache**: File indexes are persisted to disk so re-opening a previously indexed file skips the full indexing pass. Configure the maximum cache size (MB) in Options → Performance. Disabled by default.
- **Command palette**: VS Code-style quick-command dialog (Ctrl+Shift+P / Cmd+Shift+P). Fuzzy-search all menu actions, recent files, favorites, highlighter sets, and plugin commands.
- **Translations**: Full UI translations for German, Ukrainian, Spanish, French, Brazilian Portuguese, and European Portuguese. Selectable in Options → View → Language.

### UI/UX improvements

- **Complete theme overhaul**: All three themes (Light, Dark, High Contrast) have been fully rewritten with consistent color palettes.
- **External QSS theme system**: Theme stylesheets are now loaded from external `.qss` files. Users can override any theme by placing a `.qss` file in `<AppConfigDir>/themes/`.
- **High Contrast theme**: New style option for users who need maximum contrast — bold borders, high-visibility focus indicators, and a black-on-white palette.
- **Toggle button visibility**: Checkable toolbar buttons clearly show their active state across all themes.
- **Dark theme improvements**: Softened checkbox contrast, improved disabled text contrast (WCAG AA), red hover on tab close buttons.
- **Toolbar icon size**: Increased default from 16×16 to 24×24 for modern displays. Configurable via settings.

### Accessibility

- Keyboard focus on all search filter buttons via Tab.
- Explicit tab order for the search bar and filter buttons.
- `setAccessibleName()` on main window, tab widget, search input, and filter buttons.
- `:focus` styles in all themes for keyboard navigation visibility.

### Bug fixes (33 total)

This release includes a comprehensive 4-round code audit that identified and fixed 33 bugs:

- **Memory safety**: Fixed memory leaks in indexing/search interrupt paths, potential double-free in `AbstractLogView` destructor, dangling `QAction*` in Command Palette.
- **Crash fixes**: Null-check guards in `MainWindow::closeTab()`, `updateInfoLine()`, `openFileByName()`, `refreshOverview()`. Fixed SIGSEGV in `LogFilteredData` sentinel handling.
- **Decompression hardening**: Fixed context leaks in Lz4Device/ZstdDevice, infinite loop on stalled device, silent truncation on short writes, missing `finished_` check.
- **Index cache safety**: Fixed crash on empty/truncated files, empty cache files created on every reindex, eviction accounting errors, `mkpath()` failure ignored.
- **Undefined behavior**: Clamped `lineNumberToVerticalScroll` overflow (caught by UBSan), guarded `Session::close()` against unknown views, fixed `endOfLines.back()` on empty result.
- **Error handling**: Surfaced `hs_scan` return codes, empty `catch(...)` diagnostics, chart export write failures, `indexCacheMaxSizeMb` overflow.
- **Input validation**: Fixed trailing quote in logical-combining filter, out-of-bounds in recent file actions, invalid highlighter regex causing silent failures.
- **Security**: HTML-escaped plugin metadata in WelcomeDashboard to prevent markup injection.

### Build / internal

- Fixed broken header guard in `dispatch_to.h`.
- Renamed misspelled struct `WatchedDirecotry` → `WatchedDirectory`.
- Fixed `-Wshadow` error in `ChartPanel::exportPreset()` (GCC only).

### Tests

- Added `test_audit_regressions.py` with 11 E2E regression tests covering the audit fixes.
- Refreshed performance baselines.

### System requirements

- **Windows**: Windows 10 1809 or later (x64)
- **macOS**: macOS 12 Monterey or later (Apple Silicon and Intel)
- **Linux**: Ubuntu 24.04+, Fedora 43+, or equivalent (x64)
- **Qt**: 6.8 or later
