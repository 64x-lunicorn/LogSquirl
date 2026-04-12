---
title: "About"
type: docs
---

## Faster log explorer

_LogSquirl_ is an open source, cross-platform log viewer built with C++23 and Qt6. It started as a fork of [klogg](https://github.com/variar/klogg) by [Anton Filimonov](https://github.com/variar), which itself was a fork of the [glogg project](https://glogg.bonnefon.org/) by [Nicolas Bonnefon](https://github.com/nickbnf). Since the original klogg is no longer actively maintained, _LogSquirl_ continues development under a new name with many new features and improvements.

_LogSquirl_ is designed to:

 - be very fast — optimized for modern multi-core CPUs with SIMD (SSE2 → AVX512)
 - handle huge log files (10 GB+) without loading them into memory
 - provide a clear view of the matches even in heavily cluttered files
 - be extensible via a C ABI plugin system

Here is what _LogSquirl_ looks like:

<div id="gallery" style="display:none;">
<img src="/screenshots/mainwindow.png" alt="LogSquirl main window" />
<img src="/screenshots/dark.png" alt="LogSquirl dark theme" />
<img src="/screenshots/scratchpad.png" alt="LogSquirl scratchpad" />
<img src="/screenshots/highlighters.png" alt="LogSquirl highlighters configuration" />
</div>

## Features
{{< columns >}}
**Core — inherited from glogg and klogg**

 - Runs on Windows, macOS, and Linux thanks to Qt6
 - Displays search results separately from the original file
 - Supports Perl-compatible regular expressions (via Vectorscan SIMD engine)
 - Colorizes log lines and search results with multi-set highlighters
 - Displays a context view of where in the log the lines of interest are
 - Reads the file directly from disk, without loading it into memory
 - Watches for file changes and reloads in real-time (Follow Mode)
 - Configurable keyboard shortcuts

<--->

**New in LogSquirl**

 - **Chart Panel** — plot numeric values from log lines with regex capture groups, custom X-axis timestamps, time-based bucketing/aggregation, presets, and filter frequency visualization
 - **Plugin System** — extend with DataSource, Converter, and UI plugins via a pure C ABI (or Lua scripts)
 - **Log Merge** — combine multiple log files into a unified, live-updated view with optional deduplication
 - **Tab Groups** — organize tabs with named, colored groups; rename tabs; session persistence
 - **Context Lines (Breadcrumbs)** — show configurable surrounding lines around matches in the filtered view
 - **Predefined Filters Panel** — pinned filters that persist across sessions, with Chipmunk import support
 - **Scratchpad** — Base64, Hex, URL, JSON/XML formatting, CRC32, timestamps, and JWT decoder
 - **Boolean search** — combine regex with AND, OR, NOT operators
 - Portable version, archive extraction, favorites, and much more

{{< /columns >}}

## Downloads

Latest beta version:

[ ![GitHub Release](https://img.shields.io/github/v/release/64x-lunicorn/LogSquirl?label=GitHub%20Release&style=for-the-badge)](https://github.com/64x-lunicorn/LogSquirl/releases/latest)


Latest development builds can be downloaded from GitHub:

{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-win" >}}Windows{{< /button >}}
{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-linux" >}}Linux{{< /button >}}
{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-osx" >}}Mac{{< /button >}}

## Documentation

Full documentation is available on the [LogSquirl Wiki](https://github.com/64x-lunicorn/LogSquirl/wiki), covering all features, configuration options, and a plugin development guide.

## System Requirements

 - **CPU**: SSE2 minimum. SSSE3/POPCNT recommended. AVX2/AVX512 auto-detected for best performance.
 - **OS**: Windows 10+, macOS 15+, Ubuntu 22.04+, Fedora 43+, Oracle Linux 10+
 - **License**: GPL-3.0-or-later

