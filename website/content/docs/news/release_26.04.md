---
title: "Version 26.04 (Beta 2)"
date: 2026-04-10T00:00:00+00:00
anchor: "v26_04"
weight: 25
---

## Version 26.04.1-beta2 (April 2026)

The second beta release of LogSquirl brings major enhancements to the Chart Panel with custom X-axis extraction, timestamp parsing, time-based bucketing, presets, and filter frequency visualization.

### New features
 - **Chart X-Axis Extraction**: chart series can now use a custom regex to extract X-axis values from log lines (timestamp or numeric). Configure via the "X-Axis" group in the series dialog.
 - **Timestamp X-Axis**: parse timestamps with configurable QDateTime format (e.g. `MM-dd HH:mm:ss.zzz`). Auto-defaults to current year for formats without a year component.
 - **Time Aggregation / Bucketing**: group data points into configurable time buckets (100 ms – 5 min) and sum Y values per bucket — ideal for spotting activity peaks.
 - **Per-Document Chart Persistence**: chart series and panel visibility are automatically saved and restored when reopening a file.
 - **App-Level Chart Presets**: save named chart configurations via Save / Load / Delete Preset toolbar actions. Presets persist across sessions.
 - **Chart Export / Import**: export current series to JSON and import from JSON files — share chart configurations between machines or users.
 - **Filter Frequency Chart**: View → "Show Filter Frequency" creates count-mode chart series (one per search sub-pattern) from the active search text.
 - **JWT Decoder improvements**: `extractToken()` now uses regex-based extraction. Fixed multi-line input handling and added support for tokens embedded in log lines.

### Bug fixes
 - Fixed ambiguous `Roaring64Map::contains()` / `add()` overload on GCC/Linux where `unsigned long long` differs from `uint64_t`.

---

## Version 26.04.1-beta1 (April 2026)

The first beta1 brings Tab Groups, Log Merge, Breadcrumbs, the Chart Panel, and the transition from Hyperscan to Vectorscan.

### New features
 - **Tab Group Manager**: organize tabs into named, colored groups. Dedicated Tools → "Manage Tab Groups…" dialog for rename, recolor, and delete operations. Groups persist across sessions.
 - **Log Merge**: right-click a tab → "Merge All Left/Right" to concatenate logs into a virtual merged tab with optional deduplication and live-updates (300 ms debounce).
 - **Breadcrumbs (Context Lines)**: configurable ±N context lines around matches/marks in the filtered view. Overlapping contexts merge automatically.
 - **Chart Panel**: interactive chart pane below the filtered view. Define regex-based series with numeric capture groups to extract and plot values. Features: line/scatter chart with zoom, pan, click-to-navigate, hover tooltips, multiple series with independent colors.
 - **Vectorscan**: replaced Hyperscan with the maintained Vectorscan fork. Native ARM/NEON support. FAT_RUNTIME auto-selects fastest SIMD path (SSE2 → AVX512).
 - **Plugin System**: C ABI plugin system for DataSource, Converter, and UI plugins. Plugin repository with one-click install, Lua scripting layer, auto-load support.
 - **Tab Close Confirmation**: optional dialog with "Don't ask again" option.

### Performance
 - AVX2/AVX512 code generation in Vectorscan for higher throughput
 - Windows MSVC `/arch:AVX2` for local builds

### Build system
 - Upgraded C++ standard from C++17 to C++23
 - Dropped Qt5 — Qt6 only
 - GCC 13, Clang 17, MSVC 19.36 minimum

### System requirements
 - **CPU**: SSE2 minimum, SSSE3/POPCNT recommended, AVX2/AVX512 supported
 - **OS**: Windows 10+, macOS 15+, Ubuntu 22.04+, Fedora 43+, Oracle Linux 10+
