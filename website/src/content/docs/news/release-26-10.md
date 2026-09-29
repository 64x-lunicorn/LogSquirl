---
title: Version 26.10.0
description: Smyck themes and live theme switching, Team Folder, Go to timestamp, JSON and logfmt Log Files in the Table View, faster Search and indexing, Homebrew, apt and dnf, 104 bug fixes.
release:
  version: 26.10.0
  date: 2026-09-29
  channel: stable
---

The largest release so far. The whole user interface was rebuilt on theme tokens, so themes switch without
a restart and reach every widget; a team shares Filter Groups and Highlighter Sets through a Git
repository; LogSquirl finds its way through a Log File by time; Search, indexing, reading and startup were
measured and made faster; and Homebrew, apt and dnf install and update it. The release itself is built,
scanned and signed by a hardened pipeline. 104 bug fixes.

These changes were tried out in 26.10.0-beta2 and 26.10.0-beta3 first; 26.10.0 contains everything from
both betas.

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
- **Sessions**: a restored Session opens on the tab that was in front, each Log File on the Log Line it
  showed. A Log File from an archive comes back from its archive without holding up the other tabs, and
  standard input, merged tabs and other tabs whose source is gone are left out.
- **spdlog Log Files are recognized**, with logger, level and body in their own columns, and the choice
  among several matching Log Formats no longer changes from one start to the next.
- **Follow** is one state for every view of a Log File, the Table View included.
- **The Table View colors Matches and Marks** in tints that fit the Theme.
- **Highlighter color presets**: 20 readable color pairs, one click sets text and background.
- **Counts before commands start with 0**: `05j` moves five lines down, `012k` twelve up.
- **Every language is translated throughout**; Traditional Chinese now works.

### Plugins

- **Installed into the user plugin folder**: a plugin from the catalog installs and updates into the
  folder *Plugin Folder* opens, not the application's folder, and an updated copy of a shipped plugin wins.
- **Configure…** in Plugin Management opens a plugin's own settings, and a plugin's menu action goes into
  the submenu of the Plugins menu it names.

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
- **apt and dnf**: Signed repositories at `packages.lunicorn-lab.de` carry the last three stable releases
  for Ubuntu, Fedora 44 and Oracle Linux 10; betas are not published there.
- **The update notice** tells a Homebrew, apt or dnf install to run its package manager's upgrade.
- **Windows installer**: shows the privacy policy and can turn the update check off for the installation.
- **The portable package keeps its data beside the executable**: Log Formats, plugins, the Team Folder and
  theme stylesheets sit next to `logsquirl_portable.exe`; the first start copies them over from the old
  place once.
- Qt 6.11.3; the Windows programs and the installer are called LogSquirl; `logsquirl_grep` ships with it.

### Security and supply chain

- OpenSSL 3.5.8 LTS on Windows.
- Every release ships an SBOM, signed checksums and build provenance, and is scanned for known
  vulnerabilities before it is published; a critical finding stops the release.
- Build inputs are pinned and CI is hardened; the crash report tool and the update check were reviewed.

### Documentation

- **The documentation is on this website**, one page per topic, and the user guide and the plugin
  developer guide are checked against the app and the plugin API.

### Removed

- **Chocolatey package**: LogSquirl was never published on Chocolatey — no workflow built the package and
  its install script pointed at a download that no longer exists.
- **Lua plugin support**: The optional Lua scripting layer was off by default and its entry points were
  never called. Plugins are native shared libraries using the C ABI.

### Bug fixes

104 fixes, among them: selecting a Log Line no longer hangs for seconds on large Log Files on macOS, a
reload is no longer lost to a change on disk, file watch polling no longer stalls the UI, Log Lines beyond
4 GiB within one block are found, saving no longer drops lines, `foo$` matches in CRLF Log Files, a merged
Log File follows its sources, the visibility shortcuts switch to what their names say, and crashes in
QuickFind, in native file watching and in logging from several threads are gone.

The complete lists are in the release notes of
[26.10.0-beta2](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0-beta2),
[26.10.0-beta3](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0-beta3) and
[26.10.0](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0) on GitHub.
