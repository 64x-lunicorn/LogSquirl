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
  Find reads today.

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
