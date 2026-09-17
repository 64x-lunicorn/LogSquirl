---
title: Version 26.10.0-beta1
description: Crash and Search stall fixes, scrolling through wrapped lines, a Table View on par with the Text View, System theme and live theme switching, Qt 6.11.2, signed checksums and SBOM.
---

## Version 26.10.0-beta1 (September 2026)

The first beta of 26.10 is mostly about reliability. It fixes a memory corruption that could crash LogSquirl during a Search, Searches that stopped making progress and saves that dropped lines. It also reworks scrolling through wrapped lines and brings the Table View up to the Text View. Themes switch without a restart and can follow the operating system. The release ships on Qt 6.11.2, and every download now comes with a signed checksum file, build provenance and an SBOM.

This is a beta: please [report problems](https://github.com/64x-lunicorn/LogSquirl/issues) you run into.

### Highlights

- **System theme and live theme switching**: The new "System" style follows the operating system's light or dark mode, also when it changes while LogSquirl runs. Choosing a theme applies it at once to every open window.
- **Readable in every theme**: Line numbers, bullets, radio buttons, sliders, check boxes, the Command Palette and many smaller widgets take their colors from the theme. High Contrast gets clearer checked, hovered and disabled states.
- **Scrolling through wrapped lines**: With text wrapping on, the wheel, arrow keys and Page Up/Down move by screen lines, so a line taller than the window can be scrolled through to its end. The view stays at the bottom and keeps your reading position when you resize or change the font.
- **Jumps move the view only when needed**: Going to a line, Match, Mark or QuickFind result leaves the view alone when the target is already visible, and otherwise puts it on the top row.
- **Table View on par with the Text View**: Rows show Marks, Matches, Highlighters and Search Limits like the Text View, both views share one context menu, and column widths are kept.
- **Faster re-indexing of growing files**: With the index cache on, reopening a grown log file indexes only what was appended.
- **Faster Searches**: A Search with few Matches finishes about a third faster.
- **Recording shortcuts**: Click a shortcut cell or press Enter to record; Escape cancels, Backspace clears.

### Bug fixes

- **Crash with native file watching**: Closing and opening log files in the same directory could corrupt memory and crash LogSquirl, often during a Search.
- **Crashes in QuickFind and logging**: QuickFind in the Filtered View and log messages from several threads could crash LogSquirl.
- **Search stalls**: A Search could stop making progress, mostly on machines with few cores. It now always runs to completion, and a failed Search says so instead of running forever.
- **Saving drops lines**: Saving a line count that is a multiple of 5,000 left out the last 5,000 lines. Cancel now works, and a cancelled save leaves the file untouched.
- **Settings reach every tab**: Search and QuickFind colors, Highlighter Sets, zoom, fonts, shortcuts, Context Lines and View menu toggles now apply to every open log file, not only the current tab. Search highlight colors are kept across restarts.
- **Marks and Search state**: Next/previous Mark in the Filtered View go the right way, cleared Marks disappear there, reloading no longer brings back saved Marks, and a running Search no longer leaks its state onto another tab.
- **logsquirl_grep**: Reports invalid patterns and failures with a non-zero exit code instead of hanging.

### Security

- **Qt 6.11.2**: Fixes CVE-2026-9499, CVE-2026-19248, CVE-2026-76151 and CVE-2026-6210. CVE-2026-15037 (CVSS 2.9) is fixed only in Qt 6.12.0 and is accepted until then.
- **OpenSSL 3.5.8 LTS on Windows**: Pinned and checksum-verified.
- **Verifiable downloads**: Every asset is listed in a checksum file signed with Sigstore, carries a GitHub build provenance attestation and is described by a CycloneDX SBOM. The release notes show how to verify them.
- **Scanned before release**: A release is blocked by critical known vulnerabilities, and it ships exactly the packages that passed CI.

### Removed

- **Lua plugin support**: The optional Lua scripting layer, off by default, is gone. Native plugins work as before.

The full list of changes is in the [changelog](https://github.com/64x-lunicorn/LogSquirl/blob/master/CHANGELOG.md).

**Download**: [GitHub Release v26.10.0-beta1](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0-beta1)
