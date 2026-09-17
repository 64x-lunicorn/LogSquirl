# Decoration path micro-benchmarks

Micro-benchmarks for `LineDecorator::verdictFor` and `LineDecorator::decorate`
(`src/highlighting/include/linedecorator.h`), the hot path that runs for
every visible Log Line on every scroll, on files of millions of lines.

This is deliberately separate from the `tests/e2e` performance suite: the
E2E baseline measures the `logsquirl_grep` binary, process startup and file
loading, none of which touch colour composition. This benchmark produces
the number that a future optimisation of the decoration path should be
measured against.

It is cheap to run because the Line Decorator is pure and needs no GUI:
`logsquirl_decoration_benchmark` links only `logsquirl_highlighting` (no
`logsquirl_ui`, no `Qt6::Widgets`) and creates no `QApplication`.

## Building

```bash
cd build_root
cmake --build . --target logsquirl_decoration_benchmark
```

The binary is placed at `build_root/output/logsquirl_decoration_benchmark`,
alongside the other test binaries.

## Running

```bash
./output/logsquirl_decoration_benchmark
```

This runs all cases with Catch2's default benchmarking settings (100
samples). Useful options (see `--help` for the full list):

```bash
# Run only this file's cases (in case other [decoration-benchmark]-tagged
# cases are added elsewhere)
./output/logsquirl_decoration_benchmark "[decoration-benchmark]"

# More samples for a tighter confidence interval, at the cost of runtime
./output/logsquirl_decoration_benchmark --benchmark-samples 200

# Save the run for later comparison
./output/logsquirl_decoration_benchmark > run-before.txt
```

## Comparing two runs

Catch2 2.x has no built-in run-comparison tool (unlike the `tests/e2e`
Python suite, which diffs against `baseline.json` automatically). To
compare before/after an optimisation:

1. Build and run on the base commit, redirecting output to a file:
   `./output/logsquirl_decoration_benchmark > run-before.txt`
2. Make the change, rebuild, and run again:
   `./output/logsquirl_decoration_benchmark > run-after.txt`
3. `diff run-before.txt run-after.txt`, or eyeball the `mean` column per
   benchmark — Catch2 reports mean, low/high mean (95% CI), and std dev for
   each case.

Run on the same machine, ideally otherwise idle, since these are wall-clock
timings with no cross-machine baseline. A regression worth chasing is one
outside the reported confidence interval, not a percent-level wobble.

## Cases

- **common no-match line**: a short, typical log line, nothing matches.
  The baseline every other case should be compared against.
- **very long line**: ~24,000 characters, no match.
- **many highlighters, one whole-line match**: 200 Highlighters in the
  active set, only the last one (a whole-line match) applies — the cost of
  walking the whole set before it matches.
- **many matches on one line**: a single pattern (main search + QuickFind)
  matching 2000 times on one line — the cost of `HighlightedMatchRanges`
  overlap splitting at scale.
- **tab-heavy line**: 200 tab-separated fields.
- **translate to display space: old per-match re-expansion** / **...:
  rawToDisplayColumns**: isolates the raw-to-display column translation
  step from `decorate()` itself, comparing the per-match prefix
  re-expansion issue #80 replaced (`AbstractLogView::drawTextArea` used to
  re-run `untabify()` on the whole prefix for every match) against the
  `rawToDisplayColumns()` mapping built once per line that replaced it.
  Same input for both: the 2000 raw-space matches from "many matches on one
  line". This step lives in the view, not `LineDecorator`, so it isn't
  exercised by any of the other cases above.
- **long line, one early match: old per-match re-expansion** / **...:
  rawToDisplayColumns (limited)**: the case the mapping approach could
  regress if built naively — a ~24,000 character line with a single match
  near the start. The view limits `rawToDisplayColumns()` to the furthest
  raw column any match actually reaches (`QStringView{ logLine }.left(
  furthestRawColumn )`), instead of mapping the whole line for one early
  match; this pair confirms that keeps the cost comparable to the old
  per-match approach instead of regressing to an O(line length) cost paid
  regardless of how few matches there are.

## Recorded baseline

Measured 2026-09-09 on Apple Silicon (macOS, Debug build — release numbers
will be lower across the board; what matters is the relative shape and the
delta on a future re-run on the same machine/config):

| Benchmark                                                    | Mean       |
|-----------------------------------------------------------------|-----------:|
| common no-match line                                          |   100.0 µs |
| very long line                                                |    1.127 ms |
| many highlighters, one whole-line match                       |   31.38 ms |
| many matches on one line                                      |  593.9 ms |
| tab-heavy line                                                 |  569.8 µs |
| translate to display space: old per-match re-expansion         |   76.70 ms |
| translate to display space: rawToDisplayColumns                |  0.4199 ms |
| long line, one early match: old per-match re-expansion         |  1.285 µs |
| long line, one early match: rawToDisplayColumns (limited)      |  2.359 µs |

The "many highlighters" and "many matches on one line" cases are
pathological stress cases, not representative of typical log lines — they
exist to give the next optimisation ticket a concrete, reproducible number
to chase, per this ticket's purpose. They are not currently regarded as
regressions to fix by this ticket.

The translation cases were added by issue #80 (unifying the decoration
coordinate space). On the 2000-match input, the mapping-based translation
is about **155x faster** than the per-match re-expansion it replaced
(76.70 ms → 0.42 ms). On the one-early-match input, the mapping approach
is about 1 µs slower than the old approach (2.36 µs vs 1.29 µs) — both
values are negligible in absolute terms (well under a millisecond, for a
line drawn at most a few dozen times per repaint), and this is what
motivated limiting `rawToDisplayColumns()` to the furthest raw column
actually needed rather than always mapping the whole line: an earlier,
unlimited version of this change cost the full ~1 ms "very long line"
`decorate()` price *again*, per line, for even a single early match. The
first five cases are unaffected by #80 (within noise of the numbers
recorded above) — that change lives entirely in the view's translation
step, not in `LineDecorator`.

# Scrolling benchmarks

Two binaries measure scrolling a text view on a million generated Log Lines
with text wrapping on (`generated_log_lines.h`: 40 to about 600 characters,
so a Log Line wraps into one Visual Line or several).

- `logsquirl_textviewscrolling_benchmark` runs the scrolling rules alone
  (`src/textviewscrolling`, #246), without a widget: wheel notches, pages,
  the scrollbar dragged and moved to its maximum, a Log Line appended, and
  the width changed.
- `logsquirl_textview_scroll_benchmark` drives a shown text view through Qt
  events on the offscreen platform: the wheel with and without painting,
  pages, the scrollbar dragged, `updateData()` after a Log Line was
  appended, and a resize with painting. It uses only what the text view
  offered before #246, so the same file measures the code before and after.
  Its `[textview-refresh-benchmark]` cases (#295) repaint the view after a
  change of Decoration only: QuickFind typed keystroke by keystroke, the
  Search pattern and the Search Limits changed. "Log Lines read again"
  repaints after `updateData()`, the cost each of them paid before a view
  told a change of Decoration from a change of text.
  Its "one-line scroll" cases (#296) step a view 20 Visual Lines down and up
  one key press at a time, each step painted, with and without text
  wrapping: without it a step moves what was painted and paints the one Log
  Line exposed, with it the Log Lines still in view are not read or
  decorated again.

Both are Catch2 benchmarks; run them in an optimized build, as the Debug
numbers say little about scrolling cost:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-release --target logsquirl_textviewscrolling_benchmark logsquirl_textview_scroll_benchmark
./build-release/output/logsquirl_textview_scroll_benchmark --benchmark-samples 50 > after.txt
```

## Comparing with a commit from before #246

`textview_scroll_benchmark.cpp` and `generated_log_lines.h` build unchanged
on such a commit. In a worktree of it:

```bash
git worktree add ../logsquirl-before origin/master
cp tests/benchmarks/textview_scroll_benchmark.cpp tests/benchmarks/generated_log_lines.h \
   ../logsquirl-before/tests/benchmarks/
cat >> ../logsquirl-before/tests/benchmarks/CMakeLists.txt <<'CMAKE'
add_executable(logsquirl_textview_scroll_benchmark textview_scroll_benchmark.cpp)
target_link_libraries(logsquirl_textview_scroll_benchmark logsquirl_ui Catch2 test_utils)
CMAKE
cmake -S ../logsquirl-before -B ../logsquirl-before/build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build ../logsquirl-before/build-release --target logsquirl_textview_scroll_benchmark
../logsquirl-before/build-release/output/logsquirl_textview_scroll_benchmark --benchmark-samples 50 > before.txt
```

Then compare the `mean` column of `before.txt` and `after.txt` per benchmark,
as described above.

# Log data benchmarks

`logsquirl_logdata_benchmark` measures indexing a Log File and reading its
Log Lines (#275), where most of the work on making LogSquirl faster (#274)
lands. It links `logsquirl_logdata` only and needs no GUI.

It writes its own Log Files at run time into a temporary directory (under
`TMPDIR`), which is removed when the run ends; nothing is checked in. There
are two of them, each about 1 GB:

- **short lines**: about 90 bytes per Log Line, no tabs.
- **tabs and long lines**: tab-separated fields, Log Lines from about 60
  bytes to about 2 KB, and every thousandth one about 20 KB long.

Set `LOGSQUIRL_BENCHMARK_LOG_FILE_MB` to write smaller Log Files, for a quick
run that only checks the benchmark still works. Each case checks that the
Index holds as many Log Lines as were written.

## Cases

Every case runs on both Log Files, tagged `[logdata-benchmark]` and one of:

- `[indexing]` — **whole Log File**: attaching and indexing the Log File,
  without the Index Cache. Building the `LogData` before and destroying it
  after are not measured.
- `[contiguous-read]` — 10,000 Log Lines in a row from the middle of the Log
  File: **getLines, one call** versus **getLineString, line by line**.
- `[sparse-read]` — every hundredth Log Line from the start, 10,000 of them:
  **getLineString, line by line**, as the Filtered View and saving a Search
  result read today, and **getExpandedLineString, line by line**, as Quick
  Find read before #287, each next to the same Log Lines in one sparse read
  (**getLinesSparse**, **getExpandedLinesSparse**, #286), and the same Log
  Lines as UTF-8 (**getUtf8LinesSparse**, #288).
- `[read-while-indexing]` — not a Catch2 `BENCHMARK`: while the Log File is
  indexed, a reader thread asks for the line count, one Log Line
  (**getLineString**) and 60 Log Lines with tabs expanded
  (**getExpandedLines**) about once a millisecond, as a view scrolling
  through it does, and prints the **median, p99 and max latency** of each
  (#289). Scrolling stalls when the max is as long as indexing a block.
  Compare the max of two runs, not their mean.
- `[displayed-lines]` — not on a Log File: 10,000 positions walked from the
  middle of 10 million displayed Log Lines, **lineAtPosition, position by
  position** versus **DisplayedLinesCursor, takeForward** (#286).
- `[tailing]` — following the Log File as it grows (#277), once indexed, with
  and without fast modification detection: **append, check and index the
  appended Log Lines**, one change notification for an append of 20 Log
  Lines, and **check with nothing appended**, a change notification for bytes
  already indexed. Runs last, as it appends to the Log Files.

A change that adds a new way of reading the same Log Lines adds a `BENCHMARK`
next to the one it replaces, over the same `contiguousRange()` or
`sparseLogLines()`, so that both appear in one run.

## Running

Indexing 1 GB takes seconds per sample, so use fewer samples than Catch2's
default 100, and an optimized build:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-release --target logsquirl_logdata_benchmark
./build-release/output/logsquirl_logdata_benchmark --benchmark-samples 10 > after.txt

# Only one kind of case
./build-release/output/logsquirl_logdata_benchmark "[sparse-read]" --benchmark-samples 20

# A quick check on Log Files of 8 MiB
LOGSQUIRL_BENCHMARK_LOG_FILE_MB=8 ./build/output/logsquirl_logdata_benchmark --benchmark-samples 2
```

The Log Files are freshly written when indexing starts, so the operating
system has them in its file cache: the numbers measure indexing and reading,
not the disk. A full run needs about 2 GB of free space in `TMPDIR`.

To compare with a commit from before #275, copy `logdata_benchmark.cpp` and
`generated_log_file.h` into a worktree of it and add the target, as described
above for the text view benchmark:

```bash
git worktree add ../logsquirl-before origin/master
cp tests/benchmarks/logdata_benchmark.cpp tests/benchmarks/generated_log_file.h \
   ../logsquirl-before/tests/benchmarks/
cat >> ../logsquirl-before/tests/benchmarks/CMakeLists.txt <<'CMAKE'
add_executable(logsquirl_logdata_benchmark logdata_benchmark.cpp)
target_link_libraries(logsquirl_logdata_benchmark logsquirl_logdata Catch2 test_utils)
CMAKE
```

# Reading Log Lines benchmark

`logsquirl_logdata_read_benchmark` (#278) loads a Log File of 20,000 Log
Lines, one in ten colored with ANSI color sequences, and measures reading
them the three ways the application does:

- **one Log Line at a time**: 2,000 single-line reads, as Quick Find, the
  Filtered View and saving do.
- **a block of Log Lines**: all 20,000 decoded in one read.
- **a block's UTF-8 view for a Search**: the raw block and the UTF-8 view a
  Search matches against.

Each is measured hiding ANSI color sequences and showing them; the showing
cases are the reference the hiding ones should come close to. Links
`logsquirl_logdata` only. Run it in an optimized build:

```bash
cmake --build build-release --target logsquirl_logdata_read_benchmark
./build-release/output/logsquirl_logdata_read_benchmark --benchmark-samples 50 > after.txt
```

`logdata_read_benchmark.cpp` uses only what log data offered before #278, so
it builds unchanged on such a commit: copy it into a worktree of that commit
as above, with

```cmake
add_executable(logsquirl_logdata_read_benchmark logdata_read_benchmark.cpp)
target_include_directories(logsquirl_logdata_read_benchmark PRIVATE "${CMAKE_SOURCE_DIR}/tests/helpers")
target_link_libraries(logsquirl_logdata_read_benchmark logsquirl_logdata Catch2)
```

# Session restore benchmark

`logsquirl_session_restore_benchmark` restores a Session of 20 tabs as the
application does at startup (#301): it writes 20 small Log Files and a stored
Session of one window with those 20 files (view contexts, custom tab names,
tab groups) into the settings store, then measures

- **build, restore windows and Log Files, add tabs**: the Session is built,
  its window list, geometry and Log Files are restored and every Log File
  gets its tab, named and styled from the tab names and tab groups;
- **add and style the tabs only**: the 20 tabs alone, without opening the
  Log Files, where the settings reads are most of the cost.

A second case restores a Session of several large Log Files (#300), written
at run time into a temporary directory, the last one the current tab, and
measures

- **restore until the current tab has loaded**: the time until the user can
  work in the current tab;
- **restore until every tab has loaded**: the time until every Log File of
  the Session has loaded.

Before #300 every Log File starts loading at once and competes with the
current tab's; after it the current tab's loads first and the others one
after another. There are 4 Log Files of 32 MiB each by default; set
`LOGSQUIRL_BENCHMARK_SESSION_LOG_FILES` and
`LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB` for more or larger ones.

The settings store is the portable one next to the binary, as for the tests,
not the macOS preferences daemon the application uses; what was stored before
is written back at the end. The file uses only what the Session and the tab
area offered before #301, so it builds unchanged on origin/master:

```bash
cmake --build build-release --target logsquirl_session_restore_benchmark
./build-release/output/logsquirl_session_restore_benchmark --benchmark-samples 50 > after.txt
```

For the before side, copy `session_restore_benchmark.cpp` and
`generated_log_file.h` into a worktree of origin/master and add the target as in `CMakeLists.txt` here, as described for
the scrolling benchmarks above.

# Regex matcher benchmark

`logsquirl_regex_matcher_benchmark` (#279) matches a block of 20,000 Log
Lines, one in ten DEBUG, the way a Search does: one matcher for the block.
Each case runs on both regex engines:

- **lookahead**: `^(?!.*DEBUG)`, which Vectorscan rejects, so a Search runs
  it through Vectorscan as a prefilter and confirms each candidate Log Line
  with QRegularExpression.
- **boolean expression of four sub-patterns**: regexes Vectorscan compiles.
- **boolean expression with a lookahead**: three sub-patterns, one of them a
  lookahead, so all of them go through the prefilter.

**three Highlighters, one with a lookahead** creates a matcher for each of
2,000 Log Lines, as a Highlighter Set on origin/master does when it colors a
Log Line.

Links `logsquirl_regex` only. Run it in an optimized build:

```bash
cmake --build build-release --target logsquirl_regex_matcher_benchmark
./build-release/output/logsquirl_regex_matcher_benchmark --benchmark-samples 50 > after.txt
```

`regex_matcher_benchmark.cpp` uses only what the regex module offered before
#279, so it builds unchanged on such a commit: copy it into a worktree of that
commit as above, with

```cmake
add_executable(logsquirl_regex_matcher_benchmark regex_matcher_benchmark.cpp)
target_link_libraries(logsquirl_regex_matcher_benchmark logsquirl_regex Catch2)
```

# Table View paint benchmark

`logsquirl_tableview_paint_benchmark` (#294) shows a Table View of 10,000 Log
Lines with a Log Format of six fields on the offscreen platform, sized so that
exactly 50 Rows are visible, with a Highlighter Set of three whole-line and
three word-only Highlighters active, and measures

- **paint: a viewport of 50 Rows and 6 columns**: the whole viewport repainted;
- **hover: the mouse moves to the next Row and back, each painted**: two mouse
  moves over the viewport, each followed by the repaint it asks for;
- **hit test: a character in the middle and at the end of a 4000 character
  cell**: `LogTableHighlightDelegate::charIndexAtX`, which resolves a click or
  a drag inside a cell to a character.

It uses only what the Table View and its delegate offered before #294, so it
builds unchanged on origin/master:

```bash
cmake --build build-release --target logsquirl_tableview_paint_benchmark
./build-release/output/logsquirl_tableview_paint_benchmark --benchmark-samples 50 > after.txt
```

For the before side, copy `tableview_paint_benchmark.cpp` into a worktree of
origin/master and add the target as in `CMakeLists.txt` here, as described for
the scrolling benchmarks above.

# Displayed Lines benchmark

`logsquirl_displayedlines_benchmark` (#292) measures the Displayed Lines of a
Search with a million Matches over ten million Log Lines (one in ten), 3
Context Lines, everything shown and 100 Marks, some on Matches and some two
Log Lines after one. Links `logsquirl_logdata` only and needs no GUI:

- **progress ticks: 100 batches of 10,000 Matches**: the Matches arriving
  while the Search runs, without its completion. Before #292 each tick
  rebuilt the whole union, so this grew quadratically with the Matches.
- **completion after the progress ticks**: the last batch arrives with the
  completion, which builds the Context Lines around every Match and Mark.
- **continuation over 100,000 appended Log Lines**: after a completed Search,
  the Log File grows and the Search continues over the appended Log Lines in
  10 ticks and a completion.
- **toggling a Mark on and off next to a Match**: after a completed Search.

The file builds on commits from before #292: where the Displayed Lines take
no new Matches, it calls `matchesArrived()` and `searchCompleted()` without
them, as the Filtered View did then. Run it in an optimized build:

```bash
cmake --build build-release --target logsquirl_displayedlines_benchmark
./build-release/output/logsquirl_displayedlines_benchmark --benchmark-samples 20 > after.txt
```

For the before side, copy `displayedlines_benchmark.cpp` into a worktree of
origin/master and add the target, as described for the scrolling benchmarks
above:

```cmake
add_executable(logsquirl_displayedlines_benchmark displayedlines_benchmark.cpp)
target_link_libraries(logsquirl_displayedlines_benchmark logsquirl_logdata Catch2)
```

# QuickFind benchmark

`logsquirl_quickfind_benchmark` (#287) runs a QuickFind for text no Log Line
holds, so it reads and matches every Log Line it searches, over the generated
Log File of short Log Lines described above (about 1 GB, or
`LOGSQUIRL_BENCHMARK_LOG_FILE_MB`). Each case runs forwards from the first Log
Line and backwards from the last one:

- **every Log Line**, as the main view searches;
- **every tenth Log Line**, as a Filtered View searches the Matches of a
  Search.

`[file-kept-open]` runs them on a Log File kept open between reads.
`[file-kept-closed]` runs them with "keep file closed" set; it is hidden, so a
run of every benchmark leaves it out, because before #287 QuickFind reopened
the Log File for every Log Line and a single run takes minutes.

```bash
cmake --build build-release --target logsquirl_quickfind_benchmark
./build-release/output/logsquirl_quickfind_benchmark --benchmark-samples 10 > after.txt
./build-release/output/logsquirl_quickfind_benchmark "[file-kept-closed]" --benchmark-samples 3

# A quick check on a Log File of 8 MiB
LOGSQUIRL_BENCHMARK_LOG_FILE_MB=8 ./build/output/logsquirl_quickfind_benchmark --benchmark-samples 2
```

`quickfind_benchmark.cpp` uses only what QuickFind and LogData offered before
#287. For the before side, copy it and `generated_log_file.h` into a worktree
of origin/master as above, with

```cmake
add_executable(logsquirl_quickfind_benchmark quickfind_benchmark.cpp)
target_link_libraries(logsquirl_quickfind_benchmark logsquirl_ui Catch2 test_utils)
```

# Chart follow benchmark

`logsquirl_chart_follow_benchmark` (#298) charts a generated Log File of about
128 MiB (short lines, `generated_log_file.h`) with three series -- a number on
every Log Line, a count of ERROR Log Lines and a timestamp X-axis bucketed per
second -- then appends Log Lines to it, has the log data load them as a change
on disk does, and calls `ChartPanel::extractData()` after each load, as the
crawler widget does with a visible chart. The first extraction is not measured.

- **append 20 Log Lines, chart updated**: one append, measured until the chart
  holds a point for every Log Line. Before #298 every update extracted the
  whole Log File again, so this grew with its size; afterwards it does not.
- **10 appends of 20 Log Lines in quick succession, chart updated**: a busy
  Log File, the chart asked to update after each load without waiting for it.
  Before #298 each request cancelled the running extraction, waited for it on
  the GUI thread and started over from the first Log Line.

The panel is not shown, so painting the chart is not measured. Where the panel
has an update delay (#298), the benchmark sets it to zero. Set
`LOGSQUIRL_BENCHMARK_LOG_FILE_MB` for another size; a full run needs its size
free in `TMPDIR`. Run it in an optimized build:

```bash
cmake --build build-release --target logsquirl_chart_follow_benchmark
./build-release/output/logsquirl_chart_follow_benchmark --benchmark-samples 10 > after.txt
```

`chart_follow_benchmark.cpp` uses only what the chart panel offered before #298
(the update delay only under `__has_include( "chartextraction.h" )`), so it
builds unchanged on origin/master: copy it and `generated_log_file.h` into a
worktree of that commit as described for the scrolling benchmarks above, with

```cmake
add_executable(logsquirl_chart_follow_benchmark chart_follow_benchmark.cpp)
target_link_libraries(logsquirl_chart_follow_benchmark logsquirl_ui Catch2 test_utils)
```

# Chart paint benchmark

`logsquirl_chart_paint_benchmark` (#299) paints a chart widget of 800 x 400 px
with one series of a point per Log Line (x the line number, values between 0
and 1000) into an image on the offscreen platform, for 10,000, 1 million and 5
million points:

- **paint, all**: fitted to all points, painted again without a change.
- **pan 2 px, paint, all**: the view dragged by 2 px with the right mouse
  button, so nothing plotted before can be reused, and painted.
- **pan 2 px, paint, 1 % of**: the same after zooming in with the mouse wheel
  to about 1 % of the x range.
- **hover, paint, 1 % of**: the mouse moving between two positions 3 px apart,
  each followed by a paint.
- **hover, 1 % of**: the same mouse moves without painting: the hover lookup.

Before #299 the chart stroked one path through every point on every paint and
searched every point on every mouse move, so every case grew with the number
of points; a paint of 10,000 points took seconds. Afterwards the hover cases
do not grow with the points outside the view, and a paint draws at most a few
points per pixel column.

The file uses only what the chart widget offered before #299, so it builds
unchanged on origin/master. There, where `chartplot.h` does not exist, it
measures only 10,000 points: a paint of a million points would take minutes.
`LOGSQUIRL_BENCHMARK_CHART_POINTS` sets one other number of points on either
side. Run it in an optimized build:

```bash
cmake --build build-release --target logsquirl_chart_paint_benchmark
./build-release/output/logsquirl_chart_paint_benchmark --benchmark-samples 20 > after.txt
```

For the before side, copy `chart_paint_benchmark.cpp` into a worktree of
origin/master as described for the scrolling benchmarks above, with

```cmake
add_executable(logsquirl_chart_paint_benchmark chart_paint_benchmark.cpp)
target_link_libraries(logsquirl_chart_paint_benchmark logsquirl_ui Catch2)
```

# Overview and selection benchmark

`logsquirl_overview_selection_benchmark` (#297) measures two things the text
view does with a lot of Log Lines.

`[overview-benchmark]` searches a generated Log File of about 192 MiB (short
lines, `generated_log_file.h`, about two million Log Lines) for every other Log
Line, a million Matches, and draws the overview on 1000 pixel rows:

- **recompute after the Search changed, 1000 rows**: `updateData()` and
  `updateView()`, what every Search progress tick asked for. Before #297 this
  walked every Match; afterwards each row counts its Matches.
- **paint, nothing changed, 10 times**: the overview widget repainted. Before
  #297 every paint copied both line vectors.
- **10 Search ticks, each painted**: both together. While a Search runs the
  crawler widget now also paces these recomputes (200 ms), which this case does
  not use, so that it builds on both sides.

`[selection-benchmark]` selects 100,000 of a million generated Log Lines held in
memory (`generated_log_lines.h`) with a Shift+click and measures **Shift+Down
20 times and Shift+Up 20 times** at its end. Before #297 every step built the
whole selected text to report its length.

```bash
cmake --build build-release --target logsquirl_overview_selection_benchmark
./build-release/output/logsquirl_overview_selection_benchmark --benchmark-samples 10 > after.txt

# A quick check on a Log File of 16 MiB
LOGSQUIRL_BENCHMARK_LOG_FILE_MB=16 ./build/output/logsquirl_overview_selection_benchmark --benchmark-samples 2
```

`overview_selection_benchmark.cpp` uses only what the overview, its widget and
the text view offered before #297, so it builds unchanged on origin/master: copy
it, `generated_log_file.h` and `generated_log_lines.h` into a worktree of that
commit as described for the scrolling benchmarks above, with

```cmake
add_executable(logsquirl_overview_selection_benchmark overview_selection_benchmark.cpp)
target_link_libraries(logsquirl_overview_selection_benchmark logsquirl_ui Catch2 test_utils)
```

# Before and after in CI

The **Benchmarks** workflow (`.github/workflows/benchmarks.yml`, #276) builds a
branch and master in the optimized configuration the Linux packages ship
(RelWithDebInfo with LTO, in the Ubuntu 24.04 build container), runs every
benchmark listed in `CMakeLists.txt` here and the e2e performance suite on both,
on the same runner, and shows a before/after table per benchmark in the job
summary. The raw reports and the comparison as JSON are in the
`benchmark-results` artifact.

```bash
gh workflow run benchmarks.yml -f ref=my-branch            # against master
gh workflow run benchmarks.yml -f ref=my-branch -f base_ref=<tag> -f log_file_mb=1024
```

By default the before side is built with this branch's `tests/benchmarks`, so
both sides run the same benchmark code, as described above for comparing by
hand; a benchmark that does not compile on the before side is only measured
after. The comparison (`.github/scripts/benchmark-compare.py`) also works on
two local runs: put each side's `--reporter xml` output under
`<dir>/catch2/<binary>.xml` and run it with `--before <dir> --after <dir>`.

The workflow can only be dispatched once it is on master.

# Filtered View read benchmark

`logsquirl_filteredview_read_benchmark` (#288) measures the readers of a
Search's Displayed Lines. It writes a Log File of short Log Lines (256 MiB, or
`LOGSQUIRL_BENCHMARK_LOG_FILE_MB`) into a temporary directory, indexes it and
runs a Search matching every eighth Log Line, without Context Lines, before
the first case:

- `[paint]` — **getLines, a screen at each of 200 Scroll Positions**: 60 rows
  read from the Filtered View's log data at 200 positions spread over the
  matches, as painting does.
- `[save]` — **the first 100,000 displayed lines, as UTF-8**: a save through
  the Filtered View's `linesToSave()` into memory.
- `[marks]` — **remove the longest Mark and add it back**, among 10,000
  Marks spread over the Log File.
- `[grep]` — **logsquirl_grep, every eighth Log Line**: the command line
  tool run on the same Log File with its output discarded. It is taken from
  the directory of the benchmark binary and skipped when it is not there.

The file uses only what these offered before #288, so it builds on
origin/master with `generated_log_file.h` copied next to it:

```bash
cmake --build build-release --target logsquirl_filteredview_read_benchmark logsquirl_grep
./build-release/output/logsquirl_filteredview_read_benchmark --benchmark-samples 10 > after.txt

# A quick check on a Log File of 16 MiB
LOGSQUIRL_BENCHMARK_LOG_FILE_MB=16 ./build/output/logsquirl_filteredview_read_benchmark --benchmark-samples 2
```

```cmake
add_executable(logsquirl_filteredview_read_benchmark filteredview_read_benchmark.cpp)
target_link_libraries(logsquirl_filteredview_read_benchmark logsquirl_ui Catch2 test_utils)
```
