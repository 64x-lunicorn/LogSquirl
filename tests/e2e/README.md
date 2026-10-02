# LogSquirl E2E Tests

End-to-end integration and performance regression tests for LogSquirl.

## Prerequisites

- Python ≥ 3.10
- LogSquirl built locally (binaries in `build/output/`)
- Test data files present in `test_data/`

## Setup

```bash
cd tests/e2e
pip install -e .
```

This installs `pytest` (≥ 7.0) as the only dependency.

## Running Tests

```bash
# Run all tests
pytest -v --binary-dir=../../build/output

# Run only grep tests
pytest -v --binary-dir=../../build/output -k "grep"

# Run only GUI smoke tests
pytest -v --binary-dir=../../build/output -k "gui"

# Run only performance benchmarks
pytest -v --binary-dir=../../build/output -m performance

# Skip performance tests
pytest -v --binary-dir=../../build/output -m "not performance"

# Skip slow tests (generated large files, the benchmark mode tests)
pytest -v --binary-dir=../../build/output -m "not slow"
```

If `--binary-dir` is omitted, the tests look for binaries in `../../build/output` relative to
the repository root.

## Performance Baselines

Performance tests compare measured times against `baseline.json`. The "Safari Rule"
applies: **LogSquirl must never get slower.** A 5% tolerance is allowed, and a benchmark must
also be slower by more than its absolute margin: a grep case on 1 MB takes about a
millisecond, and 5 % of that is the scheduler. The margin scales with the benchmark (#705,
ADR 0018): half its baseline median, at most 1 ms (`_meta.min_delta_seconds`), at least 1 µs,
or three interquartile ranges of its baseline runs when that is more. So a read of 0.1 µs
that takes 60 µs fails, which a margin of 1 ms hid. A `min_delta_seconds` beside a benchmark's
entry sets its own margin, and `--update-baseline` keeps it. The rule is
`.github/scripts/perf_margin.py`, shared with the Performance workflow.

How fast LogSquirl must be, not only that it is not slower, is the Budgets of ADR 0018 in
`budgets.json`: one per Benchmark Scenario and Log File size, on the CI runners, checked with
`.github/scripts/perf-budgets.py check --budgets budgets.json --e2e benchmark_report.json`.
While the file's `status` is `proposed`, a broken Budget is reported and fails nothing; the
maintainer accepts the Budgets by setting it to `accepted`.

### What the numbers are

Every number but one is timed by LogSquirl itself at the event a user waits for (#667), never
around a process and never with a fixed wait:

- **GUI cases** (`gui_open_*`) run the benchmark mode's `open-and-index` scenario
  (`benchmark_mode.py`, BUILD.md "Benchmark mode") and report two benchmarks per Log File:
  `_first_line`, the open to the first Log Line displayed, and `_indexed`, the open to the
  Index finished. The process startup and the window come before the open and are not in them.
- **Search cases** (`gui_search_*`, #668) run the `search` scenario: the Log File is opened and
  loaded first, unmeasured, then one Search runs as the Search Line runs it. `_first_match` is
  the request to the first Match displayed in the Filtered View, `_finished` the request to the
  Search complete (its MB/s is the Search's). Five Searches per generated Log File
  (`SEARCH_VARIANTS` in `benchmark_mode.py`): plain text, a regular expression, no Match
  (`_finished` only), case ignored, an alternation. Every run must report the Matches counted
  without LogSquirl (`SearchVariant.known_match_count()`), or it fails.
- **QuickFind cases** (`gui_quickfind_*`, #668) run the `quickfind` scenario: on the loaded Log
  File, QuickFind is opened and `slow response` typed a character at a time, at most ten a
  second; each keystroke is timed until the first paint of the Text View after it, the one that
  marks the Matches on screen. `_keystroke_p50` and `_keystroke_p99` are a run's median and
  99th percentile keystroke.
- **Scroll cases** (`gui_scroll_*`, #669) run the `scroll` scenario: on the loaded Log File, the
  view's vertical scroll bar steps 200 lines down, then 40 pages, then jumps to the end, each
  step once the one before was painted. Every paint of the view's Viewport meanwhile is a frame,
  timed from the paint event reaching the Viewport to its handler's return; `_frame_p50`,
  `_frame_p99` and `_frame_max` are a run's median, 99th percentile and longest frame, and
  `_frame_p99` carries the frames over budget (longer than 16.7 ms, one frame at 60 Hz) of
  each run, which the report lists under *Frames Over Budget* and never compares. The Text View
  and the Table View scroll the same Log Lines, with and without a Highlighter Set of five; the
  Text View also scrolls them with ANSI color sequences, hidden and shown as colors. Each run
  paints hundreds of frames, so a case takes at most 7 runs after one warmup run.
- **Follow cases** (`gui_follow_*`, #670) run the `follow` scenario: it writes a Log File of its
  own in the run's directory, opens and follows it with a chart of one series shown, then a
  writer thread of the scenario appends Log Lines at a fixed rate for 5 s, 10 or 1000 a second,
  reading the clock just before each append. A Log Line is displayed by the first paint of the
  Text View that shows the Log File through it, and charted by the first paint of the chart
  whose points reach it. `_display_p50` and `_display_p99` are a run's median and 99th
  percentile append to display, `_chart_p99` its 99th percentile append to chart. `_chart_p99`
  also says whether the chart kept up in each run -- charted every Log Line no more than 1 s
  after the Text View displayed it -- which the report lists under *Chart Following* and never
  compares. The writer lives in the application, not in the suite: its moments and the paints
  are read from one clock, and a Python writer would add its own scheduling. Both latencies
  contain the file watch's (the native one by default): on macOS that is most of them.
- **Session restore cases** (`gui_session_restore_*`, #670) run the `session-restore` scenario:
  it generates a Session of one tab per Log File, each with 10 Marks, in the run's own data
  location (never the instance's, which `test_benchmark_mode.py` checks), and restores it as a
  start does. `_current_tab_usable` is the restore to the end of the first paint of the tab in
  front that shows Log Lines after its Index finished; the window being built is part of it.
  `_all_tabs_indexed` is the restore to the last tab's Index finished; restored tabs load one
  after another behind the tab in front (#300). The Session keeps no Kept Searches yet (#704),
  so no Search runs.
- **Grep cases** (`grep_*`) run `logsquirl_grep --benchmark-output <file>`, which writes a
  report of the same format for its Search (scenario `grep`): `index_finished`,
  `search_finished` and `matches_written`, timed from the open of the Log File. A case reports
  the open to `matches_written`, so its MB/s is the Search's, startup excluded.
- **Startup** stays a case of its own: `gui_startup_version` is the wall-clock of
  `logsquirl --version`, the only case timed around a whole process.

The report's *What Is Measured* table says this per benchmark.

### First Run (Establishing Baselines)

A benchmark without a baseline value is skipped, with the measured value in the skip reason:
it was not compared, so it does not pass. Create an initial baseline:

```bash
pytest -v -m performance --update-baseline
```

This writes measured timings into `baseline.json`. Commit the updated file.

### Updating Baselines

After intentional performance changes (optimizations), update the baseline:

```bash
pytest -v -m performance --update-baseline
```

Review the diff in `baseline.json` before committing — values should only decrease.

### Benchmark Configuration

| Option              | Default | Description                                      |
|---------------------|---------|--------------------------------------------------|
| `--bench-runs`      | 21      | Number of measured runs per benchmark             |
| `--bench-warmup`    | 3       | Number of warmup runs (discarded)                 |
| `--bench-report`    | markdown| Report format: `markdown`, `json`, or `none`     |
| `--update-baseline` | off     | Write measured values to baseline.json            |
| `--no-baseline-compare` | off | Measure and report only; the nightly Performance workflow compares with its run history instead (BUILD.md, *Nightly performance*) |

Example with custom run count:

```bash
pytest -v -m performance --bench-runs=21 --bench-warmup=3 --bench-report=markdown
```

### Statistical Methodology

- **Outlier filtering:** IQR × 1.5 — runs outside [Q1 − 1.5×IQR, Q3 + 1.5×IQR] are discarded
- **Regression detection:** Median must exceed baseline + tolerance (5%) and the absolute
  margin above, AND Welch's t-test must confirm statistical significance (p < 0.05). Both
  conditions are required.
- **Stability monitoring:** Coefficient of variation (CV%) is computed for each benchmark.
  CV > 15% triggers a warning that the result may be unreliable.

### Benchmark Report

After each performance run a `benchmark_report.md` (or `.json`) is generated in `tests/e2e/`.
It includes:

- **Summary table:** median, mean ± std, CV%, percentiles, delta vs baseline
- **Throughput:** MB/s and lines/sec for file-based benchmarks
- **Stability analysis:** CV%, IQR, outlier count, verdict (Stable/Acceptable/Noisy/Unstable)
- **Environment:** OS, CPU, cores, RAM, Python version

### Large File Benchmarks

Some benchmarks use generated files that must be written before first use: 10, 50 and 100 MB
of the 1 MB random block (a single Log Line of 1 MB, repeated), 100 MB and 1 GB Log Files of
ordinary Log Lines of about 80 bytes, one in 101 an `ERROR`, and the Log Files the scroll cases
scroll through: `generated_scroll_100Mb.log`, the same Log Lines with the class that logged them,
as a Java logger writes them, which a built-in Log Format recognizes (so they have a Table
View), and `generated_scroll_ansi_20Mb.log`, those Log Lines with ANSI color sequences around
the level, the thread and the class:

```bash
python tests/e2e/generate_test_data.py              # everything, as CI does
python tests/e2e/generate_test_data.py --max-mb 100 # without the 1 GB Log File
```

These files are in `.gitignore` and never committed. Tests that need them skip if the files are
missing, and the report lists them under *Not Measured in This Run*. The Benchmarks and
Performance workflows generate all of them, so they measure every case; the 1 GB cases take at
most 7 runs after one warmup run. To include large file benchmarks:

```bash
pytest -v -m performance                        # all benchmarks (including large if available)
pytest -v -m "performance and not slow"          # quick benchmarks only (1-1.5 MB)
```

### Benchmarks

| Name | Measures | Log File |
|------|----------|----------|
| `grep_1mb_simple` | Simple pattern search | 1 MB random block |
| `grep_1mb_regex` | Complex regex search | 1 MB random block |
| `grep_1_5mb_simple` | Simple pattern search | 1.5 MB random block |
| `grep_utf16_1mb` | UTF-16LE encoding overhead | 1 MB UTF-16LE |
| `grep_1mb_no_match` | No-match scan-only overhead | 1 MB random block |
| `grep_1mb_alternation` | Regex alternation (ERROR\|WARNING\|CRITICAL) | 1 MB random block |
| `grep_1mb_case_insensitive` | Case-insensitive regex | 1 MB random block |
| `grep_10mb_simple` | Simple pattern, 1 MB Log Lines | 10 MB, generated |
| `grep_10mb_regex` | Complex regex, 1 MB Log Lines | 10 MB, generated |
| `grep_50mb_simple` | Simple pattern, 1 MB Log Lines | 50 MB, generated |
| `grep_100mb_simple` | Simple pattern, 1 MB Log Lines | 100 MB, generated |
| `grep_utf16_10mb` | UTF-16LE encoding at scale | 10 MB UTF-16LE, generated |
| `grep_log_100mb_simple` | The `ERROR` Log Lines | 100 MB Log File, generated |
| `grep_log_100mb_regex` | A regex over every Log Line | 100 MB Log File, generated |
| `grep_log_1gb_simple` | The `ERROR` Log Lines | 1 GB Log File, generated |
| `gui_open_1mb_first_line` / `_indexed` | Open to first Log Line displayed / Index finished | 1 MB random block |
| `gui_open_log_100mb_first_line` / `_indexed` | Open to first Log Line displayed / Index finished | 100 MB Log File, generated |
| `gui_open_log_1gb_first_line` / `_indexed` | Open to first Log Line displayed / Index finished | 1 GB Log File, generated |
| `gui_search_log_100mb_<variant>_first_match` / `_finished` | Search requested to first Match displayed / Search finished; variants `plain`, `regex`, `no_match` (`_finished` only), `case_insensitive`, `alternation` | 100 MB Log File, generated |
| `gui_search_log_1gb_<variant>_first_match` / `_finished` | The same | 1 GB Log File, generated |
| `gui_quickfind_log_100mb_keystroke_p50` / `_p99` | QuickFind typed character by character: keystroke to Matches on screen marked, median / 99th percentile of a run | 100 MB Log File, generated |
| `gui_quickfind_log_1gb_keystroke_p50` / `_p99` | The same | 1 GB Log File, generated |
| `gui_scroll_<view>_frame_p50` / `_p99` / `_max` | A frame, the paint of the view's Viewport, while scrolling by line, by page and to the end: median / 99th percentile / longest of a run; `_frame_p99` also the frames over 16.7 ms. Views `text`, `text_highlighters`, `table`, `table_highlighters` | 100 MB scroll Log File, generated |
| `gui_scroll_text_ansi_hidden_frame_*` / `gui_scroll_text_ansi_colors_frame_*` | The same in the Text View, ANSI color sequences hidden / shown as colors | 20 MB scroll Log File with ANSI colors, generated |
| `gui_follow_10_per_s_display_p50` / `_display_p99` / `_chart_p99` | A Log File growing by 10 Log Lines a second for 5 s, followed with a chart: append to displayed in the Text View, median / 99th percentile of a run; append to charted, 99th percentile; `_chart_p99` also whether the chart kept up | Written by the scenario |
| `gui_follow_1000_per_s_*` | The same at 1000 Log Lines a second | Written by the scenario |
| `gui_session_restore_small_current_tab_usable` / `_all_tabs_indexed` | A Session of 3 tabs restored: to the tab in front indexed and painted / to every tab indexed | 1 MB (in front), 1.5 MB and 512 KB random blocks |
| `gui_session_restore_log_220mb_*` | The same | 20 MB scroll Log File with ANSI colors, 100 MB Log File (in front), 100 MB scroll Log File, generated |
| `gui_read_while_indexing_log_100mb_<read>_p50` / `_p99` / `_max` | While the Log File is indexed, the UI thread reads every 2 ms: `nb_line` (`getNbLine`), `line_string` (`getLineString` of one Log Line), `expanded_lines` (`getExpandedLines` of 60 Log Lines); median / 99th percentile / longest read of a run | 100 MB Log File, generated |
| `gui_read_while_indexing_log_100mb_index_wall` / `_index_cpu` | The same run's indexing, from the request to open the Log File to its Index finished: wall time / the process's CPU time, every thread together; `_index_wall` also the parallelism, CPU over wall time | 100 MB Log File, generated |
| `gui_read_while_indexing_log_1gb_*` | The same | 1 GB Log File, generated |
| `gui_startup_version` | Process start to exit of `logsquirl --version` | — |

Every grep case is timed from the open of the Log File to its last match written, every
`gui_open_*` case from the open, every Search case from its request, every QuickFind case
from each keystroke, every scroll case is a paint, every follow case is timed from an append
every session restore case from the restore and every read-while-indexing case is a read or
the indexing from the open; none contains the process startup.

The read-while-indexing cases see what the instruction counts cannot (#686): Cachegrind runs
one thread at a time, so a read that waits for the index lock costs no instruction more, and
neither does indexing that falls back to one thread. Here the first shows as longer reads, the
second as a longer `_index_wall` and a parallelism near 1, which the report lists under
*Indexing Parallelism* and never compares.

### Renamed benchmarks (#667)

The suite used to time whole processes: the grep cases were `grep_search_*` and mostly
measured the startup (a 1 MB case took 0.33 s, 0.27 s of it startup), and `gui_load_1mb` was
a start, a 2 s sleep and a SIGTERM. Their numbers mean something else now, so they have new
names. The Performance workflow treats a benchmark the previous run had and this run has not
as a failure: the first run after the rename is dispatched with `accept_new_level`, which starts
the history of the new names (BUILD.md, *Nightly performance*).

### Adding a benchmark

A case on another Log File is a row in `GREP_CASES` or `GUI_OPEN_CASES` in
`test_performance.py`. A new scenario of the benchmark mode gets a table and a test of its own:
the test turns one run's report into `{benchmark name: seconds}` -- use
`seconds_since_scenario_start(report, event)` -- and hands that function to
`measure_events()`, which keeps the statistics of each name; `_record()` compares and reports
them. Add the names to `all_benchmark_names()` and a slot in `baseline.json`. A scenario a
binary may not have yet -- the before side of the Benchmarks workflow runs its own commit's --
is run with `run_known_scenario()`, which raises `ScenarioUnknown` for the test to skip on.
`SEARCH_CASES`, `QUICKFIND_CASES`, `SCROLL_CASES`, `FOLLOW_CASES`, `SESSION_RESTORE_CASES`
and `READ_WHILE_INDEXING_CASES` are examples.

## Test Structure

```
tests/e2e/
├── conftest.py              # Fixtures, helpers, statistics, CLI options, report generation
├── baseline.json            # Performance baseline data (schema v2)
├── generate_test_data.py    # Large test file generator (10/50/100 MB, 100 MB and 1 GB Log Files, scroll Log Files)
├── pyproject.toml           # Python project config
├── README.md                # This file
├── test_grep_search.py      # Basic search functionality (7 tests)
├── test_grep_encoding.py    # Encoding handling (9 tests)
├── test_grep_edge_cases.py  # Edge cases and error handling (10 tests)
├── test_gui_smoke.py        # GUI smoke tests (6 tests)
├── test_heavy_tabs.py       # Heavy tabs crash test
├── test_user_data_untouched.py # The suite leaves the user's own LogSquirl alone
├── isolated_instance.py     # Starts LogSquirl with its own settings, Session, cache and plugins
├── benchmark_mode.py        # Runs the benchmark mode (`--benchmark`) and logsquirl_grep's report
├── test_benchmark_mode.py   # The benchmark mode and logsquirl_grep report their events
├── test_measurement.py      # How runs become the reported statistics (no binaries needed)
└── test_performance.py      # Performance regression tests (see "Benchmarks")
```

## Starting the application

No test starts the application directly (#328). Run on a developer machine the
suite would otherwise overwrite that developer's own settings and Session --
one smoke test starts with `-n`, which clears inactive window sessions -- and
read and write their real plugin directory.

Every test that starts LogSquirl takes the `isolated_gui` fixture instead (or
`isolated_gui_module`, one environment for a whole module, for the
benchmarks). It hands out an `IsolatedLogSquirl` from `isolated_instance.py`,
which gives the instance a temporary directory of its own for settings, the
Session, the cache, Log Formats, plugins, plugin configuration and the
single-instance lock, and runs it offscreen. Its methods:

| Method | For |
|--------|-----|
| `run(*args)` | an instance that exits by itself (`--version`, `--help`) |
| `start_and_terminate(*args, settle=)` | start, let it settle, SIGTERM, report how it ended |
| `start_primary(*args)` | a primary instance, waited for until its event loop runs |
| `launch(*args)` | a bare `Popen`, for secondary instances |

`test_user_data_untouched.py` is the guard: it records the user's own
LogSquirl locations, runs an isolated instance and checks that not one byte
there changed.

Windows is not covered: its named pipes are not scoped by a directory, so a
test instance could hand its Log Files over to the user's LogSquirl. The
fixture skips there, and so do the tests that start the application.

## Adding New Tests

1. Create a test function in the appropriate file (or add a new `test_*.py` file).
2. Use fixtures from `conftest.py`: `logsquirl_grep_binary`, `logsquirl_binary`, `test_data_dir`.
3. For grep tests, use `run_grep()`; its `stdout` carries only the matched log lines, its
   `stderr` the tool's own log messages (#327). `grep_output_lines()` splits stdout into
   lines and stays as a safety net against stray log output.
4. For GUI tests, take the `isolated_gui` fixture and use its `run()` for short-lived
   commands or `start_and_terminate()` for startup tests. Never start the binary
   directly: see "Starting the application" above.
5. For performance tests, see "Adding a benchmark" above: time events with `measure_events()`;
   `measure_execution()` (wall-clock around a call) is for the startup case only.
6. A case on a generated file is `slow`; `GREP_CASES` and `GUI_OPEN_CASES` mark it from `generated=True`.

## Developer Workflow

Every code change must pass the full E2E suite **before** opening a pull request.

### Before you start coding

```bash
# Make sure the test suite is green on the current branch
cd tests/e2e
source .venv/bin/activate
pytest -v --binary-dir=../../build/output
```

### After making changes

```bash
# 1. Rebuild the project
cd <repo_root>
cmake --build build --parallel

# 2. Run C++ unit tests
cd build && ctest --verbose --output-on-failure && cd ..

# 3. Run the full E2E suite
cd tests/e2e
source .venv/bin/activate
pytest -v --binary-dir=../../build/output
```

- **All tests must pass** before you push.
- If performance tests fail, your change introduced a regression. Profile and fix it.
- If you intentionally improved performance, update the baseline and commit it with your PR:
  ```bash
  pytest -m performance --update-baseline
  git add baseline.json
  ```
- If you added a new binary feature, add a corresponding E2E test in the appropriate file.

### Quick reference

| What you changed       | What to run                          |
|------------------------|--------------------------------------|
| Grep/search logic      | `pytest -k grep`                     |
| Encoding handling      | `pytest -k encoding`                 |
| GUI / UI code          | `pytest -k gui`                      |
| Performance-sensitive   | `pytest -m performance`              |
| Quick perf check       | `pytest -m "performance and not slow"` |
| Everything             | `pytest -v`                          |

## CI Integration

These tests can be added to the CI pipeline by adding a step after the build:

```yaml
- name: Run E2E tests
  run: |
    pip install -e tests/e2e
    pytest tests/e2e -v --binary-dir=build/output
```
