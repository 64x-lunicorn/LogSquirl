---
title: Version 26.10.0-beta3
description: Team Folder for shared Filter Groups and Highlighter Sets, Go to timestamp, JSON and logfmt Log Files in the Table View, apt and dnf repositories, 27 bug fixes.
release:
  version: 26.10.0-beta3
  date: 2026-09-28
  channel: beta
---

## Version 26.10.0-beta3 (September 2026)

The third beta of 26.10: a team can now share Filter Groups and Highlighter Sets through a Git
repository, LogSquirl finds its way through a Log File by time, JSON and logfmt Log Files get a Table
View, and Ubuntu, Fedora and Oracle Linux users install and update it with their package manager.
27 bug fixes.

This is a beta. Install it next to a stable version only if you want to try these changes early.

### Team Folder

- **Shared groups from a Git repository**: Name a Git repository in the options and LogSquirl keeps it
  current in the background. Its Filter Groups and Highlighter Sets show up as Team groups.
- **Edit and publish**: A changed or new Team group is committed under your own Git identity and pushed;
  when someone else changed the same group, you choose to keep yours, take theirs or save yours as a copy.
- **Share and copy back**: "Share with team" makes a Team copy of your own group, "Copy to my groups" does
  the reverse, and a Team group can be deleted for everyone.
- **Import and export one group at a time**, with a choice when an imported group already exists.

### Time

- **Go to timestamp** (`Ctrl+Shift+L`) jumps to the first Log Line at or after a time, in milliseconds
  even in ten million lines.
- **Search limits by time**: limit a Search to a time range (`Ctrl+Alt+T`) or to N minutes around the
  current Log Line (`Ctrl+Alt+W`).
- **Time zones and years count**: `+02:00` and `Z` are honoured, a syslog Timestamp without a year takes
  it from the file's date, and lookups run on a worker thread instead of blocking the window.
- **Δt column**: the Table View shows the time since the previous Log Line with a Timestamp.

### Log Files and views

- **JSON and logfmt Log Files get a Table View** with one column per field; bring your own Log Format.
- **Count values**: count the values of a Table View column or of a Search's capture group, most frequent
  first; a click searches for the value.
- **Standard input**: `journalctl -f | logsquirl -` opens and follows what arrives on a pipe.
- **A restored Session** opens on the tab that was in front, each Log File on the Log Line it showed.
- **Follow** is one state for every view of a Log File, the Table View included.
- **Highlighter color presets**: 20 readable color pairs, one click sets text and background.
- **Every language is translated throughout**; Traditional Chinese now works.

### Installing and updating

- **apt and dnf**: Signed repositories at `packages.lunicorn-lab.de` carry the last three stable releases
  for Ubuntu, Fedora 44 and Oracle Linux 10; betas are not published there.
- **The update notice** tells a Homebrew, apt or dnf install to run its package manager's upgrade.
- **Windows installer**: shows the privacy policy and can turn the update check off for the installation.
- Qt 6.11.3; the Windows programs and the installer are called LogSquirl; `logsquirl_grep` ships with it.

### Bug fixes

27 fixes, among them: selecting a Log Line no longer hangs for seconds on large Log Files on macOS,
QuickFind searches the Table View and no longer crashes when its view closes, Search Limits survive a
reload, `foo$` matches in CRLF Log Files, a merged Log File follows its sources, the Command Palette
opens, and check marks show on Windows.

The complete list is in the release notes on GitHub.

### Already in 26.10.0-beta2

Two Smyck themes and a flatter look, live theme switching, a reworked Dashboard, faster Search, indexing
and startup, Homebrew, a hardened release pipeline, and 64 bug fixes.

#### Themes and look

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

#### Performance

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

#### Installing and updating

- **Homebrew**: On a Mac, `brew install --cask 64x-lunicorn/tap/logsquirl` installs LogSquirl from its own
  tap, and `brew upgrade` updates it. The cask follows every stable release; betas stay out of it.

#### Security and supply chain

- Qt 6.11.2 and OpenSSL 3.5.8 LTS on Windows.
- Every release ships an SBOM, signed checksums and build provenance, and is scanned for known
  vulnerabilities before it is published; a critical finding stops the release.
- Build inputs are pinned and CI is hardened; the crash report tool and the update check were reviewed.

#### Removed

- **Chocolatey package**: LogSquirl was never published on Chocolatey — no workflow built the package and
  its install script pointed at a download that no longer exists.
- **Lua plugin support**: The optional Lua scripting layer was off by default and its entry points were
  never called. Plugins are native shared libraries using the C ABI.

#### Bug fixes

64 fixes, among them: a reload is no longer lost to a change on disk, Filter frequency follows the
Search's Match case and regexp groups, file watch polling no longer stalls the UI, backreferences work in
patterns, Log Lines beyond 4 GiB within one block are found, saving no longer drops lines, and crashes in
QuickFind, in native file watching and in logging from several threads are gone.

The complete list is in the release notes of 26.10.0-beta2 on GitHub.
