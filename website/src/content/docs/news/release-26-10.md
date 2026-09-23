---
title: Version 26.10.0-beta2
description: Two Smyck themes and a flatter look, live theme switching, a reworked Dashboard, faster Search, indexing and startup, 64 bug fixes.
release:
  version: 26.10.0-beta2
  date: 2026-09-23
  channel: beta
---

## Version 26.10.0-beta2 (September 2026)

The largest release so far: the whole user interface was rebuilt on theme tokens, so themes now switch
without a restart and reach every widget; Search, indexing, reading and startup were measured and made
faster; the release itself is now built, scanned and signed by a hardened pipeline. 64 bug fixes.

This is a beta. Install it next to a stable version only if you want to try these changes early.

### Themes and look

- **Smyck and Smyck Light**: Two new themes after the [SMYCK terminal color scheme](https://color.smyck.org/),
  dark and light. Choosing one also colors the nine color labels in SMYCK's colors; a color you picked
  yourself is left alone.
- **Theme changes without a restart**: A Theme applies to every open window and Log File right away.
- **System theme**: LogSquirl follows the operating system's light or dark setting.
- **Flatter look**: Panes, header cells, tool bars and the status bar lost their boxes and are separated
  by surface colors instead. Inputs and buttons have 4 px corners, menus and tooltips 6 px, tabs are flat
  with the selected one underlined, and a dialog's default button is filled in the accent color.
- **Themes are token sets**: Every Theme is a set of color tokens, so line numbers, bullets, the Table
  View, the Search line and the remaining widgets all take their colors from the active Theme instead of
  hard-coded values. User stylesheets apply on top of it.
- **Dashboard**: Recent Files, Favorites and Plugins as cards in one column, in the application font, with
  Open File as the primary action.

### Performance

- **Faster Searches**: A Search prepares its pattern once instead of twice; a Search with few Matches
  finishes about a third faster, measured on 2 million Log Lines.
- **Faster indexing**: Log File blocks are parsed on several cores with one scan for line feeds and tabs,
  and reading Log Lines no longer waits for the indexer.
- **Growing Log Files index only what was added**, and following one reads only the new bytes instead of
  the whole file on every change.
- **Smoother views**: Scrolling redraws only what it uncovers, and Quick Find, Highlighter changes and
  Search progress repaint without reading the Log Lines again.
- **Charts with millions of points**: A chart extracts only new Log Lines, draws only the visible range
  and finds the point under the mouse by binary search.
- **Faster startup**: A restored Session loads the current tab first, settings are read once, and plugins
  load after the first window is on screen.
- **Windows Search uses AVX2** where the CPU has it, and Windows and Linux releases are built with full
  optimization and link-time optimization.

### Installing and updating

- **Homebrew**: On a Mac, `brew install --cask 64x-lunicorn/tap/logsquirl` installs LogSquirl from its own
  tap, and `brew upgrade` updates it. The cask follows every stable release; betas stay out of it.

### Security and supply chain

- Qt 6.11.2 and OpenSSL 3.5.8 LTS on Windows.
- Every release ships an SBOM, signed checksums and build provenance, and is scanned for known
  vulnerabilities before it is published; a critical finding stops the release.
- Build inputs are pinned and CI is hardened; the crash report tool and the update check were reviewed.

### Removed

- **Chocolatey package**: LogSquirl was never published on Chocolatey — no workflow built the package and
  its install script pointed at a download that no longer exists.
- **Lua plugin support**: The optional Lua scripting layer was off by default and its entry points were
  never called. Plugins are native shared libraries using the C ABI.

### Bug fixes

64 fixes, among them: a reload is no longer lost to a change on disk, Filter frequency follows the
Search's Match case and regexp groups, file watch polling no longer stalls the UI, backreferences work in
patterns, Log Lines beyond 4 GiB within one block are found, saving no longer drops lines, and crashes in
QuickFind, in native file watching and in logging from several threads are gone.

The complete list is in the release notes on GitHub.
