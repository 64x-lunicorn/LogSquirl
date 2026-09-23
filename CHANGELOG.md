# Unreleased

## Changes

- **Color presets for Highlighters**: The Highlighter editor offers 20
  ready-made color pairs, 12 soft pastels with dark text and 8 strong colors
  with white text; one click sets both the text and the background color.
  Every pair stays readable and stands out from the Log Lines in every
  Theme. The color dialog's basic colors are the same hues in tonal scales
  instead of Qt's stock set, and a new Highlighter takes the next soft preset
  instead of black on white. Existing Highlighters keep their colors (#423).
- **Every language is translated throughout**: More than half of the
  application's text had never reached the translations, so the charts, the
  plugins, the Presentation menu and parts of the Options Dialog stayed
  English in every language. All nine languages are now complete, the crash
  report and issue report dialogs included. Traditional Chinese, offered but
  never built, now works; Simplified Chinese, half English until now, is
  complete too (#448).

## Bug fixes

- **Selecting a Log Line no longer hangs on large Log Files on macOS**: With
  the Table View of a large Log File and an app on the Mac that uses the
  accessibility features, every click, double click or dragged selection in
  any view took seconds. The Table View no longer tells the accessibility
  clients which Row is selected, because Qt then rebuilt an accessibility
  element for every Row. A click takes milliseconds again (#425).

- **The close button of a tab is square again**: The red fill under the mouse
  was 14x20 pixels around a 16-pixel icon, taller than it was wide and larger
  than the button it belongs to. Its right margin was taken off the drawn box
  without anything taking off as much above and below (#418).
- **Menu icons on macOS**: macOS draws the menu bar's menus in the system's
  appearance, not the Theme's, so a dark Theme's white icons sat on a light
  menu and were hard to make out (and a light Theme's dark ones on a dark
  menu). Those menus now carry no icons on macOS, the way macOS menus usually
  look; the toolbar keeps its icons (#421).
- **Check marks on Windows**: A checked box was an empty filled square in
  the Windows build. The Themes draw check marks, arrows and close buttons
  from SVG files, and the Windows packages lacked Qt's SVG support; the
  installer and the portable zip now ship `Qt6Svg.dll` and its plugins (#427).

## Build and packaging

- **`logsquirl_grep` ships with LogSquirl**: Every build built the command line
  tool and no package carried it. It is now installed beside the application in
  the deb, the rpm, the AppImage, the Windows installer and the portable zip,
  and on macOS it sits in the app bundle, where it is called as
  `/Applications/LogSquirl.app/Contents/MacOS/logsquirl_grep`. The user guide
  documents the tool and its options, including that matches go to stdout and
  log messages to stderr (#430).
- **No library is named twice on the link line**: A macOS build of every target
  ended in `ld: warning: ignoring duplicate libraries`, fourteen archives across
  four executables, because a library that already arrives transitively was
  named a second time. Those mentions are gone. `src/app` keeps the TBB
  dependency `main.cpp` really uses, now through an interface target that
  carries TBB's headers and definitions without naming its archive again.
  Linux and Windows link unchanged (#450).

## Internal

- **Third-party code no longer drowns the project's own warnings**: The CPM
  packages were compiled with whatever warnings the project sets for itself, and
  a Windows build printed 1210 warnings out of them plus 394 command line
  warnings on top, so a new warning in LogSquirl's own code was one line among
  sixteen hundred. Third-party code is built without warnings now and its
  headers are system headers, and the three packages that turned warnings back
  on for themselves -- streamvbyte's `/Wall`, uchardet's `-ffloat-store` and the
  hyperscan fork's `-fpermissive` -- are asked not to. A new test fails if the
  flag that does it ever reaches LogSquirl's own targets, or stops reaching a
  third-party one (#452).
- **A warning from the link step fails the build**: LogSquirl builds with link
  time optimization, so the compiler generates code a second time at the link
  step -- and `-Werror` never reached that command line, because CMake puts a
  target's compile options on its compile lines only. GCC 12 and 13 printed
  `-Wstringop-overflow` on every appimage and noble run with everything staying
  green -- and so, it turned out, did GCC 14 on a fourth job nobody had looked
  at. The link step now fails on a warning too, on GCC and Clang. That one
  diagnostic is a GCC bug rather than a finding about this code, and is recorded
  as accepted with the versions it applies to and a guard that takes it back at
  GCC 16 (#454, `docs/adr/0009`).
- **Every source file of the project is built with the project's warnings**: Two
  MODULE libraries the tests load, the Plugin UI Port probe and the slow
  converter plugin, linked neither `project_warnings` nor `project_options`, so
  nothing checked their code at all and MSVC's C4996 on `getenv` was neither
  warned about nor suppressed the way the rest of the project does it. Both are
  built with the project's warnings and options now, and a new test fails if any
  target under `src/` or `tests/` compiles the project's code without them (#451).
- **The ctest discovery script sets the policies it relies on**: Every Linux CI
  run printed a CMP0007 developer warning eight times while ctest read its test
  list -- the only CMake warning that came from this project's own code. The
  script runs with `cmake -P` and inherits no policy from the project, so it now
  sets the two it relies on itself. Test discovery finds the same tests (#453).

## Documentation

- **A first bug report has a form to follow**: A report from outside arrives
  through an issue form that asks for the version, the operating system, how
  LogSquirl was installed and the size and kind of the Log File, the fields the
  crash reporter already pre-fills. The feature form asks what the reporter is
  trying to find out in their logs rather than what the app should do, and
  questions are pointed at the Q&A discussions, vulnerabilities at a private
  security advisory (#447).

# v26.10.0-beta2 (2026-09-23)

## Changes

- **Homebrew**: On a Mac, `brew install --cask 64x-lunicorn/tap/logsquirl`
  installs LogSquirl from its own Homebrew tap, and `brew upgrade` updates
  it. The cask installs the same signed and notarized DMG as the release
  page, for Apple Silicon and macOS 15 or later; `brew uninstall --zap`
  also removes settings, caches, formats and plugins (#377).
- **Homebrew cask follows every stable release**: CI Release sets the cask to
  each stable release as soon as it is published, after checking that it
  installs, so `brew upgrade` offers it right away; betas stay out of the
  cask (#378).
- **Windows Search uses AVX2 where the CPU has it**: The release build ships
  Hyperscan twice, as `hs.dll` for SSE4.2 and `hs_avx2.dll` built with
  `/arch:AVX2`, and loads the one the CPU supports at the first Search;
  MSVC has no equivalent of Vectorscan's Linux fat runtime, so this is a
  run-time choice between two DLLs instead. A CPU without AVX2 still gets the
  SSE4.2 build it always had (#281).
- **mimalloc, process-wide**: A `LOGSQUIRL_MIMALLOC_OVERRIDE` option lets
  mimalloc serve `malloc`/`operator new` for the whole process, Qt included,
  where its override mechanism actually works. Wired for Linux and Windows;
  refused on macOS, where both of mimalloc's documented override mechanisms
  were measured and found broken (one never reaches Qt's allocations, the
  other crashes AppKit at startup) (#282).
- **Smyck theme**: A fourth theme, after the SMYCK terminal color scheme
  (https://color.smyck.org/): a dark theme in its background, its grays and its
  ANSI colors, with the same sizes and shapes as Dark. Choosing it also colors
  the nine color labels in SMYCK's colors, and choosing another theme colors
  them back; a color label you picked a color for yourself is left alone
  (#353).
- **Smyck Light theme**: The same scheme on light surfaces, for a bright room:
  SMYCK's light white as the window color, its background as the text color,
  its selection color as the accent and its ANSI colors for status, errors and
  links. It carries the same color labels as Smyck, so switching between the
  two leaves them alone (#353).
- **Themes differ in color only**: Light and High Contrast use Dark's sizes
  and shapes (combo box arrow, menu item padding and icon offset, tab add
  button); High Contrast keeps only its thicker borders and outlines. The tab
  close button is neutral in every Theme and red only on hover, the toolbar
  path field reads as a read-only field with a visible edge, and combo box
  popups highlight their current item the same way in every Theme (#264).
- **Flatter look**: Within the same layout, panes, header cells, tool bars and
  the status bar have no boxes; areas are separated by surface colors, and
  group boxes are bold section titles. Inputs and buttons have 4 px corners,
  menus, popups and tooltips 6 px. Tabs are flat with the selected one
  underlined, and their close button shows on the selected tab and under the
  mouse. A dialog's default button is filled in the accent color. Scroll bars
  are 8 px wide. Dark uses five steps of gray, and a Dark palette saved by an
  earlier version no longer keeps the old, darker window color. High Contrast
  keeps square corners and every border; its default button is not filled but
  shows yellow text, and focus in yellow like every button (#265).
- **Dashboard**: The dashboard uses the application font instead of fixed
  small sizes, shows Recent Files, Favorites and Plugins as cards in one
  column, and emphasizes Open File as the primary action (#266).
- **Less work per line**: Logging does not flush after every message; encoding
  detection reads a 256 KB sample instead of the whole 5 MB block; expanding
  tabs, painting ASCII text and line numbers, and extracting Log Format fields
  do less work per line; Sentry's debug output is on only in debug builds
  (#304).


- **Themes are token sets**: Light, Dark and High Contrast are each defined
  by one set of named tokens, from which both the Qt palette and the
  application stylesheet are generated (one stylesheet template replaces the
  three hand-written `.qss` files). Their look is unchanged.
- **System theme**: The new "System" style follows the operating system's
  light or dark color scheme, also when it changes while LogSquirl runs.
- **Theme changes without a restart**: Choosing a theme in the Options dialog
  applies it at once to every open window, including icons, tab bars, dialogs
  and a floating sidebar; only a language change still asks for a restart.
- **User stylesheets apply on top**: A `dark.qss`, `fusion-light.qss` or
  `high-contrast.qss` in `<AppConfigDir>/themes/` is now added after the
  built-in stylesheet instead of replacing it, so it only needs the rules it
  changes. The `[dark]` settings group can override any Dark token by name.
- **Widgets follow the theme**: High Contrast and System (on a dark operating
  system) now show the light icons in the toolbar, the tab close buttons and
  the sidebar title bar, like Dark does. The tab close-button hover color, the
  plugin status badges and the welcome page's plugin status come from the
  theme's tokens (`CloseButtonHover`, `StatusOk`, `StatusWarning`,
  `StatusInactive`, `StatusInfo`, `StatusText`), so High Contrast gets its own
  colors for them.

- **Grown Log Files index only what was added**: With the index cache on,
  reopening a Log File that has grown since its Index was cached indexes
  only the appended part, starting from the last cached Log Line, as long as
  the start and the cached end of the file are unchanged. Progress starts at
  the cached share and never goes backwards; any other change to the file
  still indexes it in full.
- **Index Cache housekeeping**: The index cache now removes the entries it
  lets go of least recently opened first, not least recently written. An
  Index larger than the whole cache size limit is not written, so it no
  longer pushes existing entries out, and the entry just written is never the
  one removed. A Log File under the temporary directory is not cached also
  when it is reached through a symbolic link (such as `/var` and
  `/private/var` on macOS), and a Log File that is only locked for the moment
  keeps its cached Index.
- **One context menu for both Presentations**: The Text View and the Table
  View offer the same context menu entries in the same order. The Table View
  gains Find next, Find previous, Set search start, Set search end and Clear
  search limits; only Set selection start and end stay with the Text View. In
  the Table View, the search entries, Color Labels and the scratchpad act on
  the selection (a double-clicked word, or the selected Rows as tab-separated
  cells) instead of the text of the cell that was right-clicked. Mark, Copy,
  Copy with line numbers, the scratchpad entries and Save selected to file
  are disabled while nothing is selected.
- **Reload recognizes the Log Format again**: Reloading a Log File runs
  Format Recognition again, so an edited user-defined Log Format is picked up
  by a reload.
- **Recording shortcuts**: In the Options dialog's shortcuts table, a
  shortcut is recorded by clicking its cell or pressing Enter on it; the
  "..." button beside each cell is gone. Escape or Tab cancels the recording,
  and Backspace, Delete or the clear icon clears the shortcut. A recording
  holds one key combination: shortcuts made of several key combinations can
  no longer be recorded, but existing ones are kept. Shortcuts are stored as
  portable text such as `Ctrl+O`; shortcuts stored in the native form still
  load.
- **Search line and sidebar width**: The Search line keeps room for about 20
  characters while the sidebar is open; when the Search row runs out of
  width, the visibility box shrinks and the match count is shortened first
  (the whole count is in its tool tip). The sidebar opens at about a quarter
  of the window instead of almost half, and the width it is left at is saved
  with the window in the Session and used the next time it opens.
- **Line numbers and bullets follow the Theme**: The bullet zone and line
  numbers of the Text View and the Filtered View take their colors from the
  Theme (Tokens `ViewportMargin`, `ViewportMarginBorder`, `LineNumberText`,
  `Bullet`, `BulletOutline`) instead of a fixed dark gray with white numbers.
  In Light the margin is now light gray with dark line numbers; Match and
  Mark bullets keep their own colors.
- **Readable controls in every Theme**: Radio buttons and sliders are drawn
  from the Theme's Tokens, so they no longer vanish in Dark and High
  Contrast. Check boxes are 16px in every Theme (the Options dialog no longer
  changes height with the Theme), show a muted check mark when disabled and
  a dash when partly checked. The Command Palette's category badges and
  shortcuts stay readable in every Theme, also on a selected row, and Matches
  and Marks standing for few Log Lines stay visible in the overview on dark
  backgrounds.
- **High Contrast states**: A checked button in the Search row shows a dark
  icon on its yellow background, a progress bar's filled part is black with
  a yellow outline so its label stays readable, a hovered push button shows
  its text in yellow, and a disabled push button has a dashed border.
- **Theme colors for the remaining widgets**: The pull-to-follow bar, the
  chart tooltip, a conflicting shortcut, an invalid or failed Search and the
  overview's highlight frame are drawn in Theme colors (new Tokens
  `PullToFollowStripe`, `ErrorBackground`, `ErrorText`), and the hints on the
  welcome page and in the Plugins dialog use the secondary text color.

- **Scrolling through wrapped Log Lines**: With text wrapping on, the mouse
  wheel (also with Alt), the arrow keys, Page Up/Down and selection
  autoscroll move by Visual Lines, so a Log Line taller than the Viewport can
  be scrolled through to its end. Page Up/Down move by one Viewport height.
  The scrollbar still counts Log Lines. When the view starts partway through
  a Log Line, its upper rows show no bullet and no line number.
- **Bottom of a wrapped view**: At the bottom of the scrollbar, with the
  arrow keys, the wheel, Page Down and follow mode, the last Visual Line of
  the Log File sits exactly on the last row, also when the last Log Line is
  taller than the Viewport. A short Log File whose Log Lines wrap past the
  Viewport can now be scrolled.
- **Reading position kept on re-wrap**: Resizing the window, changing the
  font or showing line numbers keeps the text that was on the top row at the
  top, also partway through a wrapped Log Line. A view at the bottom stays at
  the bottom.
- **Jumps move the view only when needed**: Going to a line, to the next
  Match or Mark, to a QuickFind result, or selecting a Log Line in the
  Filtered View leaves the view where it is when the target is already fully
  visible. Otherwise the target is put on the top row instead of in the
  middle. A click on the overview strip still centres its Log Line.
- **Mark and Match rows in the Table View**: The Table View colors the
  background of a Row that is a Match, a Mark or both, in the colors of the
  Text View's bullets. Whole-line Highlighters and Search Limits take
  precedence, and Context Lines are dimmed.
- **Table View colors like the Text View**: A whole-line Highlighter colors
  the whole Row, Matches of the Search are highlighted in their cells, Rows
  outside the Search Limits are subdued, and a partly selected Row keeps its
  Highlighter colors outside the selection. A Context Line stays dimmed when
  it is selected as a whole.
- **QuickFind matches the Log Line as written**: QuickFind looks at the Log
  Line's own characters instead of the text with tabs expanded, so a pattern
  for a tab finds tabs, and a pattern for spaces no longer finds the spaces
  a tab is drawn as.
- **Saving lines to a file**: While the lines are saved, the progress dialog
  keeps updating and Cancel stops the save at once. A cancelled or failed
  save leaves the destination file untouched. A save from the Filtered View
  writes the Log Lines shown when it started, even if a Search or Mark change
  arrives during it.
- **Copying**: Copying with nothing selected leaves the clipboard as it was,
  and both Presentations copy a null character in a Log Line as a space.
- **Filtered View navigation**: Jump to bottom in the Filtered View selects
  the last Log Line it shows. With nothing shown, jump to top and Mark
  navigation do nothing.
- **Saved Marks applied once**: The Marks saved with the Session are applied
  when the Log File is first loaded. Reloading the Log File clears the Marks
  and no longer brings the saved ones back.
- **Failed Search**: A Search that fails shows "Search failed" and offers to
  report the problem; it used to stay running forever.
- **Faster Searches**: A Search prepares its pattern once instead of twice,
  and a Search with few Matches finishes about a third faster (measured on a
  Log File of 2 million Log Lines).
- **Faster indexing**: Log File blocks are parsed on several cores with one
  scan for line feeds and tabs, and reading Log Lines no longer waits for the
  indexer. The longest-line width of lines with tabs is now the real tab-stop
  width, so the horizontal scroll range fits them exactly.
- **Following a Log File reads only what was added**: a growing Log File is
  checked from its start and end instead of being read in full on every
  change.
- **Faster reads**: Quick Find, the Filtered View, saving displayed lines and
  the grep command line tool read Log Lines in batches; hiding ANSI color
  sequences, patterns Vectorscan rejects and Log Files in UTF-16 or Latin-1
  no longer decode or compile per Log Line.
- **Smoother views**: scrolling redraws only the Visual Lines it uncovers,
  Quick Find typing, Highlighter changes and Search progress repaint without
  reading the Log Lines again, a running Search updates the displayed lines
  and the overview by what changed, and the Table View decides a row's colors
  once per row.
- **Charts with millions of points**: a chart follows a growing Log File by
  extracting only the new Log Lines, draws only the visible range and finds
  the point under the mouse by binary search.
- **Faster startup**: a restored Session loads the current tab first,
  settings are read once, plugins load once after the first window shows,
  and a second instance hands its Log Files over at once.
- **Release builds**: Windows and Linux releases are built with full
  optimization, link-time optimization covers every LogSquirl library, and
  mimalloc is updated to 2.5.2.
- **Benchmarks against master**: a manually started CI workflow builds a
  branch and master optimized and reports their benchmarks side by side.

## Bug fixes

- **A reload is not lost to a change on disk**: A reload asked for while the
  Log File was still being indexed or checked waited in a slot the next change
  on disk overwrote with a check, and when that check found nothing changed in
  what the Index covers, the reload never ran -- a rewrite in place went
  unseen, and an Encoding chosen meanwhile was dropped. The log data now
  decides which of two index jobs waits by one rule, strongest first: Attach,
  explicit reload, automatic full reindex, check, partial reindex; an Attach
  takes the Encoding a reload forces. The "truncated" state the log data kept
  so a truncation would not be lost the same way is gone (#395).
- **Filter frequency follows the Search's Match case and regexp groups**:
  With Match case off, Show Filter Frequency counts a word in any case, so a
  Search for `error` charts the `Error` and `ERROR` lines too; the series
  keeps this across a restart. A regexp Search is split only at a top-level
  `|`: `(a|b)c` is charted as one series instead of nothing, a `|` inside a
  character class, escaped or inside `\Q...\E` stays part of its
  alternative, and `x|y` is still charted as two series.
  Other chart series match case as before (#411).
- **Filter frequency counts what a logical Search matches**: Show Filter
  Frequency reads the sub-patterns of a logical Search the way the Search
  does, with the same parser: an escaped quote `\"` and the backslashes
  before a quote are unescaped, `"a or b"` stays one sub-pattern, and
  sub-patterns joined with `and` or `not(...)` are charted too, each on its
  own, the excluded one included. With regular expressions off the
  sub-patterns are counted as fixed strings, not as regexps. Plain and
  non-logical regexp Searches are charted as before (#410).
- **Adding a word to a plain Search keeps both words searchable**: With
  neither regular expression nor logical combination on, adding a word to a
  non-empty Search, or combining several Predefined Filters, switches the
  logical combination on and gives `"alpha" or "beta"` instead of
  `alphabeta`, which matched neither word's Log Lines. Adding a word to an
  empty Search, or using a single Predefined Filter, stays plain; the
  regexp and logical combination modes are unchanged (#408).
- **Excluding a word from an empty Search gives not("word")**: Excluding a
  word from an empty Search Line in plain text or regexp mode gives
  `not("word")` instead of `"" and not("word")`, and switches the logical
  combination on as before (#407).
- **Stopping a Search says how many Matches it found**: After Stop, the
  Search Line says how many Matches the Filtered View holds, "1 match found"
  or "7 matches found", instead of always "0 match found". The same after a
  reload, a Log File truncated on disk or switching auto-refresh (#406).
- **A word ending in a backslash keeps a logical Search valid**: In the
  logical combination mode a run of backslashes right before a quote of a
  sub-pattern is now written and read doubled: adding a word such as
  `C:\temp\` to the Search, excluding it or replacing the Search with it
  writes `"C:\temp\\"`, and a regexp Predefined Filter ending in `\\`
  combines the same way, so the Search matches instead of failing with
  "Pattern has unmatched quotes". A
  backslash anywhere else is read as written, so `"\d+"` still matches digits;
  only a hand-written pattern with two or more backslashes right before a
  quote is read differently (#405).
- **A word with quotes keeps a logical Search valid**: Adding a word that
  contains a `"` to the Search, excluding it, replacing the Search with it or
  combining Predefined Filters with it in the logical combination mode writes
  the inner quote as `\"`, so the Search runs and matches the word with its
  quotes instead of failing with an error in the expression. Excluding a word
  from a Search that is not a logical combination and contains a quote gives a
  valid one too (#398).
- **File watch polling stops stalling the UI**: The poll tick now runs on a
  thread of its own and stats each watched file with no lock held, instead of
  holding the file watcher's lock across every `QFileInfo` stat on the thread
  that owns the UI. With many open Log Files, or files on a slow or network
  drive, adding or removing a watch as a Log File is opened or closed no
  longer waits behind the poll (#322).
- **A local build knows its own version**: A build made without a version in
  the environment -- every build a developer makes -- called itself 26.7.0
  while the release it was cut from is 26.07.0: CMake dropped the leading zero
  of a version component below `cmake_minimum_required(VERSION 3.16)`, where
  policy CMP0096 keeps it. The project now asks for 3.16, so `--version`, the
  Windows resource and the handshake between two instances all spell the
  version the way `CMakeLists.txt` does. Releases, which are handed their
  version, were never affected (#372).
- **`--version` says which version it is**: Asking either binary for its
  version printed only its own name. The desktop application on macOS was the
  one place it worked, and by accident: Qt fell back to the application
  bundle's property list. Both binaries now report the version, the build date
  and the commit they were built from (#368).
- **Backreferences in a pattern**: A pattern that refers back to one of its own
  groups, such as `(ERROR) \1`, now finds the Log Lines that repeat the
  captured text. A Search with one was refused as invalid, complaining about a
  group that is plainly there, and a Color Label with one quietly colored
  nothing: every pattern was compiled without capturing groups, which leaves a
  backreference with nothing to refer to. Only a pattern that uses a
  backreference now captures, so every other pattern keeps the faster
  capture-free path (#336).
- **Reloading a Log File reads it again**: A Log File rewritten in place with
  the same size was still shown as it was indexed when its modification time
  had not changed -- on a file system with coarse timestamps, or one that
  writes the time late. Reloading it now compares every byte the Index was
  built from, and reads the Log File again where they differ. Opening and
  following a Log File are unchanged, and so is what following a growing one
  costs (#337).
- **`logsquirl_grep -d` prints its debug output**: Asking the command line
  tool for debug output aborted it instead. Reading the settings looked up the
  main font in the font database, which only the desktop application has, and
  the log line that does so is written only once `-d` raises the log level --
  so the flag had apparently never worked (#345).
- **Log Lines are drawn in the font you chose**: At startup the main view drew
  its Log Lines in the system UI font, in rows laid out for another font, so
  each row painted over the descenders of the row above it. The view now draws
  in the font it was given, whatever a Theme's stylesheet does to the widget's
  own font (#354).
- **The Viewport shows every column that fits**: A Log Line now runs to the
  right edge of the Viewport instead of stopping a few characters short of it,
  and a click lands on the character under the pointer however far right it
  sits. Columns are measured with the advance the text is painted with, which
  Qt counts in fractions of a pixel; measuring them in whole pixels lost a
  fraction per column and whole characters across a Viewport (#352).
- **Multi-frame .lz4 files**: A `.lz4` Log File made of several frames
  (block-streamed output, concatenated files) opens completely instead of
  stopping after the first frame, and a truncated or corrupt one reports an
  error instead of loading partially (#325).
- **A last Log Line that stopped matching**: When a Log File grows, its last
  Log Line may have been incomplete when it was searched. If it matched then
  and does not any more once it is complete — with an exclude pattern, say —
  the Search drops its Match when it searches that Log Line again, and the
  match count and the Filtered View follow instead of keeping a line that no
  longer matches (#330).
- **A Search superseded before it started**: A new Search that was superseded
  before it got to run — several patterns typed in quick succession, say — left
  the results of the Search before it in place. A Search continued right after
  it, as one following a growing Log File is, then went on from those results
  and showed the previous pattern's Matches beside its own; it now starts from
  nothing and shows only the Matches of the pattern it runs (#331).
- **Log Lines beyond 4 GiB within one block**: A Log File in which 128
  consecutive Log Lines span 4 GiB or more (one very long Log Line is enough)
  shows the right Log Lines; their positions in the Index are no longer
  truncated to 32 bits, which showed wrong Log Lines in a Release build and
  aborted a Debug build. The Index Cache format changes with it, so each Log
  File is indexed once more after the update (#321).
- **Main font**: Every Configuration uses the fixed-pitch main font style, not
  only the first one created, so the saved font does not depend on which
  settings were read first (#229).
- **A chart keeps its zoom**: A chart that follows a growing Log File keeps the
  view you zoomed or panned to instead of fitting the whole data again on every
  append. It fits the view again when a series is added, edited or removed, a
  preset is loaded, or you press Fit; a chart you never zoomed keeps following
  the data as before (#329).
- **Encoding in the grep command line tool**: `logsquirl_grep` reads its Log
  File in the Encoding the application detects for it, or in the one the
  settings force, instead of always reading it as ISO-8859-1. A UTF-8 Log Line
  with non-ASCII text prints as it is in the Log File rather than
  double-encoded, a UTF-16 or Latin-1 Log File prints as UTF-8 text, and a
  pattern with non-ASCII text finds its matches (#326).
- **Warnings of the grep command line tool**: `logsquirl_grep` writes its log
  messages to stderr instead of stdout, so a warning such as "Non LF
  terminated file" no longer lands between the matches. Its stdout carries
  only the Log Lines the Search matched and can be piped into another tool;
  the warnings are still shown, and `-d`/`--debug` writes its messages to
  stderr too (#327).


- **Multi-frame .zst files**: A `.zst` Log File made of several frames opens completely, and a truncated
  one reports an error.
- **Match count**: The match count stays exact while a Search follows a growing Log File.
- **Portable arm64 builds**: Apple Silicon and Linux arm64 builds target a portable CPU.
- **Plugins in every window**: Converter plugins handle the file extensions they declare, and plugin menu
  entries show in every window.
- **Marks of restored tabs**: A restored tab that had not loaded yet keeps its Marks when the Session is
  saved.
- **Search Limits in the Table View**: The Table View subdues exactly the Log
  Lines outside the Search Limits, like the Text View; it used to leave the
  Log Line right after the end of the limits unsubdued. The Filtered View no
  longer draws a Log Line shown before the start of the Search Limits as
  inside them.
- **Next and previous Mark**: In the Filtered View, "next Mark" now moves down
  and "previous Mark" up, as in the main view; they were swapped. In the main
  view, "previous Mark" no longer skips a Mark when the selected Log Line has
  none.
- **Kept Searches**: Color Labels, Search Limits and shortcut changes now reach
  the Filtered Views of kept Searches, not only the current one, and a
  Filtered View built for a newly kept Search starts with the current Color
  Labels and Search Limits.
- **Highlighter Set changes in every Log File**: Changing the Highlighter Sets
  re-colors the Color Labels of every open Log File, in every window, not only
  the current tab.
- **Zoom and settings reach every Log File**: A zoom changes the font of every
  open Log File and nothing else, and a changed font or shortcut in the
  Options reaches Log Files in background tabs without switching to them.
- **QuickFind on a whole-line selection**: A Table View Row selected as a
  whole now shows its QuickFind matches, as the Text View does.
- **Searching while loading**: A Search requested while a Log File is first
  loading now runs over the whole Log File once loading has finished.
- **logsquirl_grep failures**: The command line tool prints an invalid
  pattern, a failed load or a failed Search to stderr and exits with a
  non-zero code instead of hanging, and reports a missing Log File argument
  instead of crashing.
- **Plugin widgets at exit**: A status or footer widget a plugin had removed
  was still deleted with the main window. That could crash LogSquirl on exit
  when the plugin deleted the widget itself or its library was already
  unloaded, and each disable/enable of a plugin left a toolbar entry behind.
- **Widgets of a disabled plugin**: A widget or menu action a plugin
  registered from a background thread just before it was disabled no longer
  shows up after it is gone.

- **Crash with native file watching**: Closing and opening Log Files in the
  same directory with native file watching could corrupt memory and crash
  LogSquirl later, often during a Search. File watching now uses efsw 1.7.2,
  which fixes this on Windows and Linux.
- **Crash in QuickFind in the Filtered View**: QuickFind in the Filtered View
  could crash while a Search was adding Matches or Marks changed. It could
  also select a different Log Line than the one that matched when Marks
  changed during the QuickFind; it now goes on to the next displayed match
  instead.
- **Crash when logging from several threads**: Log messages written from
  several threads at once, as a running Search does, could corrupt memory and
  crash LogSquirl.
- **Search stalls**: A Search could stop making progress, most likely on a
  machine with few processor cores, and wait until another Search replaced
  it. It now always runs to completion.
- **Saving drops lines**: Saving a Log File or a selection whose line count is
  a multiple of 5,000 left out the last 5,000 Log Lines; a save of exactly
  5,000 lines wrote an empty file.
- **Cleared Marks in the Filtered View**: Clearing the Marks removes them from
  the Filtered View; they used to stay displayed.
- **Search results from another mode**: A Search could in rare cases show the
  cached results of the same pattern run as an exclude, boolean or plain-text
  Search.
- **Stale Context Lines**: Repeating an earlier Search shows the Context Lines
  of its own Matches instead of those of the Search before it, and clearing a
  Search removes its Context Lines.
- **Stopped Search**: A stopped Search no longer reports itself as complete,
  and its partial results are not reused for the next identical Search.
- **Search state after switching tabs**: Switching away from a tab while its
  Search runs no longer puts that Search's buttons, progress or a stale status
  line onto the newly shown tab.
- **Search settings reach every Log File**: A changed Search setting, such as
  the number of Context Lines, reaches every open Log File and the Filtered
  Views of kept Searches, not only the current tab. Changing a Highlighter Set
  no longer restarts file watching or rebuilds Context Lines.
- **Options applied during a run**: Applying the Options while a Log File is
  indexed or searched no longer changes that run partway through; the new
  settings take effect on the next one.
- **Fonts that are not fixed-pitch**: If the font chosen for Log Lines turns
  out not to be fixed-pitch on this system, for example because it is not
  installed and another one is substituted, the view uses a fixed-pitch font
  that is actually installed instead of drawing misaligned text. The Options
  still show the font that was chosen.
- **Clicks before the first paint**: A click or hover in a text view before
  it was first drawn is placed correctly; a click in the bullet zone could
  toggle a Mark by accident or fail to toggle one.

- **Search highlight colors are kept**: The main Search and QuickFind
  background colors chosen in the Options were saved but never loaded, so
  they fell back to their defaults on the next start.
- **Settings reach every open Log File**: Changing a main Search or QuickFind
  color re-colors every open Log File, and the View menu's line number and
  overview toggles change every open Log File, not only the current tab. A
  Log File being opened draws in the configured colors and font from its
  first frame.
- **Zoom keeps bold and antialiasing**: Zooming no longer drops a bold font
  or forced antialiasing, and zooming from a font size that is not in the
  offered list (such as one set in the settings file) steps to the nearest
  offered size instead of an arbitrary one.
- **Hiding ANSI color sequences**: Switching "Hide ANSI color sequences"
  updates the Presentations and Filtered Views of every open Log File at
  once, background tabs included, instead of only once something else
  repainted them.
- **Table View column widths**: Column widths saved for a Log Format survive
  reopening the Log File; they used to be overwritten by automatic sizing
  right after being restored.
- **Save selected to file in the Table View**: Saves the Log Lines of the
  selected Rows, independent of the Text View's selection.
- **Searching from the Table View**: Text sent to the Search from the Table
  View's context menu is no longer escaped twice.
- **Views after a Theme switch**: An open Text View repaints in the new
  Theme at once instead of keeping the previous Theme's colors until
  something else repainted it.
- **Theme details**: The spin box's up arrow points up in Light and High
  Contrast, and Dark no longer draws bright lines under the toolbar and above
  the tabs.
- **Shortcut conflicts**: A conflict in the first row of the shortcuts table
  no longer marks every shortcut cell once another shortcut is edited.

- **Update notifications**: A LogSquirl installed from a release is offered
  the newer stable release, and with "check for beta versions" also a newer
  beta; a LogSquirl running a beta is offered the next beta or the stable
  release. The option used to have no effect, and every notification linked
  to a "continuous" build page that does not exist. The notification lists
  the changes up to the offered release.

## Security

- **OpenSSL on Windows**: The Windows installer and portable zip ship OpenSSL
  3.5.8 LTS, which has no known vulnerabilities. LogSquirl 26.07.0 for Windows
  bundles OpenSSL 3.6.2, with 28 known vulnerabilities, three of them
  critical: CVE-2026-63073 (CVSS 9.8), CVE-2026-34182 (9.1) and
  CVE-2026-75803 (9.1); Windows users of 26.07.0 should update (#225, #199).
- **Update offers**: The update check offers a release only when its link in
  the update feed points to a LogSquirl release page on GitHub; any other
  link is ignored and logged (#222).
- **Crash report tool**: The crash report dialog's minidump tool is
  rust-minidump's `minidump-stackwalk`, downloaded at build time from a pinned
  release, checked against its SHA-256 and listed in the release SBOM. It
  replaces Breakpad executables of unknown origin that were committed to the
  repository; the dialog shows a readable crash report with the stack of each
  thread (#318).
- **SBOM of the AppImage**: The release SBOM lists the Ubuntu packages of the
  system libraries the AppImage bundles, with `pkg:deb` purls, so the
  vulnerability scan covers them (#227).


- **Qt 6.11.2**: All packages are built with Qt 6.11.2 instead of 6.10.3,
  and the Windows, macOS and AppImage packages bundle it. It fixes
  CVE-2026-9499 (Qt5Compat), CVE-2026-19248 (Qt XML), CVE-2026-76151 (Qt
  Network) and CVE-2026-6210 (Qt SVG).
  CVE-2026-15037 (Qt XML, QDom serialization, CVSS 2.9) is fixed only in Qt
  6.12.0 and is accepted as a known risk until then: LogSquirl re-serializes
  XML only to show it back to the user.
- **OpenSSL 3.5.8 LTS on Windows**: The Windows packages ship the OpenSSL
  DLLs from a pinned, SHA-256-verified OpenSSL 3.5.8 build, OpenSSL's
  long-term support line. They used to come from whatever Chocolatey offered
  when the build cache was filled; LogSquirl 26.07.0 shipped OpenSSL 3.6.2.
- **Signed checksums and build provenance**: Every release asset is listed in
  `logsquirl-<version>-sha256.txt`, which now covers all Windows, macOS and
  Linux packages, debug symbols and dependency archives, and uses plain file
  names so `sha256sum -c` works in a download folder. The checksum file is
  signed keyless with Sigstore
  (`logsquirl-<version>-sha256.txt.sigstore.json`), and every asset carries
  a GitHub build provenance attestation. The release notes have a
  "Verifying downloads" section with the `cosign verify-blob`, `sha256sum -c`
  and `gh attestation verify` commands.
- **SBOM**: Every release publishes a CycloneDX 1.6 SBOM,
  `logsquirl-<version>-sbom.cdx.json`. It lists the pinned dependencies of
  the released commit and the Qt, OpenSSL and ICU versions found inside the
  AppImage, the Windows zip and the macOS app, and is attested as the SBOM of
  every other asset.
- **Vulnerability scan before release**: The release SBOM is scanned for known
  vulnerabilities with grype, OSV and Qt's own list of security advisories.
  A critical finding, or a Qt advisory that NVD has not scored yet, stops the
  release until it is fixed or accepted with a reason and an expiry date.
  Master's dependencies are also scanned daily.
- **A release ships the tested build**: A release tag no longer rebuilds. The
  release takes the packages that passed CI on master for the tagged commit
  and checks that they belong to that commit and carry the tag's version.
  Only then are the macOS app and DMG signed, notarized and stapled, in a
  protected release environment; builds of pull requests and branches are
  never signed.
- **Pinned build inputs**: Every dependency fetched by CMake is pinned to a
  full commit, and the build tools downloaded in CI (among them linuxdeploy
  for the AppImage, NSIS, create-dmg, Boost and sentry-cli) are pinned to
  exact versions and refused when their SHA-256 does not match. The Linux
  build images are pinned by base image digest, scanned, and signed; CI
  verifies their signature before building with them.
- **Hardened CI**: Every GitHub Action is pinned to a commit, limited to an
  allowlist and audited with zizmor and CodeQL on every change; workflows run
  with a read-only token and the repository is rated weekly by OpenSSF
  Scorecard. The website is deployed over FTPS.

## Removed

- **Chocolatey package removed**: The Chocolatey package source is gone.
  LogSquirl was never published on Chocolatey: no workflow built the package,
  and its install script pointed to a download that no longer exists.

- **Lua plugin support**: The optional Lua scripting layer
  (`LOGSQUIRL_USE_LUA`) has been removed together with its Lua and sol2
  dependencies. It was off by default and its data-source and converter entry
  points were never called. Plugins are native shared libraries using the C
  ABI; a manifest whose `library` ends in `.lua` is now loaded like any other
  library and fails with the normal load error.

## Build and packaging

- **macOS releases carry line info**: The macOS release build now uses
  RelWithDebInfo, same as Windows and Linux since #280, so its dSYM resolves
  a symbolicated crash to a file and line instead of just a function; the
  build stays at full optimization, with the intermediate link-time
  optimization object kept for `dsymutil` to read (#340).
- **Linux packages declare Qt**: The DEB and RPM packages depend on the
  distribution's Qt 6 packages, with the Qt version LogSquirl is built with as
  the minimum. On a distribution with an older Qt the package manager refuses
  the install instead of LogSquirl failing to start; use the AppImage there.
  The packages no longer ship CRoaring's static library and headers (#226).
- **No fast math**: The build no longer uses `-ffast-math` / `/fp:fast`, so
  chart aggregation follows IEEE floating point rules (#304).
- **Packaging recipes pass options that exist**: The Arch recipe builds
  `RelWithDebInfo` instead of the misspelled `RelWithDebugInfo`, which CMake
  took as a build type of its own and so built without optimization and
  without debug information; the Gentoo ebuild passes
  `-DLOGSQUIRL_MIMALLOC_OVERRIDE=OFF` instead of the long-removed
  `-DLOGSQUIRL_USE_MIMALLOC=OFF`. A test compares every `-D` option under
  `packaging/` with the options the project declares, and every build type
  with the ones CMake knows (#333).
- **Hash-pinned Python tools**: Every pip install in CI and the build images
  uses hash-locked requirements with `--require-hashes`, and aqtinstall runs
  from a throwaway directory, so no stale Python packages (setuptools,
  msgpack) stay in the images. Renovate keeps the requirements and their
  hashes current (#317).


- **Fedora 44**: The installation of the Fedora RPM is now tested on
  Fedora 44, the release it is built on, instead of on Fedora 43.
- **Qt 6.11 build requirements**: Building with Qt 6.11 needs the
  qtdeclarative module installed as well, because Qt's `lrelease` links
  Qt Qml when it generates the translations.
- **Dependency updates**: Renovate keeps the CMake dependencies, Qt and the
  pinned tool versions current and recomputes their SHA-256 checksums;
  Dependabot covers the actions, build images, Python test packages and the
  website, which now builds with Astro 7.
- **Faster and stricter CI**: Builds on all platforms use sccache, the Windows,
  macOS and end-to-end jobs run in parallel, and one "CI passed" check gates
  merges. A clang-format check, an AddressSanitizer/UndefinedBehaviorSanitizer
  test run, one CTest test per Catch2 test case with JUnit reports, and job
  timeouts were added.

- **Release notes from the changelog**: The notes of a GitHub release are
  its section of this changelog, followed by how to verify the downloads. A
  release without a changelog section is stopped before anything is signed;
  26.07.0 was published with empty notes.

- **Release checks before merge**: A pull request is checked before merge
  for a valid update feed, a website that builds without broken links, a
  CHANGELOG entry (or the `no-changelog` label) and, when it changes the
  version, a complete release preparation. Every pull request now reports
  the required CI check, also one that changes only the website or the feed.
- **Website release pages**: The release overview, the home page and the
  sidebar are generated from the release pages, and a release's page goes
  live once its GitHub release is published. The 26.06 page names the
  published release, 26.06.1.

## Internal

- **The Search Session hands the Displayed Lines a matches delta**: With
  every state change the Search Session calls one callback, synchronously and
  right before it reports the change, with what changed in its Matches: the
  outcome (discarded, arrived or completed), the Matches added (none when they
  were replaced) and those removed, pointing into its own bitmaps for the
  length of that call. The Displayed Lines apply it in one call. The two
  getters that were valid only while the state change was being reported, and
  the switch over the Search's phase in the log filtered data, are gone; no
  bitmap is copied and the callback runs on the thread the state change
  always did (#400).
- **The Displayed Lines keep the length of every Mark**: A Mark is added with
  the length of its Log Line, and when Log Lines change from some line on the
  Displayed Lines read the lengths of the Marks from there again through a
  length function they are handed. The separate Mark length structure the log
  filtered data kept in step by hand in every Mark operation is gone; the
  Filtered View is as wide as before (#401).
- **The FileWatcher runs clean under TSan**: The Watch Policy's polling
  half reaches the poll thread through a queued signal wired once, before
  that thread starts, instead of a lambda queued per change. TSan reported
  the lambda, built on the UI thread and run on the poll thread, because it
  cannot see Qt's event queue hand it over; nothing it carried was ever
  written again, so it was no race, and the FileWatcher itests now run
  clean under TSan without a suppression. Behaviour is unchanged (#409).
- **Index jobs as values**: The log data hands its worker one index job as a
  value -- Attach, Full, Partial or Check, the `IndexJob` variant the job
  rule already decides over -- and the worker runs it with `run()`. The four
  job classes that only called the worker method of the same name are gone,
  and the worker's finished notifications reach the log data without slots
  that only sent them again. Behaviour is unchanged (#397).
- **The Load Rule**: What a load, a change on disk and a reload mean for an
  Open Log File -- only Log Lines added, the Marks cleared, the Log Format
  recognized again, the Marks saved with the Session applied, a Search
  waiting for the first load run -- is decided by one Qt-free class,
  `LoadRule`, which holds every flag the Open Log File kept for it and asks
  the Search's auto-refresh whether a Search continues or starts again. The
  Open Log File carries out its decisions; behaviour is unchanged, and two
  oddities are kept as commented table rows: a check that finds the Log File
  unchanged reports it grew, and a load with no Log Lines leaves Format
  Recognition to the next one. Table tests cover the sequences without a Log
  File, a thread or an event loop (#396).
- **The Search Line is a model without widgets**: How adding a word to the
  Search, excluding one, replacing the Search or combining Predefined Filters
  edits the pattern, that excluding switches the logical combination on, and
  what the line says about the Search that runs (progress with its plural and
  gauge, the Matches found, a truncated Log File, an error in the expression,
  the Search / Stop / Clear buttons) moved out of the Crawler Widget into
  `SearchLine`, in a library of its own that the
  `searchline_no_qt_widgets` check keeps free of Qt Widgets. The Crawler
  Widget hands it every event and mirrors its flags, pattern and display;
  the search history, the Theme's palettes and refreshing the views stay in
  the widget. Its texts keep the `CrawlerWidget` translation context, so the
  translations still match. Table tests cover it without a widget (#399).
- **A TSan baseline**: `cmake/tsan.supp` suppresses the findings a
  `-DENABLE_SANITIZER_THREAD=ON` build reports in code TSan cannot instrument
  (oneTBB's flow graph, and a `QThreadPoolThread::run()` finding on
  `LogFilteredDataWorker::search()`'s `shared_ptr<const RegularExpression>`
  judged safe rather than just suppressed); `ctest` picks it up automatically
  and BUILD.md documents it for a standalone TSan run. See
  `docs/adr/0007-tsan-suppresses-onetbb-and-uninstrumented-qt-internals.md`
  (#347).
- **GUI e2e tests run isolated on Windows too**: The isolated-instance test
  harness no longer skips on Windows. `APPDATA`/`LOCALAPPDATA`/`TEMP`/`TMP`
  point into the instance's own temp directory, and the single-instance
  named pipe is scoped per instance, so a test run cannot touch a real
  install's settings, Session, cache or plugins, and cannot reach (or be
  reached by) a LogSquirl the user is actually running. `e2e-windows` now
  runs the ~13 previously-skipped GUI tests instead of skipping them (#348).
- **Smaller indexing parse blocks**: The blocks indexing reads and parses a
  Log File in are now 1 MiB, down from 5 MiB, so a 16 MiB read buffer keeps
  about 16 of them in flight instead of 3 and more cores parse in parallel.
  The 5 MiB size stays as the encoding-detection sample, the header and tail
  digest and the Index Cache resume check, unaffected by the parse block
  size and unchanged in what they read, so a cached Index still resumes.
  Measured on the indexing benchmark (1 GB generated Log Files,
  `RelWithDebInfo`, 10 samples each, before and after run back to back
  under the same otherwise-idle machine): short lines 109.5 ms -> 99.8 ms
  (~9% faster), tabs and long lines 163.3 ms -> 114.2 ms (~30% faster)
  (#339).
- **Sentry release job**: Without a Sentry token the release's Sentry job
  skips its steps and stays green; with one, a failing upload shows as a red
  job (#228).
- **CI hardening**: The install-check containers are digest-pinned and kept
  current by Renovate, a weekly GHCR Cleanup workflow deletes build image
  versions no CI run uses, and the website deploy verifies the FTPS server
  certificate (#230).


- **Plugin Catalog and Plugin Host**: The former plugin manager class is split
  in two. `PluginCatalog` finds the installed plugins from their manifests
  without loading a library and needs Qt Core only; `PluginHost` loads the
  enabled plugins from the catalog and serves their host callbacks. The plugin
  ABI is unchanged, so published plugins work as before.

- **Settings Policies for the widget layer**: A Presentation Policy, a
  QuickFind Policy and a Decoration Policy declare the settings the
  Presentations, the Filtered Views and QuickFind read, and reach the views
  of every open Log File. The Mark and Match colors and the Line Decorator's
  setup are defined once for both Presentations. A build-time check fails
  when a widget file outside a short, justified allowlist reads the settings
  store directly.
- **One declaration per setting**: Each stored setting is declared once with
  its key and default, and that list drives defaults, loading and saving.
  Setting keys are unchanged, so existing settings files need no migration.
- **Table View and Log Format Catalog**: The Table View is its own widget
  behind one Presentation interface shared with the Text View, and one
  builder creates the context menu for both. The Log Format registry becomes
  the Log Format Catalog, and Format Recognition owns its sample size and a
  Recognition Policy.
- **Index Cache owns its rules**: Validity, exclusion, size budget and
  eviction are decided by the Index Cache instead of the indexing run.
  Indexing reads its blocks through its own flow graph, so a pass completes
  even when no worker thread is free.
- **Libraries without Qt Widgets**: Decompression moves into its own
  `logsquirl_compression` library, and the plugin layer shows plugin widgets
  through a Plugin UI Port, so neither links Qt Widgets; build-time checks
  keep it that way. TBB no longer appears in the log data library's public
  headers.
- **Theme screenshots**: A hidden UI test renders every Theme to comparison
  screenshots of the main window, menus, dialogs and a widget gallery.

- **Line Decorator**: Highlighters, Highlighter Sets and QuickFind matching
  moved into their own library without Qt Widgets. One Line Decorator decides
  the colors of every Log Line for both Presentations, and a benchmark covers
  it.
- **Search Session and Displayed Lines**: A Search Session owns the pattern,
  the run in flight, its Matches, its progress and the results cache; each
  run has an identity so a replaced run's results are dropped. Displayed
  Lines own the Marks and Context Lines and are rebuilt only when these
  change. A Search reads its blocks through a small interface that tests can
  fill from memory, and failures are reported as results.
- **Settings Policies**: Indexing, searching, file watching and file access
  are handed Settings Policies instead of reading the settings store, which
  their libraries no longer link. The regex engine is chosen by the caller.
  The Session applies every settings, Highlighter Set and font change to all
  Log Files and windows through one entry point.
- **Text view scrolling**: Where each Log Line is drawn is one Viewport layout
  value, built once and read by painting and hit testing alike. The Scroll
  Position and every scrolling rule live in a library without Qt Widgets, and
  the text view maps between the Filtered View and Log Lines in one place.
- **Open Log File and View Set**: An Open Log File, in its own library,
  decides what growing, truncation and reloading mean for Searches and
  Marks, and hears of file changes through a File Watch Port with a fake for
  tests; `logsquirl_grep` uses it too. A View Set hands fonts, Color Labels,
  Search Limits and Policies to every view of a Log File.
- **Tests**: The text view's painting is compared pixel for pixel against
  golden images, the Table View's cell hit test is checked against what it
  paints, the Index Cache is built with its directory, and the sanitizer job
  gates merges.

## Documentation

- **README refresh**: Add a branded introduction, prominent download links, a
  short getting-started workflow, and concise, benefit-focused feature sections
  while preserving the project's origin statement and attribution. Replace the
  outdated screenshot with a fresh macOS capture using a fictional incident log,
  with the personal file path removed from the image.

- **Domain glossary and decision records**: `CONTEXT.md` defines the terms the
  code and issues use (Log File, Search, Filtered View, Mark, Highlighter,
  Presentation, View Set and more), and `docs/adr/` records architecture
  decisions, such as why scrolling counts Log Lines when text wraps.

---

# v26.07.0 (2026-07-13)

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
 - **Release binaries now carry the correct version number**: The CI version
   helper (`.github/actions/logsquirl-version`) hard-coded the base version
   `26.03.0`, so every release — regardless of its Git tag — attached binaries
   reporting `26.03.0.<build>`. For example, release `v26.06.1` shipped
   artifacts versioned `26.03.0.0740`. The action now derives `YY.MM.PATCH`
   from the release tag (falling back to the `CMakeLists.txt` project version
   for untagged builds), so binaries match the published release
   (e.g. `v26.06.1` → `26.06.1.<build>`). See `BUILD.md` → "Version Numbering".
 - **Update notifications now fire after a stable release**: When publishing a
   stable release, the CI pipeline updated the unused `stable_version` field in
   `latest.json` instead of the `stable` field the application actually reads,
   so stable users were never notified of new stable versions. The pipeline now
   updates `stable` (and keeps `stable_version` in sync).

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
