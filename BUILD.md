# How to Build LogSquirl

## Overview

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.
Local builds can be faster because code can be optimized for current CPU instead of generic x86-64. Support for SSE4/AVX code paths
will be enabled if available on build machine.

## Getting the Source

This project is [hosted on GitHub](https://github.com/64x-lunicorn/LogSquirl). You can clone this project directly using this command:

```
git clone https://github.com/64x-lunicorn/LogSquirl
```

## Dependencies

To build LogSquirl:

- cmake 3.16 or later to generate build files
- C++ compiler with C++23 support (at least gcc 13, clang 17, msvc 19.36)
- Qt 6.5 or later (CI builds use Qt 6.11.3):
  - QtCore
  - QtGui
  - QtWidgets
  - QtConcurrent
  - QtNetwork
  - QtXml
  - QtTools
  - Qt5Compat

To build Vectorscan regular expressions backend (default on 64-bit):

- CPU with support for [SSSE3](https://en.wikipedia.org/wiki/SSSE3) instructions (for Vectorscan backend; FAT_RUNTIME auto-selects best SIMD path)
- Boost (1.58 or later, header-only part)
- Ragel (6.8 or later; precompiled binary is provided for Windows; has to be installed from package managers on Linux or Homebrew on Mac)

To build installer for Windows:

- nsis to build installer for Windows
- Precompiled OpenSSl library to enable https support on Windows

Building tests:

- QtTest

All other dependencies are provided by [CPM](https://github.com/cpm-cmake/CPM.cmake) during cmake configuration stage (see 3rdparty directory).

CPM will try to find Vectorscan, TBB, uchardet and xxhash installed on build host.
If a library can't be found, the one provided by CPM will be used.

## Building

### Configuration options

By default LogSquirl is built without support for reporting crash dumps. This can be enabled via cmake option `-DLOGSQUIRL_USE_SENTRY=ON`.
Such a build downloads the pinned [minidump-stackwalk](https://github.com/rust-minidump/rust-minidump) release for
the target (Linux x86-64, macOS arm64/x86-64, Windows x64) at configure time, checks its SHA-256
(`cmake/MinidumpStackwalk.cmake`) and ships it next to the app as `logsquirl_minidump_dump`; the crash report dialog
runs it on a pending minidump. `-DLOGSQUIRL_MINIDUMP_STACKWALK=<path>` ships an existing executable instead, for
offline builds or other targets.

LogSquirl uses Vectorscan regular expressions library which requires CPU with SSSE3 support, ragel and boost headers.
LogSquirl can be built with only Qt regular expressions backend by passing `-DLOGSQUIRL_USE_VECTORSCAN=OFF` to cmake.

On Windows, a build with `-DLOGSQUIRL_GENERIC_CPU=ON` (the release) builds Hyperscan twice, as `hs.dll` for SSE4.2
and `hs_avx2.dll` with `/arch:AVX2`, and loads the one the CPU supports at the first Search. Setting the environment
variable `LOGSQUIRL_HYPERSCAN_DISABLE_AVX2=1` makes it load `hs.dll` on a CPU with AVX2 too. Without the option,
Hyperscan is linked statically and built for the build machine's CPU, like the rest of LogSquirl.

Releases are `RelWithDebInfo` builds. LogSquirl optimizes that build type as fully as `Release`
(`-O3` with GCC and Clang, `/Ob2` and a non-incremental `/OPT:REF /OPT:ICF` link with MSVC) and keeps its
debug information for crash reports. Link time optimization is on for every LogSquirl target, not for the
third-party libraries; turn it off with `-DLOGSQUIRL_USE_LTO=OFF`, which makes linking a lot faster during development.

The file types LogSquirl opens are declared once, in `cmake/FileTypes.cmake`, and each platform's packaging is
generated from that list. On Linux, `make install` and the deb and rpm install them: a shared-mime-info package, the
document icon in the hicolor theme and the desktop entry's `MimeType`. `-DLOGSQUIRL_FILE_TYPES=OFF` leaves them out
and builds an application that registers none on Linux, so its *File Associations* page is disabled there; that is
how the AppImage is built: it has no install step that could register them.

LogSquirl links [mimalloc](https://github.com/microsoft/mimalloc) on every platform. By default only LogSquirl's own
containers, roaring and Vectorscan allocate through it; Qt and the standard containers use the system allocator.
On Linux, `-DLOGSQUIRL_MIMALLOC_OVERRIDE=ON` lets mimalloc serve `malloc` and `new` for the whole process, Qt included.
The option is off until measurements decide it per platform (#282), and configuring fails with it on macOS or Windows:
there a statically linked mimalloc does not take over Qt's allocations.

To measure the override, build the same commit twice, once with the option and once without, and compare the
benchmarks (`tests/benchmarks/README.md`) and the e2e performance suite of both builds. To have the **Benchmarks**
workflow do that on one runner, push a throwaway branch whose only commit turns the option's default to `ON` in
`CMakeLists.txt`, and dispatch the workflow from that branch with its parent as `base_ref`:

```bash
gh workflow run benchmarks.yml --ref <override-branch> -f base_ref=<branch without it>
```

### Profile-guided optimization

`-DLOGSQUIRL_PGO=GENERATE` builds instrumented binaries; every run of them records a profile into
`LOGSQUIRL_PGO_DIR` (default `<build dir>/pgo-profile`). `-DLOGSQUIRL_PGO=USE` builds optimized for that profile
(Clang and AppleClang, GCC, MSVC; `cmake/ProfileGuidedOptimization.cmake` says how per compiler). The training
workload is the benchmark mode's scenarios, run by the e2e performance suite. `-DLOGSQUIRL_BOLT=ON` (Linux) links
the executables so that `llvm-bolt` can rewrite them afterwards. All three are off by default, and a release build
uses them only behind its job's `pgo` (and `bolt`) switch in `ci-build.yml`, false until the A/B numbers show a clear
gain on that platform (#682). No profile is checked in: CI builds, trains and uses it in one run.

`.github/scripts/pgo.py` runs each step and records its wall time. On a developer machine (here macOS):

```bash
pgo() { python3 .github/scripts/pgo.py --timings pgo/timings.json "$@"; }
pgo build --build-dir pgo/plain --mode OFF --benchmarks          # the build to compare with
python3 tests/e2e/generate_test_data.py --max-mb 100
pgo build --build-dir pgo/build --mode GENERATE
pgo train --binary-dir pgo/build/output                         # needs .github/requirements/e2e.txt
pgo merge --toolchain clang --profile-dir pgo/build/pgo-profile # gcc: nothing to merge; msvc: pgomgr
pgo build --build-dir pgo/build --mode USE --benchmarks         # GCC: the same build directory
pgo measure --side plain=pgo/plain/output --side pgo=pgo/build/output --results pgo/results
python3 .github/scripts/benchmark-compare.py --before pgo/results/plain --after pgo/results/pgo
pgo times
```

A USE build without its profile stops at configure time and says what to run. A GENERATE build keeps `-Werror` on
its compile and link lines (ADR 0009) like any other; a USE build keeps it for everything but two diagnostics, which
it accepts as warnings: GCC's `-Wmissing-profile`, for code the training never ran, and Clang's `-Wbackend-plugin`
hash mismatch, for an inline function whose copies differ between translation units (GCC's `-Wcoverage-mismatch`
still fails the build). Clang has no group of its own for the hash mismatch, so `pgo.py build --mode USE` fails on
any other `-Wbackend-plugin` diagnostic and logs how many of each accepted one the build printed (ADR 0019). The profile reaches the libraries and
`logsquirl` and `logsquirl_grep`, the executables the training runs; the own sources of the other executables (tests,
micro-benchmarks) compile without it, since Clang would match their `main()` to logsquirl's by name. A USE build does not use a compiler launcher
(sccache): the cache keys an object on the command line, not on the profile it names.

The **PGO** workflow (`.github/workflows/pgo.yml`) produces the numbers per platform, `gh workflow run pgo.yml --ref
<branch>`: plain, PGO, and on Linux PGO and BOLT, each built from scratch on its own runner, then every Catch2
micro-benchmark and the e2e performance suite run on all of them on one runner. The `pgo-report` artifact holds the
A/B tables and the build time each variant took; the build time increase is the instrumented build, the training,
the merge and the optimized build (and BOLT's three steps) against the one plain build they replace.

### Plugin SDK

The plugin C ABI header (`logsquirl_plugin_api.h`) is installed alongside the
application during `cmake --install`. To develop plugins, see
[docs/plugin-sdk.md](docs/plugin-sdk.md) for the complete developer guide.

### Building on Linux

Here is how to build logsquirl on Ubuntu 24.04.

Install dependencies:

```
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev qt6-5compat-dev libboost-all-dev ragel
```

Configure and build logsquirl:

```
cd <path_to_logsquirl_repository_clone>
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
```

**_For Qt 5 builds, replace the qt6 packages above with `qtbase5-dev qttools5-dev`._**

Binaries are placed into `build_root/output`.

See `.github/workflows/ci-build.yml` for more information on build process.

### Building on Windows

Install Microsoft Visual Studio 2022 with C++ support.
Community edition can be downloaded from [Microsoft](https://visualstudio.microsoft.com/vs/).

Install latest Qt 6 version using [online installer](https://www.qt.io/download-qt-installer).
Make sure to select the MSVC 2022 64-bit component.

Install CMake from [Kitware](https://cmake.org/download/).
Use version 3.14 or later for Visual Studio 2022 support.

Download the Boost source code from http://www.boost.org/users/download/.
Extract to some folder. Directory structure should be something like `C:\Boost\boost_1_63_0`.
Then add `BOOST_ROOT` environment variable pointing to main directory of Boost sources so CMake is able to fine it.

Prepare build environment for CMake. Open command prompt window and run:

```
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\vsdevcmd" -arch=x64
```

Next setup Qt paths:

```
<path_to_qt_installation>\bin\qtenv2.bat
```

Then add CMake to PATH:

```
set PATH=<path_to_cmake_bin>:$PATH
```

Configure logsquirl solution:

```
cd <path_to_project_root>
md build_root
cd build_root
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

CMake should generate `logsquirl.sln` file in `<path_to_project_root>\build_root` directory. Open solution and build it.

Binaries are placed into `build_root/output`.

For https network urls support download precompiled OpenSSL 3.x library from https://www.firedaemon.com/firedaemon-openssl.
Put libcrypto-3 and libssl-3 for desired architecture near logsquirl binaries.

### Building on Mac OS

LogSquirl requires macOS 15 (Sequoia) or higher.

Install [Homebrew](https://brew.sh/) using terminal:

```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Homebrew installer should also install xcode command line tools.

Download and install build dependencies:

```
brew install cmake ninja qt boost ragel
```

Usually path to qt installation looks like `/opt/homebrew/opt/qt/lib/cmake/Qt6` (Apple Silicon) or `/usr/local/opt/qt/lib/cmake/Qt6` (Intel).

Configure and build logsquirl:

```
cd <path_to_logsquirl_repository_clone>
mkdir build_root
cd build_root
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQt6_DIR=<path_to_qt_install> ..
cmake --build .
```

Binaries are placed into `build_root/output`.

By default, logsquirl will rely on cmake to figure out target MacOS version. Usually it uses build host version.
To override default cmake value pass an option `-DLOGSQUIRL_OSX_DEPLOYMENT_TARGET=<target>` to cmake during configuration step,
`<target>` is one of `14`, `15`, `16`. LogSquirl's target must be greater or equal to the target used by Qt libraries.

## Code style

Formatting follows the `.clang-format` file at the repository root. CI runs a
"Format" job (clang-format in dry-run mode over every project-owned `.cpp`,
`.h` and `.hpp` file) pinned to **clang-format 23** — the config's
`Standard: c++23` requires that major version. Format locally with a
matching version before pushing:

```bash
pip install --require-hashes -r .github/requirements/clang-format.txt
clang-format -i <file>
```

## Running tests

### C++ unit tests (Catch2)

Tests are built by default. To turn them off pass `-DLOGSQUIRL_BUILD_TESTS=OFF` to cmake.
Tests use Catch2 v3 (fetched and built by CMake, pinned in `3rdparty/CMakeLists.txt`) and require QtTest module. Tests can be run using ctest tool provided by CMake:

```
cd <path_to_logsquirl_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```

Each Catch2 test case is its own ctest test named `<test executable>: <test case>`, so a
single case runs with e.g. `ctest -R "^logsquirl_tests: Scenario: QuickFind"`. The tests
run one after another: the Qt test executables share one portable settings file.

### Theme screenshots

A hidden UI test renders every Theme (Light, Dark, High Contrast, Smyck, Smyck Light) to PNG files:
the main window with a Log File and a Search, the sidebar, every menu, the Command Palette, the
dialogs and a gallery of every standard widget in every state. It is not part of `ctest` or CI; run
it by its tag, offscreen (no display or screen-recording permission needed), into a directory of
your choice:

```
LOGSQUIRL_SCREENSHOT_DIR=/path/to/shots build/output/logsquirl_itests -platform offscreen "[.screenshots]"
```

Each image is named `<view>_<theme>.png`, so the Themes of one view sort together. Render once
before and once after a Theme change into two directories to compare them side by side. The Log
File shown is a copy of `test_data/screenshot_demo.txt` under `/tmp/logsquirl-screenshots`, so no
path of your machine appears in the images.

The website's screenshots come from the same run (#586): `main-window-search`, `main-window-chart`,
`command-palette` and `main-window-dashboard`, each in `smyck` and `smyck-light`, copied to
`website/src/assets/screenshots/` as `search-smyck.png`, `search-smyck-light.png` and so on. The home
page shows the Smyck set in the dark site theme and the Smyck Light set in the light one. To retake
them for a release, run the test from that release's build and copy the eight files. On macOS, keep
the run away from your own settings by pointing Core Foundation and the temporary directory
elsewhere:

```
CFFIXED_USER_HOME=/tmp/shots-home TMPDIR=/tmp/shots-tmp/ LOGSQUIRL_SCREENSHOT_DIR=/tmp/shots \
  build/output/logsquirl_itests -platform offscreen "[.screenshots]"
```

The run also renders the Table View, with Format Recognition on as in the application: before the
Search (`main-window-table-view`) and with it (`main-window-table-view-search`), where the Rows of
the Matches show the Theme's Row color (#590). The website does not show it
yet; the demo log is recognized as spdlog since #589, so its columns are right.

### E2E integration tests (Python / pytest)

End-to-end tests exercise the compiled `logsquirl_grep` and `logsquirl` binaries
against the files in `test_data/`. They cover search correctness, encoding handling,
edge cases, GUI smoke tests, and **performance regression detection** (5 % tolerance locally;
CI checks performance nightly, see *Nightly performance* below).

**Prerequisites:** Python >= 3.10

```bash
# One-time setup (from repository root)
cd tests/e2e
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -e .

# Run all E2E tests (40 tests)
pytest -v --binary-dir=../../build/output

# Run only functional tests (skip performance benchmarks)
pytest -v --binary-dir=../../build/output -m "not performance"

# Run only performance benchmarks
pytest -v --binary-dir=../../build/output -m performance
```

**Performance baselines:** After optimizations, update the baseline with
`pytest -m performance --update-baseline`. Review the diff in `baseline.json`
before committing — values should only go down, never up. `baseline.json` is for
measuring on your own machine; a benchmark it has no entry for is reported as
skipped with the measured value, never as passed. CI does not compare with it.

#### Nightly performance

The **Performance** workflow (`.github/workflows/performance.yml`, #441, #677, #685) measures master every
night at 02:41 UTC on GitHub-hosted `ubuntu-24.04` runners, two ways, side by side:

- **Wall-clock** (job *measure*): it builds master as CI ships it (RelWithDebInfo with LTO, in the
  noble build container), generates the 10, 50 and 100 MB test files and the generated 100 MB and
  1 GB Log Files and runs the whole e2e performance suite (`-m performance`, 21 measured runs per
  benchmark) with `--no-baseline-compare`: every scenario of the benchmark mode. The application
  is started only through the suite's isolated instances, as in every e2e run. A benchmark that
  is skipped fails the run. On the same runner, it then runs the same suite on a **reference
  build**: the last release tag (`vX.Y.Z`, no pre-release) in the commit's history, built in the
  commit's container with the same options (only `logsquirl` and `logsquirl_grep`, in the same
  build directory, so only what differs from the release is recompiled; the binaries are cached
  per tag, container and options, so this happens once per release). A scenario the release does
  not have skips there. Each benchmark measured on both gets the ratio *this commit ÷ reference*,
  which does not depend on the CPU model the runner has.
- **Instruction counts** (job *count*): every Catch2 benchmark of `tests/benchmarks` runs once
  under Callgrind with the scripts of the pull request counts (*Instruction counts* below), but
  on generated Log Files of 32 MiB instead of 4 (`count_log_file_mb`). A count repeats to within
  a fraction of a percent on a shared runner, where a time varies by 5–20 % and more between the
  runners' CPU models.

The *record* job puts each benchmark's value next to its earlier values: the runs since the
latest accepted one, on the same runner CPU model (`system.cpu` of the suite's report; for counts
the model they were taken on, since glibc picks its string functions by CPU) and, for counts,
with Log Files of the same size. Wall-clock medians differ by up to 24 % between CPU models and
by about 1.4 % on one (#675), so a run that lands on a model without enough history only reports;
the job summary names the model and how many earlier runs of it the series has. A **regression is a change point in that series**
(`.github/scripts/perf_changepoint.py`), not one run against a threshold: a run *c* from which on
every run up to the latest is above

    limit(c) = reference + max(tolerance × reference, min_delta, 3 × IQR)

where *reference* is the median and *IQR* the interquartile range of the up to 14 runs before *c*
(for wall-clock, the median IQR within those runs when that is larger), and *min_delta* the
absolute margin of `perf_margin.py` (ADR 0018). The earliest such run is the
change point, the run before it the last good one: the commits between the two are where the
change came from. The reference is the level before the change, so a regression that lasts does
not pull it up and heal itself, and a series that scatters (runners of several CPU models) gets
room for its own scatter.

| Series | Tolerance | min_delta | Lasting | Runs before it | Files an issue |
|---|---|---|---|---|---|
| Instruction counts | the benchmark's threshold of the *Instruction count gate* (+2 % by default) | none | 1 run | 3 | yes |
| Wall-clock medians | 10 % | half the reference, at most 10 ms, at least 1 µs, or the `min_delta_seconds` beside the benchmark's Budget | 2 runs | 6 of the same CPU model | no, the trend only |

With fewer runs before it, a benchmark is reported only. Wall-clock is shown in the job summary as
the trend and files nothing: instruction counts are the gate, and the spread within one CPU model
is known from a single pair of runs so far. **A run whose median within-run CV is above 20 %**
(one run in four had 61 %, #675) is recorded but flagged unusable: its wall-clock is neither
compared nor part of a later run's series, its e2e Budgets are not checked, and the summary says
so; a reference run above 20 % flags only the ratios. Each **Budget** of ADR
0018 (`tests/e2e/budgets.json`, the one place they live) is checked as well
(`perf-budgets.py`): a broken Budget names the runs since it broke. While `status` in
`budgets.json` starts with `proposed`, the Budgets are only shown in the summary and are no
finding; they become one once the maintainer sets it to `accepted` (ADR 0018). A benchmark the previous run
measured and this one did not is a finding too, and so are counts that were not taken at all.

**A finding files an issue.** For a run of master, the *issues* job opens one issue per scenario
(the scenario of the Budget or the benchmark's name; for counts, the benchmark binary), labelled
`needs-triage` and `performance`, with a table of the findings and, for each, the commit range
from the last good run to the first bad one (a compare link and the `git log` to run). The issue
is found again by a marker in its body: while it is open, each night updates its description and
comments when a new finding joins it; closed, it stays closed for the same findings, and a new
change point opens a new one. `.github/scripts/perf-issues.py` decides and writes the texts; the
job only calls `gh issue create/edit/comment` and may write issues and nothing else. The run is red
as well while a finding stands.

The results live on the **`perf-data`** branch, which the workflow creates on its first run and
only ever appends to: `history/<time>-<commit>-<run>.json` per run of master (the statistics of
every benchmark, the instruction counts with the CPU model they were taken on, the commit, the
version, the runner's CPU, the run's median CV and whether it is usable, and the reference
build's tag, statistics and ratios), and `trend.csv` with one row per run: the CPU model, the
median CV, what is unusable (`run` or `reference`), the reference tag, then one column per
benchmark median, per ratio (`ratio: <benchmark>`) and per count, for the trend over releases.
The ratio column is the one to read across CPU models; it steps when a new release becomes the
reference.

**The trend is on the website**, on the [Performance](https://logsquirl.lunicorn-lab.de/performance/) page
(#678): one chart per benchmark, grouped by Benchmark Scenario, for the last 120 days of `history/`, with the
releases marked and each Budget drawn as a line; wall-clock with one line per CPU model, instruction counts
per benchmark binary. The website build draws it as SVG from the checkout of the branch that
`LOGSQUIRL_PERF_DATA` names (`website/src/perf-trend.mjs`); without it the page says it has no runs. The
*website* job brings each recorded run of master to the site (*Release pages on the website*). To see it
locally, with the worktree below: `cd website && LOGSQUIRL_PERF_DATA=/tmp/perf-data npm run dev`.

**The spread within one CPU model** is what #675 re-decides a dedicated benchmark runner on (one
pays off if same-model medians vary by more than 3 %):

    git fetch origin perf-data && git worktree add /tmp/perf-data origin/perf-data
    python3 .github/scripts/perf-history.py spread --data-dir /tmp/perf-data [--since 2026-10-03] --markdown spread.md

prints, per CPU model, the CV of each benchmark's medians over the usable runs: of the reference
build (one tag, so fixed code: the runner's own spread), of master (with its own changes) and of
the ratio. After eight weeks of nightly runs, post its table on #675. The raw runs stay in each run's `perf-result`
artifact, the Callgrind dumps for a week in `perf-instruction-counts-dumps`. Only the *record* job
may push, and only to that branch. Do not delete or rewrite the branch; it is the only copy.

**Dispatched from another branch** (`gh workflow run performance.yml --ref <branch>`), the run is
compared with master's history the same way but recorded under `trial/`, which nothing compares
with, so a branch never moves master's reference, and it files no issue: its findings are in the
job summary of *compare and record*, and the run is red. To see the detection work, push a branch
with a test commit that costs instructions in code a benchmark measures (for example a needless
second pass over the Log Lines while indexing), dispatch the workflow on it and read the summary: the benchmarks it
reaches are regressions, with master's last recorded commit as the last good one and the branch's
commit as the first bad one. `perf-issues.py`'s tests check the issue that would be filed.

**Accepting a slowdown** that is intended: dispatch the workflow from master with
`accept_new_level` (`gh workflow run performance.yml --ref master -f accept_new_level=true`).
That run is recorded as the start of a new level, files no regression, and the series restart from
it, so the check reports only until enough runs of the new level exist. Then close the issue.

The suite measures events, not sleeps (#667): the GUI cases run the benchmark mode's scenarios,
the grep cases the grep tool's own report. The rename of its benchmarks (`grep_search_*` to
`grep_*`, `gui_load_1mb` to `gui_open_1mb_*`) removes benchmarks the history knows, so the first
Performance run after it is dispatched from master with `accept_new_level=true`.

See [`tests/e2e/README.md`](tests/e2e/README.md) for full documentation.

#### Benchmark mode

The application measures what a user waits for itself (#666): started with a scenario, it opens
what the scenario names, waits for the events it reports -- not for a fixed time -- writes one
JSON report and exits. It runs offscreen, so CI can run it:

```bash
QT_QPA_PLATFORM=offscreen build/output/logsquirl \
    --benchmark open-and-index --benchmark-output report.json path/to/file.log
```

| Option | Meaning |
|---|---|
| `--benchmark <scenario>` | The scenario to run. An unknown name lists the scenarios and exits with 2. |
| `--benchmark-output <file>` | Where the report goes; standard output without it. |
| `--benchmark-option <name>=<value>` | An option for the scenario, repeatable; reported under `options`. |
| `--benchmark-timeout <s>` | The run fails after this many seconds (default 600). |
| `--window-width`, `--window-height` | The window's size; 1280 x 800 without them, so runs paint alike. |

The exit code is 0 when the scenario finished, 1 when it failed (the report says why) or its
report could not be written, and 2 for a command line it cannot run.

A **Benchmark Run** reads and writes nothing of yours: its settings, Session, Log Formats,
plugins, themes and crash dumps live in a temporary directory that goes when it ends (a
Portable Run in that directory, see `DataLocation::isolateCurrentIn`); it loads no plugins, does
not check for a new version, uses no Index Cache and takes a single-instance lock of its own,
so it neither hands its Log File to a LogSquirl you are running nor is handed theirs. Apart
from that it starts as a start from the command line does: Theme, Highlighters, TBB and
allocator setup are the application's own. The e2e suite still starts it only through its
isolated instances (`tests/e2e/benchmark_mode.py`).

Measure with an optimized build (RelWithDebInfo or Release), and compare two builds as A/B on
one machine; numbers of a Debug build say nothing.

**Scenarios**

| Scenario | Events | Results |
|---|---|---|
| `open-and-index` | `log_file_opened`: the tab opened and loading started (after the window is shown and the plugins are loaded, as for a Log File given on the command line); `first_log_line_displayed`: the first paint of the Text View's Viewport that shows a Log Line ended; `index_finished`: the Index is complete. Both of the last two carry `log_line_count`. | `log_line_count`, `log_file_bytes`, `index_mb_per_s` (10^6 bytes per second from the open to `index_finished`) |
| `search` | On a loaded Log File (opened and loaded first, unmeasured), one Search as a user typing it into the Search Line and pressing Return runs it (#668). `first_match_displayed`: the first paint of the Filtered View's Viewport that shows a Match ended (none without a Match); `search_finished`: the Search is complete, with `match_count`. Options: `pattern` (required), `regex` (`true`: a regular expression), `match_case` (`false`: case is ignored). An invalid pattern, a failed or an interrupted Search fails the run. | `match_count`, `undecided_count`, `log_line_count`, `log_file_bytes`, `search_gb_per_s` |
| `quickfind` | On a loaded and shown Log File, QuickFind is opened as *Edit → Find* opens it, then `pattern` is typed into it character by character (#668). Each keystroke is answered by the first paint of the Text View's Viewport that starts after it; the next comes once it is answered and at least `keystroke_interval_ms` (100) after the one before. `keystroke_marked` per keystroke, with `typed` and `latency_ms`. Fails when the bar does not hold the typed pattern at the end. | `keystroke_count`, `keystroke_latency` (a summary), `log_line_count`, `log_file_bytes` |
| `scroll` | On a loaded Log File (with `view=table` the Table View is then shown as its toggle shows it), the view's vertical scroll bar steps `line_steps` (200) lines down, `page_steps` (40) pages down and jumps to the end (`QAbstractSlider::triggerAction`), each step once the first paint of the view's Viewport that starts after it has ended (#669). Every paint of that Viewport from the first step until the paint answering the jump has ended is a frame: from its paint event reaching the Viewport to the view's handler returning. The Overview, the Filtered View and compositing are not part of it. `scrolled_to_end`: that last paint ended, with `frame_count`. Options: `view` (`text`, `table`), `highlighters` (`true`: a Highlighter Set of five is made and activated in the run's own collection), `ansi` (`text`, `hide`, `colors`: the setting *ANSI color sequences*), `line_steps`, `page_steps`. For `view=table` Format Recognition is on, and the Log File needs a Log Format the Catalog knows. | `view`, `frame_time` (a summary of the frames, with `budget_ms`, 1000/60, and `over_budget_count`, the frames longer than it), `step_count`, `line_step_count`, `page_step_count`, `unmoved_step_count`, `highlighter_count`, `log_line_count`, `log_file_bytes` |
| `follow` | Writes its own Log File (no Log File argument), follows it with a chart, and a writer thread appends `lines_per_second` (10) Log Lines a second for `duration_ms` (5000), reading the clock just before each write (#670). A Log Line is displayed at the first Text View paint that shows the file through it while following at the end, charted at the chart's first such paint. `writer_finished`, `last_log_line_displayed`, `last_log_line_charted`. Options: `initial_lines` (1000), `chart` (`true`), `chart_budget_ms` (1000), `settle_ms` (5000); a Log Line not displayed within `settle_ms` fails the run. | `display_latency` and `chart_latency` (append to displayed / charted), `chart_behind_display`, `chart_kept_up` (every Log Line charted within `chart_budget_ms` of being displayed), `writer_lateness` |
| `session-restore` | Two or more Log Files. Generates a Session of one tab each, with `marks` (10) Marks, `searches` (3) Kept Searches (the words `error`, `warn`, `info`, `debug`, `fatal` in turn, fixed strings ignoring case, the last one current) and tab `current` (0) in front, in the run's own data location, and restores it (#670, #704). `tab_indexed` per tab, `current_tab_usable` (the first paint of the front tab after its Index finished), `all_tabs_indexed`, `tab_searches_finished` per tab (its Kept Searches ran again once its Log File loaded), `all_tabs_restored` (every tab indexed and every Kept Search finished). | `tab_count`, `current_tab`, `marks_per_tab`, `searches_per_tab`, `log_line_count`, `log_file_bytes` |
| `read-while-indexing` | A small Log File of the run's own is loaded first, unmeasured, so the scenario starts with the request to open the Log File (#686). While it is indexed, a timer on the UI thread reads every `read_interval_ms` (2) `getNbLine`, `getLineString` of one Log Line and `getExpandedLines` of `lines` (60) Log Lines, each timed apart. `log_file_opened`; `index_finished`, with `log_line_count` and `read_count`. Fails when the Index finished before a Log Line could be read. | `nb_line_latency`, `line_string_latency`, `expanded_lines_latency` (summaries), `read_count`, `indexing` (`wall_ms` from the open request to `index_finished`; `cpu_ms`, the process's CPU time in it, every thread together; `parallelism` = `cpu_ms` / `wall_ms`, the last two only where the platform reports CPU time), `index_mb_per_s`, `log_line_count`, `log_file_bytes` |

**The report**, format version 1. Times are milliseconds as floating point numbers, sizes bytes.

| Field | Meaning |
|---|---|
| `format` | Always `"logsquirl-benchmark"`. |
| `format_version` | `1`. Raised when a field is renamed or removed or changes meaning or unit, not for a new field. Readers check it. |
| `scenario` | The scenario's name. |
| `outcome` | `"passed"` or `"failed"`; `failure` then says why. |
| `application` | `version` and `commit` of the build. |
| `platform` | `os`, `kernel`, `cpu_architecture`, `qt_version`, `qpa_platform`. |
| `options` | The `--benchmark-option` values, by name. |
| `log_files` | `path` and `size_bytes` of each Log File given. |
| `process.start_source` | `"os"` when times count from the process start the operating system reports, `"main"` when it did not report one and they count from `main()`. |
| `process.main_entered_ms` | When `main()` was entered, since the process started: loading the libraries. On macOS it includes the system's check of an app bundle copied to a new place, as the e2e suite's isolated instances do for every instance. |
| `process.peak_rss_bytes` | The peak resident set size (macOS, Linux) or peak working set (Windows) when the scenario ended; left out where the platform does not tell. |
| `scenario_started_ms` | When the scenario's measured part began, since the process started (`open-and-index`: the request to open the Log File, after the window was made). |
| `events[]` | In the order they happened: `name`, `since_process_start_ms`, `since_scenario_start_ms`, and `data`, an object of the scenario's own, when it has any. |
| `results` | What the scenario concluded, by name; the table above lists them. A summary of many durations is an object with `count`, `min_ms`, `p50_ms`, `p99_ms` (nearest rank), `max_ms`, `mean_ms`. |

Every time is read from the steady clock at the event itself: the end of a paint is taken by
an event filter around the Viewport's paint event, the end of the load from the signal that
reports it. No sleep and no polling interval is part of a reported number. The time from the
process start to `main()` comes from the operating system: in clock ticks (usually 10 ms) on
Linux, in microseconds on macOS and in 100 ns on Windows; every later time is measured from
`main()` with the steady clock.

**Adding a scenario** is one new source file under `src/app/benchmark/scenarios/`, which the
build picks up without a list to add it to: a class derived from `logsquirl::benchmark::Scenario`
and a `ScenarioRegistration` with its name. It is handed a `ScenarioRun`
(`src/app/benchmark/scenariorun.h`) with the Log Files, the options, the report to put its
events and results in, new windows and the Session of the run's own directory, and ends the run
with `finish()` or `fail()`. `PaintProbe` times the paints of a widget, `Distribution` summarizes
many durations. Nothing in the report or its writer changes for a new scenario.
`LoadedLogFile` opens, loads and shows a Log File before the measured part starts,
`InputLatency` pairs each input with the first paint after it, `FrameTimes` summarizes the frames
of a view, `AppendLatency` pairs appended Log Lines with the paints that show them and
`ProcessWork` takes wall and CPU time. A scenario that measures under settings of its own
overrides `prepare()`: it is called once the run is isolated and before the application reads a
setting, and writes them into the run's settings. Nothing is set up there yet, so no regular
expression is compiled in it.

**The grep tool** takes `--benchmark-output <file>` too (#667): it then writes a report in the
same format, scenario `grep`, timed from just before the Log File is opened, so a Search's
throughput leaves out the process start. Events: `index_finished`, `search_finished`,
`matches_written`; results: `match_count`, `log_file_bytes`, `mb_per_s`.

### Instruction counts

Every pull request that CI Build builds also gets the **Instruction Counts** workflow
(`.github/workflows/instruction-counts.yml`, #671), which CI Build calls as its job
*Instruction counts*: it builds the Catch2 benchmarks of
`tests/benchmarks` in the noble build container as CI ships them (RelWithDebInfo with LTO), once
for the master commit the pull request is merged onto and once for the merge, and counts the
instructions of each benchmark under Valgrind's Callgrind. The job summary and one pull request
comment, updated on every push, show each benchmark's count before and after and the change in
percent; the `instruction-counts` artifact holds the same as JSON, and `instruction-counts-dumps`
the Callgrind dumps, to see in `callgrind_annotate` or KCachegrind where the instructions went.

The job *Instruction counts gate* judges the counts (#672), and the required **CI passed** check
waits for it: a benchmark that costs more than its threshold more instructions than on the base
fails it, unless the pull request carries the `perf-accepted` label, and a benchmark counted on
the base but not on the pull request fails it, label or not (CONTRIBUTING.md, *Instruction
count gate*). The thresholds live in one place, `THRESHOLD_PERCENT` in
`.github/scripts/instruction-counts.py`: +2 % by default, more for the few benchmarks that vary
more (twice the widest spread measured between counts of the same code). The gate reads the
labels when it runs, so a re-run after the label changed judges again; the **Instruction Counts
Label** workflow (`instruction-counts-label.yml`) re-runs it by itself when `perf-accepted` is
added or removed on a pull request from a branch of this repository, and updates the comment.
The verdict is in the job summary, in the `instruction-counts-gate` artifact and in the comment,
which lists the benchmarks that are over their threshold, failed or accepted.

A push to master counts the pushed commit and keeps its counts in the Actions cache, under the
commit, the benchmark sources, the counting script and the runner's CPU model (glibc picks its
string functions by CPU, so counts compare only between runners of one model). A pull request whose
base was counted that way on a runner of the same model takes the before side from there and only
builds and counts itself, in about 17 minutes with a warm sccache; otherwise it builds and counts
both sides, which takes about twice that. Most of a side is the link time optimization of the
twenty-odd benchmark executables and Vectorscan's runtime, which sccache does not cache.

A count is reproducible where a time is not: two runs of the same commit differ by less than
0.5 % for most benchmarks, on a shared runner whose times vary by 5–20 %. The exceptions wait for
other threads: which heap blocks glibc's allocator has free, and how long an idle worker thread
spins, then depend on how the threads took turns, and their counts vary by up to about 3 %
(Valgrind's `--fair-sched=yes` makes that about a third of what it is without). oneTBB's idle
workers spun the most: in one run 1.2 M instructions (9 %) more of a 20-tab Session restore than
in the next (#708). So, counted, a benchmark binary runs oneTBB's flow graphs (indexing, Search)
on the thread that waits for them alone, with no TBB worker thread (`instruction_count.h`); a
timed run keeps TBB's workers. And since a thread's turns can add to a count but never take from
the work, a benchmark that is over its threshold has its binary counted once more on the after
side, from the same build, and the lower of the two counts is the one compared and judged: the
gate fails only when both counts are over. The comment lists both counts of every recounted
benchmark. The before side is not recounted (its build is gone by then, or its counts came from
master's run): a count too high there can hide a cost, but not fail a pull request. That has a
known cost: the after side is judged by the lower of two counts while the before side has one, so
a real regression smaller than a benchmark's spread can pass. It is accepted, because the gate
must never be falsely red (#672); the thresholds, measured while TBB's workers still spun, are to
be measured again and lowered (#727). To count the
same work every time, each benchmark runs its measured code exactly once, in the benchmarks' **fixed-work mode**
(`tests/benchmarks/instruction_count.h`), instead of as often as Catch2's clock asks for:
with `LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1`, `BENCHMARK` and `BENCHMARK_ADVANCED` start
Callgrind's counting where Catch2 would start its clock and write one dump, named
`<test case> / <benchmark>`, where it would stop it. The count covers every thread. Test cases tagged
`[wall-clock]` time themselves without a `BENCHMARK`, or do their work in another process, and are
left out. The generated Log Files are
smaller than in a timed run (4 MiB, also per Session Log File), so the counts are not
comparable with Catch2's times.

The same run also counts each benchmark's **allocations and peak heap** (#673), which the job
summary and the comment show before, after and as a change, in a table of their own: the heap
blocks one run of the measured code allocated, all threads together, and the most heap it held
at once above what was held when it started, in bytes as the allocators round them up. They are
reported only; the gate judges instructions alone. The counting cannot tell a block allocated
before the measured code from one it allocated: a `realloc` of an older block counts as an
allocation, as every `realloc` does, and freeing an older block lowers the heap held at most to
where it started, never below, so the peak is the most the measured code's own blocks held at
once while it frees no older block, and can be less than that when it does. Callgrind keeps the program's own allocator,
and Valgrind's heap tools (DHAT, Massif) only report a whole process, not the stretch between two
points of it, so the counting happens in the benchmark binary itself: configured with
`LOGSQUIRL_BENCHMARK_HEAP_COUNTS=ON`, as `instruction-counts.sh` does (Linux with glibc only, no
sanitizer), each benchmark binary links `tests/benchmarks/heap_count.c`, which replaces glibc's
malloc and its family for the whole process (operator new, Qt and the libraries allocate
through it) and hands every call on to glibc, and the link wraps mimalloc's `mi_new_n` and
`mi_free`, through which `logsquirl::vector` allocates (`--wrap`, in
`tests/benchmarks/CMakeLists.txt`). `instruction_count.h` opens the count where Callgrind starts
and closes it where Callgrind stops, and `heap_count.c` appends a line per benchmark to
`<binary>/heap.tsv`. That costs no second run; the counting adds a few instructions to each
allocation, which the instruction counts include on both sides. Allocation counts count work, as
instruction counts do, and repeat with it, so there is no noise threshold and the table lists
every difference; the benchmarks that wait for other threads can vary as their instruction
counts do, since how often a thread's loop runs while it waits, and when blocks are freed, then
depend on how the threads took turns.

The comment comes from a second workflow, **Instruction Counts Comment**
(`instruction-counts-comment.yml`), which starts when a CI Build run has completed: a pull request from a
fork has a read-only token and cannot comment, and the workflow that can runs only master's code
and reads the artifact as untrusted data.

To reproduce a count locally, on Linux x86-64 with Valgrind installed (`sudo apt-get install
valgrind`; it has no port for macOS on Apple Silicon, where the noble build container under
Docker does it):

```bash
# From the repository root; builds every benchmark (RelWithDebInfo) in build_root and
# counts them into counts/<binary>/
.github/scripts/instruction-counts.sh counts
.github/scripts/instruction-counts.py collect counts --json counts.json

# Or one benchmark binary by hand, after building it with Valgrind's headers installed
# (and configured with -DLOGSQUIRL_BENCHMARK_HEAP_COUNTS=ON for its heap counts, which
# LOGSQUIRL_BENCHMARK_HEAP_FILE then names the file of)
LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1 QT_HASH_SEED=0 QT_QPA_PLATFORM=offscreen \
LOGSQUIRL_BENCHMARK_HEAP_FILE="$PWD/counts/heap.tsv" \
LOGSQUIRL_BENCHMARK_LOG_FILE_MB=4 LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB=4 \
  valgrind --tool=callgrind --instr-atstart=no --trace-children=yes \
    --main-stacksize=67108864 --fair-sched=yes --callgrind-out-file=counts/callgrind.out.%p \
    build_root/output/logsquirl_decoration_benchmark --order decl --rng-seed 1 '~[wall-clock]'
callgrind_annotate counts/callgrind.out.<pid>.<n>   # "totals:" is the count, the rest where it went
```

A count depends on the compiler, the optimization options and the libraries, so only counts from
the same build container and the same options compare; the workflow's are from the noble build
container with the CI Build noble job's options.

### Fuzzing

`tests/fuzz` holds libFuzzer targets for the code that reads bytes from anywhere before a user sees
anything: `indexing_blocks_fuzzer` (indexing blocks and stitching, in every line feed width),
`log_format_fuzzer` (Log Format JSON parser and field extractor) and `ansi_color_fuzzer`. Their seed
inputs are in `tests/fuzz/corpus/<target>/`. CI runs them with ClusterFuzzLite
(`.github/workflows/cflite.yml`, `.clusterfuzzlite/`): on pull requests for the changed code, weekly for
every target.

To run them locally you need Clang with libFuzzer (on macOS Homebrew's `llvm`, not Apple's Clang). The whole
build must be instrumented with `-fsanitize=fuzzer-no-link`, or the fuzzers see no coverage of the libraries:

```bash
F="-fsanitize=address,undefined,fuzzer-no-link"
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F" \
  -DLOGSQUIRL_BUILD_FUZZERS=ON -DLOGSQUIRL_BUILD_TESTS=OFF \
  -DLOGSQUIRL_USE_LTO=OFF -DLOGSQUIRL_USE_VECTORSCAN=OFF
cmake --build build-fuzz --target logsquirl_fuzzers
mkdir -p /tmp/corpus && cp -r tests/fuzz/corpus/ansi_color/. /tmp/corpus/
build-fuzz/output/ansi_color_fuzzer /tmp/corpus -max_total_time=60
```

A crash is written as `crash-<hash>`; run the target with that file as its only argument to reproduce it.
Copy a small input that found new code into `tests/fuzz/corpus/<target>/` to keep it as a seed.

### Sanitizer builds

`cmake/Sanitizers.cmake` adds Address, Memory, Undefined Behavior and Thread sanitizers behind their own
options (GCC/Clang only): `-DENABLE_SANITIZER_ADDRESS=ON`, `-DENABLE_SANITIZER_MEMORY=ON`,
`-DENABLE_SANITIZER_UNDEFINED_BEHAVIOR=ON`, `-DENABLE_SANITIZER_THREAD=ON`. Measure with `RelWithDebInfo` and
`-DLOGSQUIRL_USE_LTO=OFF`; LTO makes linking a sanitizer build a lot slower for no measurement benefit.

```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOGSQUIRL_USE_LTO=OFF -DENABLE_SANITIZER_THREAD=ON ..
cmake --build .
ctest --build-config RelWithDebInfo --verbose
```

**ThreadSanitizer and the libraries it cannot see (#347, #482, #510).** A TSan build builds oneTBB with
TSan too (`TBB_SANITIZE` in `3rdparty/CMakeLists.txt`). glibc comes prebuilt without it, so TSan does not
see its locks and reports what it does on two threads as data races between glibc and itself. `ctest`
sorts those out: `cmake/CatchTestDiscoveryRunTest.cmake` has TSan write its reports to files in the
case's directory, and `cmake/TsanReportFilter.cmake` leaves out a data race only when both racing
accesses were made inside a library `cmake/tsan.supp` lists on an `#@uninstrumented` line, each with its
reason. Any other report, one access in LogSquirl's code or in Qt being enough, fails the case, printed
in full; the case's output ends with lines counting what was left out. `cmake/tsan.supp` holds no `race:`
entry: TSan matches one against callers too, so it would hide races in LogSquirl's code running on top of
Qt's event loop, its thread pool or oneTBB's flow graph. See
`docs/adr/0007-tsan-suppresses-onetbb-and-uninstrumented-qt-internals.md` for how every finding of the
CI job was sorted, what was fixed in the code, and why neither `called_from_lib` nor
`ignore_noninstrumented_modules` does this job.

Qt must be built with TSan as well; the CI job's image has one (`docker/ubuntu24.04-tsan`). Against a
prebuilt Qt, TSan does not see Qt's own locks either, and the cases fail with races between Qt and
itself. To run the TSan suite locally as CI does, build inside that image (it needs
`vm.mmap_rnd_bits=28` on the host, like any GCC TSan binary):

```bash
docker run --rm -v "$PWD":/src -w /src ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble-tsan:latest bash -c \
  "cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOGSQUIRL_USE_LTO=OFF \
     -DENABLE_SANITIZER_THREAD=ON -DLOGSQUIRL_USE_SENTRY=OFF && cmake --build build-tsan && ctest --test-dir build-tsan"
```

Running a TSan binary directly, outside `ctest`, prints every report, the libraries' own included. To
sort them the same way, run the case through the runner:

```bash
cmake -DTEST_BINARY=$(pwd)/build_root/output/logsquirl_tests \
      -P cmake/CatchTestDiscoveryRunTest.cmake -- "<test case name>" -platform offscreen
```

**TSan in CI (#439, #482, #510).** The `Sanitizers / tsan` job in `.github/workflows/ci-build.yml` builds
the same configuration as above in the `logsquirl-ubuntu-noble-tsan` image, whose Qt is built with TSan,
and runs every test case under `ctest`, for every pull request and every push to master. When the
image's hash is not on GHCR yet, the job builds the image first, Qt included (about an hour; its time
limit is 150 minutes for that). It blocks like `Sanitizers / asan-ubsan`, and no case is excluded from
it. It needs no `TSAN_OPTIONS` of its own: the runner sets them, and a change to `cmake/tsan.supp` reaches
it from there. Runtime: about 16 minutes, an 8-minute build and 7 to 8 minutes of tests (the tests took
19 minutes before oneTBB was built with TSan).

That a race in LogSquirl's code turns the job red is checked on every TSan run, not assumed: in a TSan
build `tests/helpers/tsan_canary.cpp` increments a plain `int` from two `std::thread`s and exits 0, and
the `tsan_canary` test (`tests/tsan_canary.cmake`) runs it through the ctest runner. It passes only when the
runner failed the case because TSan's report of that race was kept, not because the canary crashed or
exited non-zero. If the runner, the filter or `cmake/tsan.supp` ever let such a race through, that test
goes red. The same race put into an ordinary unit test case (`OffsetInFile basic operations`) turned the
job red once in CI, run 36147083238, job 108110847495.

## CI/CD Pipeline

### Version Numbering

CI builds use the scheme `YY.MM.PATCH.BUILD`.

- `YY.MM.PATCH` is the `project(... VERSION …)` value declared in
  `CMakeLists.txt`, for every build (pull requests, branch dispatches and
  pushes to master alike).
- `BUILD = github.run_number + 717`, the CI Build run number.

For example, `VERSION 26.06.1` on CI Build run number 50 produces version
`26.06.1.767`. `.github/actions/logsquirl-version` resolves it.

A release publishes the packages CI Build made for the tagged commit on master;
the tag is pushed after that build, so the commit has to declare the release's
version already. Before tagging `vX.Y.Z` (or a pre-release `vX.Y.Z-betaN`), set
`VERSION X.Y.Z` in `CMakeLists.txt` on master and let CI Build pass. CI Release
refuses a build whose base version is not the tag's, naming both.

The same commit must also carry the release's section in `CHANGELOG.md`, headed
with the tag itself and the date, e.g. `# v26.10.0-beta1 (2026-09-17)`: that
section is the release's notes. CI Release fails before anything is signed when it is missing.

### Docker Build Containers

Linux builds use pre-built Docker images hosted on GHCR:

| Image | Based On | Packages |
|-------|----------|----------|
| `ghcr.io/64x-lunicorn/logsquirl-oracle10` | Oracle Linux 10 | Qt 6, GCC, RPM |
| `ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble` | Ubuntu 24.04 | Qt 6, GCC, DEB, Valgrind (*Instruction counts*) |
| `ghcr.io/64x-lunicorn/logsquirl-ubuntu-jammy` | Ubuntu 22.04 | Qt 6, GCC 12, AppImage |
| `ghcr.io/64x-lunicorn/logsquirl-fedora44` | Fedora 44 | Qt 6, GCC, RPM |
| `ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble-tsan` | Ubuntu 24.04 | Qt 6 built from source with ThreadSanitizer, GCC; the `Sanitizers / tsan` job only |

Every image is built with `docker/` as its build context, so all of them install sccache
from the one script `docker/shared/install-sccache.sh`; bumping sccache is an edit to that file only.
Images are content-addressed. `docker/image-hash.sh` hashes an image's own directory and `docker/shared`;
the **Docker Images** workflow pushes each image under that hash as its tag (plus `:latest`, for humans only)
and never overwrites an existing hash tag. CI Build computes the same hash from its checkout and pulls exactly
`<image>:<hash>`; when that tag does not exist (a PR that changes `docker/`, or a merge the workflow has not
published yet) it builds the image locally under the same ref. So an open PR's toolchain changes only when
its own `docker/` files do.

The **GHCR Cleanup** workflow (`ghcr-cleanup.yml`, weekly) deletes the image versions no run uses any more: images
of an earlier hash that are older than 30 days, the old commit SHA tags, and the signatures and manifests that
belong to them. It keeps `:latest`, the hashes of master's `docker/` directories and anything younger than two days.
A deleted image a PR still asks for is built locally, as for any new hash. Dispatch it with *dry run* (the default)
to see what it would delete.

Before pushing, the Docker Images workflow scans each image with Trivy (CRITICAL and HIGH, fixed upstream only) and
uploads the result to code scanning under `trivy-image-<name>`; findings are reported there but do not fail the
build. The pushed image carries buildx SBOM and provenance attestations and a keyless cosign signature. CI Build
refuses a pulled image whose signature does not come from `ci-docker.yml` on master; to check one by hand:

```sh
cosign verify ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble:<hash> \
  --certificate-identity-regexp '^https://github\.com/64x-lunicorn/LogSquirl/\.github/workflows/ci-docker\.yml@refs/heads/master$' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com
```

Every `FROM` is pinned by digest (`image:tag@sha256:…`); Dependabot proposes digest bumps as pull requests.

Images are published when files in `docker/` change on master. OS security patches arrive through a monthly
scheduled run that bumps `docker/shared/refresh-stamp` on the branch `ci/docker-image-refresh` and files an issue
linking to it; opening and merging that pull request gives every image a new hash and so a fresh build. Workflows
may not open pull requests in this repository, so the maintainer opens it, and CI Build runs on it as usual. To
propose a refresh by hand, run the **Docker Images** workflow via `workflow_dispatch` with *propose_refresh*;
running it on master without that publishes any hash tag still missing.

> **AppImage compatibility:** The AppImage is built on the Ubuntu 22.04 (jammy)
> image on purpose. `linuxdeploy` bundles Qt and libssl but never bundles glibc
> or libstdc++ — those come from the host — so the build host's C library sets
> the compatibility floor. Building on jammy pins that floor to glibc 2.35 and
> the GCC 12 libstdc++ (`GLIBCXX_3.4.30`), letting the AppImage run on Ubuntu
> 22.04 and every newer distribution. Do not move the AppImage build to a newer
> base unless you intend to drop support for older distros. (The `.deb` is still
> produced on Ubuntu 24.04 and targets that release and newer.)

> **deb and rpm packages need the distribution's Qt:** unlike the AppImage, the
> `.deb` and `.rpm` packages do not bundle Qt. They are linked against the Qt of
> the build image (aqtinstall, `QT_VERSION` in `docker/*/Dockerfile`) and declare
> the distribution's Qt packages with that version as the minimum
> (`CPACK_DEBIAN_PACKAGE_DEPENDS` / `CPACK_RPM_PACKAGE_REQUIRES` in
> `CMakeLists.txt`, taken from the Qt CMake found). On a distribution whose Qt is
> older, the package manager refuses to install the package; use the AppImage
> there. CI Build's package check (`packaging/linux/check-package.sh`) verifies in
> a plain container of each target distribution that the package ships no static
> libraries, headers (except the Plugin SDK header) or CMake files of its
> dependencies, that it declares the Qt dependency, and that it installs exactly
> when the distribution's Qt is new enough; otherwise the job logs a warning.

### Release Process

A release is prepared in one pull request that sets the version in `CMakeLists.txt`, turns `# Unreleased` in
`CHANGELOG.md` into the release's section, adds the release's `changelog` entry to `latest.json` and its page on
the website (*Release pages on the website*). CI Build's Format job checks a pull request that changes the version
for all of these (`.github/scripts/release-prep.py release-preparation`) and names every missing piece.

Releases are triggered by pushing a git tag to a master commit whose CI Build
push run succeeded, whose `CMakeLists.txt` declares the tag's version and whose
`CHANGELOG.md` has the tag's section (see *Version Numbering*):

- **Stable release**: push a semver tag like `v26.04.0`
- **Beta release**: push a pre-release tag like `v26.04.0-beta1`

The release workflow does not build. It:
1. Finds the successful CI Build run for the push to master that built the
   tagged commit and fails with an error if there is none (still running,
   failed, or never run because the commit was `[skip ci]` or only touched
   ignored paths). It downloads that run's packages by artifact ID, checks each
   against its digest, and checks that the build is the tag's: every artifact
   belongs to that run and commit, the SBOM records the commit, and the version
   file, the SBOM, the Linux package names, `logsquirl_portable.exe` in the
   Windows portable zip and the macOS app (its binary and `CFBundleVersion`)
   carry the tag's version (`.github/scripts/release-build.py`)
2. Signs the macOS app, notarizes and staples it, packs the DMG again from it
   and signs, notarizes and staples the DMG
   (`.github/actions/mac-sign-notarize`), in the `release` environment
3. Uploads debug symbols to Sentry (non-blocking), in the `release` environment
4. Builds the release SBOM `logsquirl-<version>-sbom.cdx.json` (CycloneDX 1.6):
   the CPM packages and pinned platform components that CI Build's SBOM job read
   from the built commit, plus the Qt, OpenSSL and ICU versions found in the
   AppImage, Windows zip and macOS app and what syft finds in them
   (`scripts/sbom/logsquirl_sbom.py`). The Ubuntu 22.04 system libraries the
   AppImage bundles carry no version syft can read: `generate_appimage.sh` asks
   the build image's dpkg database for the package and version of each one
   (`logsquirl_sbom.py appimage-debs`, written to `logsquirl_appimage_debs.json`
   in the AppImage artifact), and the SBOM lists those packages with
   `pkg:deb/ubuntu/...` purls, which grype matches against the Ubuntu security
   tracker. The file itself is not a release asset
5. Scans the SBOM for known vulnerabilities (`scripts/sbom/logsquirl_vulns.py`):
   grype for the components with a CPE, OSV for the CPM packages by pinned commit
   and tag, and Qt's own list of advisories (https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products,
   "Qt Framework" section) for the Qt version, with the severity NVD gives the CVE.
   The scan fails as a tooling error when that page can no longer be read; an advisory whose affected versions
   it cannot read is reported as unconfirmed and does not block. NVD requests are retried on rate limits and
   timeouts; if NVD still cannot be reached for a matched Qt advisory, the release scan fails as a tooling error
   (the daily scan only warns). All findings go to code scanning (category `sbom-vulns`); a critical one (CVSS
   v3/v4 base score ≥ 9.0 or rated critical) stops the release unless `scripts/sbom/vuln-ignore.yml` on master
   accepts it with a reason and an expiry date. So does a Qt advisory NVD has not scored yet (reported as
   *unscored*): assess it and record the decision in the ignore file.
   Without an NVD API key the scanner is limited to 5 NVD requests in 30 seconds. To raise it to 50, request a free
   key at https://nvd.nist.gov/developers/request-an-api-key (it arrives by e-mail and is activated from the link
   in it), then add it as the repository secret `NVD_API_KEY` (Settings → Secrets and variables → Actions, or
   `gh secret set NVD_API_KEY`). The release and daily scans pass it to `.github/actions/sbom-vuln-scan`; without
   the secret they run unkeyed. The file is read
   from master even for a tag release, so accepting a risk and re-running the failed job is enough.
   The `Vulnerability scan` workflow scans master's source SBOM daily and only reports. It also reconciles the
   code scanning alerts with the ignore file, which code scanning would otherwise not act on: an accepted
   finding's alert is dismissed as *won't fix* with the recorded reason, and reopened once the entry expires.
6. Creates a draft GitHub Release with all platform packages, the SBOM and the checksum file, and as its notes
   the tag's `CHANGELOG.md` section followed by how to verify the downloads,
   attests build provenance for every asset and the SBOM for every other asset, signs the checksum file keyless with
   cosign (the `.sigstore.json` bundle is uploaded as an asset but is not listed in
   the checksum file), then publishes the draft. A failure in between leaves a draft.
7. Commits the release to the update feed `latest.json` on the branch `feed/<tag>` and names the link to open
   its pull request (job summary and a notice). The ruleset only lets a pull request with passing checks change
   master, and workflows may not open pull requests here, so **the maintainer opens and merges it**; the app
   announces the release once it is merged. The Changelog check needs no entry for a `feed/` branch.
8. Dispatches **Deploy Website**, so the release's page goes live (see *Release pages on the website*)
9. For a stable release, sets the Homebrew cask to it, checks that it installs and pushes it to the tap, in the
   `release` environment (see *Homebrew tap*)

`latest.json` on master is the update feed LogSquirl reads at start-up
(`src/versioncheck`). Its fields:

| Field | Written by | Used for |
|-------|------------|----------|
| `stable`, `stable_url`, `stable_build` | CI Release (feed pull request), stable tag | The latest stable release: its name, release page and the `YY.MM.PATCH.BUILD` it was published from |
| `beta`, `beta_url`, `beta_build` | CI Release (feed pull request), pre-release tag | The latest beta, offered to users with "check for beta versions" on and to users running a beta |
| `releases` | CI Release (feed pull request) | Every published release name; a running version that was only published as betas runs a beta |
| `changelog` | Release preparation (by hand, oldest first) | One line per release, listed in the update notification for the releases a user skips, up to the offered one |
| `ci`, `ci_url` | CI Release (feed pull request), stable tag (`ci` only) | Read only by LogSquirl 26.07.0 and older, which append an OS suffix to `ci_url`; it ends in `#`, so they land on the latest release page |
| `stable_version`, `beta_version` | CI Release | Not read by the application |

A release is offered when its build is newer than the running one; betas and
the stable release of a version share `YY.MM.PATCH` and differ only in the
build. Without a `*_build` field only a newer `YY.MM.PATCH` is offered. A
re-run of an older release leaves a feed that announces a newer build unchanged.

#### Release pages on the website

A release's page is `website/src/content/docs/news/release-YY-MM.md`, and its frontmatter is the only place
the website writes the release's version:

```yaml
release:
  version: 26.10.0-beta1   # the tag without its "v"
  date: 2026-09-17
  channel: beta            # stable, beta or legacy
```

The sidebar, the release overview and the home page's latest release cards are generated from these pages
(`website/src/releases.mjs`); a missing or malformed field fails the website build. The page is merged with
the release preparation, but **Deploy Website** leaves out every page whose release has no published GitHub
release yet, so it goes live when CI Release dispatches the deploy after publishing. `npm run dev` and the
pull request build show every page.

**The website goes live with a release, not with a merge.** Deploy Website has no push trigger: a website
change merged to master waits for the next release, which is when CI Release dispatches the deploy and the
whole site, including that release's page, goes up at once. Until then the change is only visible in
`npm run dev` and in the pull request build, whose link check is what keeps a broken website out of master.

Dispatching the workflow by hand stays the way to deploy without cutting a release, from the Actions tab or
with `gh workflow run deploy-website.yml --ref master`. Two cases need it: a website fix that cannot wait for
the next release (a wrong download link, legal text), and a release whose deploy did not run or failed, which
leaves the site on the previous release until someone dispatches it. Nothing retries that on its own, and the
dispatch must be on `master`, because the `website` environment admits no other branch.

The nightly **Performance** run dispatches it as well, from its *website* job after recording a run of master,
with `performance_trend` (#678): that deploy builds the
newest published release's tag instead of master, with the newest runs of the `perf-data` branch on its
Performance page, so the trend is current every morning and no unreleased website change goes live with it.
The Budgets drawn are that tag's `tests/e2e/budgets.json`, so a Budget changed on master shows with the next release.
The deploy runs master's steps on the tag's tree: a tag that lacks a file they use (`website/src/perf-trend.mjs`,
`website/scripts/leave-out-unpublished.mjs`), such as a release from before #678, leaves the site as it is with a
notice until the next release deploys (#731).

Manual releases, e.g. to re-run a release, are also supported via
`workflow_dispatch`: dispatch it from the tag (*Use workflow from*, or
`gh workflow run ci-release.yml --ref v26.04.0 -f tag=v26.04.0`) with that tag
as input. The optional CI Build run ID must name the successful push run on
master for the tagged commit; without it, that run is found as for a tag push.

#### Homebrew tap

macOS users on Apple Silicon (macOS 15 or later; there is no Intel build) can install LogSquirl with
`brew install --cask 64x-lunicorn/tap/logsquirl`. The cask is
`Casks/logsquirl.rb` in [`64x-lunicorn/homebrew-tap`](https://github.com/64x-lunicorn/homebrew-tap), a repository
of its own because only a repository named `homebrew-*` can be tapped by that short name. The official
`homebrew/cask` does not accept LogSquirl yet (its notability threshold); once it does, the cask moves there.

The app does not update itself, so the cask declares no `auto_updates` and `brew upgrade` is how cask users get a
new release. That makes a stale cask a stale install, so CI Release's `update-homebrew` job sets `version` and
`sha256` in the cask after every stable release (never a beta, which would reach every cask user):

1. It takes the hash from the `logsquirl-mac-arm64.dmg` line of the release's checksum file, which lists the signed
   DMG as published (`.github/scripts/release-cask.py`). A cask that already has the release and hash, or a newer
   release, stays as it is: a re-run pushes nothing and an older release never downgrades the cask.
2. It commits the change in its clone of the tap, taps that clone, runs `brew audit --cask --strict --online`,
   installs the cask, checks that the installed app's `CFBundleVersion` is the release's version and uninstalls it
   again, on a macOS runner and without launching the app. The audit checks URL and hash, not that the DMG holds
   the app the cask names; the install does.
3. Only then it pushes the commit to the tap's `main`, through the deploy key `HOMEBREW_TAP_DEPLOY_KEY` (a secret of
   the `release` environment with write access to `homebrew-tap` alone). The tap has no required checks, so unlike
   the update feed there is no pull request.

The job runs after the release is published: when it fails, the release is out and only the cask lags behind. Fix
the cause and re-run the job, or update the cask by hand the same way (`release-cask.py update`, then audit and
push). Releases up to 26.07.0 have no DMG line in their checksum file; hash their download with `shasum -a 256`.

Removing the tap, for instance once the cask moves to `homebrew/cask`, means removing three things together: the
`update-homebrew` job, the tap's deploy key and the `HOMEBREW_TAP_DEPLOY_KEY` secret.

#### Package repository

Ubuntu 24.04 (amd64) users add the LogSquirl APT repository once (README) and get every release with
`apt upgrade`. It is served at `https://packages.lunicorn-lab.de/apt` by the GitHub Pages of this repository,
deployed from Actions: no package binary is ever committed, and the website's FTPS sync never touches it. The
domain is a CNAME to `64x-lunicorn.github.io` at Netcup, so the repository can move without users changing anything.

After every stable release, CI Release calls `publish-packages.yml` (never after a beta, which would reach every
apt user). The repository is rebuilt completely on every run, so there is no state to corrupt and a re-run gives the
same repository:

1. `release-apt.py select` takes the last three stable, published releases from the release list that carry a noble
   `.deb` and a checksum file, ordered by version.
2. `release-apt.py build` refuses a `.deb` whose SHA-256 differs from the one its release's checksum file lists, so
   the served files are the release assets byte for byte and their attestations still apply. It lays out
   `apt/pool`, `apt/dists/noble` (`Packages` from `apt-ftparchive`, and a `Release` dated by the newest release, not
   by the build), the public key `logsquirl-packages.asc` and the deb822 file `logsquirl.sources`.
3. `sign` writes `InRelease` and `Release.gpg`; `verify` checks both with the served public key alone, and a real
   `apt-get update` against the site (with `Signed-By` and a `file:` URI) must show no warning, offer every
   selected release and download the newest `.deb` unchanged.
4. The site is the Pages artifact; the `deploy` job deploys it.

Signing and deploying are separate jobs because a job has one environment: `build` runs in `release` and holds the
signing key `PACKAGES_GPG_PRIVATE_KEY` with `contents: read` only; `deploy` runs in `github-pages` with only
`pages: write` and `id-token: write`. The workflow fails when the secret holds another key than the pinned
fingerprint `51ABA6432D0407ED62E8EC403169E5DF85C9A3A3` (#379).

A failing run leaves the release out and the repository as it was: fix the cause and re-run the job, or dispatch
Publish Packages *from a release tag* (*Use workflow from*, or `gh workflow run publish-packages.yml --ref <tag>`);
any `v*` tag that contains the workflow works, the content is always the latest stable releases. A branch fails at
once with a message, because the signing key is only reachable from `v*` tags. Dispatch it for the first fill and
after a key rotation. A tag from before this workflow existed has no workflow to dispatch.

Rotating the key is a deliberate step, because every user then has to fetch the new key: create the key as #379
describes (without a passphrase: the secret is the key alone, and signing fails on a protected one), replace the secret, change `PACKAGES_KEY_FINGERPRINT` in `publish-packages.yml` and dispatch Publish
Packages from a release tag. The site is always built as a whole: the APT and the DNF repositories are one Pages
artifact.

**DNF repositories (#381).** The same run builds `dnf/fedora` (the Fedora 44 RPM) and `dnf/el10` (the Oracle Linux 10
RPM) from the last three stable releases that carry them, with `release-dnf.py` (`select`, `build`, `sign`, `verify`,
the same steps as for APT), and serves `logsquirl-fedora.repo` and `logsquirl-el10.repo` next to the key. Only the
metadata is signed, with the key of the APT repository (`repomd.xml.asc`; `repo_gpgcheck=1`, `gpgcheck=0`): signing an
RPM rewrites it, and it would no longer be the release asset the attestations cover. The signed `repomd.xml` carries
every package's checksum, so dnf still verifies each package. `build` refuses an RPM whose SHA-256 differs from its
release's checksum file. The Fedora RPM is built against Fedora 44; when a newer Fedora changes its Qt it may stop
working there until the build matrix follows, which is why the install instructions name the supported releases.
The workflow's last check runs dnf itself in clean `fedora:44` and `oraclelinux:10` containers
(`.github/scripts/check-dnf-repo.sh`): no warning from the signed metadata, every release offered, the downloaded RPM
is the release asset, and an older release upgrades to the newest with `dnf upgrade`. `createrepo_c` is given the
newest release's publication as revision, so a re-run is meant to give the same metadata.

#### Secrets and environments

The signing and upload secrets are not repository secrets but secrets of GitHub
Environments, so only the jobs bound to an environment can read them, and only
for the refs its deployment policy admits:

| Environment | Deployment policy | Secrets | Jobs |
|-------------|-------------------|---------|------|
| `release` | tags `v*` | `MACOS_P12_FILE`, `MACOS_P12_PASSWORD`, `APPLE_ID`, `APPLE_PASSWORD`, `APPLE_TEAM_ID`, `SENTRY_TOKEN`, `HOMEBREW_TAP_DEPLOY_KEY`, `PACKAGES_GPG_PRIVATE_KEY` | CI Release `sign-mac`, `sentry`, `update-homebrew` (the tap's deploy key, write access to `homebrew-tap` only); Publish Packages `build` (the package repository's signing key) |
| `github-pages` | branch `master`, tags `v*` | none | Publish Packages `deploy` |
| `website` | branch `master` | `FTP_SERVER`, `FTP_USERNAME`, `FTP_PASSWORD` | Deploy Website `deploy` |

CI Release calls Publish Packages with `secrets: inherit`. Without it the `release` environment's secrets reach
the called workflow empty, although its `build` job names the environment (actions/runner#4453, #626).

CI Build never signs and references no signing secret: a pull request, a push
to master and a `workflow_dispatch` on any branch all produce the same unsigned
macOS and Windows packages. A manual CI Release dispatched from a branch fails
before anything is downloaded, because its signing job could not enter the
`release` environment.

### Workflows

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `ci-build.yml` | push/PR to master | Build + test all platforms, check the update feed; on a pull request also check a release preparation and build the website with its link check |
| `changelog.yml` | PR to master (also on label changes) | Require a CHANGELOG entry under `# Unreleased`, or the `no-changelog` label |
| `deploy-website.yml` | dispatch only: by CI Release after a release is published, by the nightly Performance run (`performance_trend`), or by hand from the Actions tab | Build the website without the pages of unpublished releases, with the performance trend of the `perf-data` branch, and upload it; `performance_trend` rebuilds the last published release's site |
| `ci-release.yml` | tag push `v*` | Sign and publish the CI Build packages of the tagged commit as a GitHub Release |
| `publish-packages.yml` | called by CI Release after a stable release; dispatch from a release tag | Build the signed APT and DNF repositories from the last three stable releases and deploy them with GitHub Pages |
| `ci-docker.yml` | `docker/**` changes | Build + push Docker images to GHCR |
| `ghcr-cleanup.yml` | weekly schedule, dispatch | Delete the build image versions on GHCR that no CI run uses any more |
| `renovate-checksums.yml` | PR from a `renovate/*` branch | Recompute the SHA-256 of every pinned download after a Renovate version bump |
| `instruction-counts.yml` | called by CI Build for a push/PR to master it builds | Count the instructions of every Catch2 benchmark under Callgrind, with its allocations and peak heap: before and after a pull request, reported in the job summary and the artifact, and judged by CI Build's gate; a push to master keeps its counts for the pull requests based on it (see *Instruction counts*) |
| `instruction-counts-comment.yml` | `workflow_run` of CI Build | Post the report and the gate's verdict as one pull request comment, updated on every run, with master's code only |
| `instruction-counts-label.yml` | `perf-accepted` added to or removed from a PR | Re-run CI Build's instruction counts gate and update the comment (see *Instruction counts*) |
| `performance.yml` | nightly schedule (02:41 UTC), dispatch | Measure master's e2e performance suite and the instruction counts of its benchmarks in an optimized build, find change points and broken Budgets, record the run on the `perf-data` branch and file an issue per scenario with a finding (see *Nightly performance*) |
| `pgo.yml` | dispatch only | Build each platform without and with profile-guided optimization (and BOLT on Linux), measure the micro-benchmarks and the e2e performance suite of all of them on one runner, and report the A/B tables and the build times (see *Profile-guided optimization*) |
| `codeql-analysis.yml` | push/PR + weekly schedule | CodeQL security analysis of the C++ code and the workflows; results in third-party code (`build/_deps`, `cpm_cache`) are dropped before upload, because `paths-ignore` has no effect for compiled languages |


Every pull request runs CI Build, so the required **CI passed** check always reports. Its *Changes* job
skips the build, test and SBOM jobs for a pull request that changes only the files a push to master ignores
(`website/**`, `latest.json`, `BUILD.md`, `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `.gitignore`), and
runs the *Website* job only when the website changed: `npm test`, then `npm run build` (which fails on a broken
internal link) twice, as is and as deployed without the pages of unpublished releases. The *Format* job checks the
update feed on every run (`.github/scripts/release-feed.py check`).

### Action pinning

Every third-party action in `.github/workflows/` and `.github/actions/` is pinned to the full commit SHA of a
release, with that release's exact version as a comment; local actions (`./.github/actions/...`) are exempt:

```yaml
- uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
```

A tag can be moved to other code, a commit SHA cannot. Dependabot reads the `# vX.Y.Z` comment and bumps SHA and
comment together. To add or bump an action by hand, resolve the release tag to its commit (peel an annotated tag:
`gh api repos/<owner>/<repo>/git/ref/tags/<tag>`, then `.../git/tags/<sha>` while the object type is `tag`). The
repository requires SHA pinning, and the Format job of CI Build runs the same check as

```bash
.github/scripts/check-action-pins.sh
```

### Dependency updates

Two bots propose dependency updates as pull requests, each for what the other cannot read, so no dependency gets
PRs from both:

- **Dependabot** (`.github/dependabot.yml`): the GitHub Actions `uses:` pins, the digest-pinned Docker `FROM` lines
  and the website's npm packages.
- **Renovate** (`renovate.json5`, only its custom regex managers and pip-compile are enabled): the CPM packages in
  `3rdparty/CMakeLists.txt`, every tool version pinned in workflows, composite actions, the build images, the
  packaging scripts and `cmake/*.cmake` (Qt, OpenSSL, Boost, Ninja, CMake, Ragel, sccache, grype, NSIS, create-dmg,
  sentry-cli, linuxdeploy, minidump-stackwalk, the aqtinstall commit of `install-qt-action` and the Renovate config
  validator itself), the digests of CI Build's install-check images (`check_container`), and the hash-locked pip
  requirements.

**pip requirements.** Every `pip install` in CI and the build images reads a requirements file with exact versions and
hashes and passes `--require-hashes`: `docker/shared/aqtinstall-requirements.txt` (aqtinstall, installed into a
throwaway directory that is deleted once Qt is in the image), `.github/requirements/clang-format.txt`,
`.github/requirements/clang-tidy.txt`, `.github/requirements/e2e.txt` and `scripts/sbom/requirements.txt`. Each is generated from the `.in` file next to it by
the `uv pip compile --generate-hashes --universal` command in its header; after editing a `.in` file, rerun that
command. Renovate bumps the pins and reruns the command, and re-locks the dependencies below them once a month.

Both wait until a release is seven days old and run weekly; Renovate lists everything it tracks on its
**Dependency Dashboard** issue. Renovate's grouping:

- one PR per dependency, with a major version in a PR of its own;
- **Qt** in one PR across the CI matrices, CodeQL, the four Dockerfiles and this file (the SBOM job fails when they differ);
- **build tools** (CMake, Ninja, Ragel, sccache) in one PR, since a bump in `docker/` rebuilds every build image;
- **linuxdeploy** and its Qt plugin together;
- a CPM package pinned to a release updates `VERSION`, the commit SHA and the `# <tag>` comment in one change;
- a CPM package without releases (forks such as `variar/oneTBB`, `KDAB/KDToolBox`, `getsentry/sentry-native`) tracks
  its default branch and only gets a PR once its checkbox on the Dependency Dashboard is ticked.

A tool pin that is downloaded and verified is written as a block Renovate and the checksum script both read; to add
one, follow the same form and add its download URL to `URLS` in `.github/scripts/update-checksums.py`. The block goes
into a composite action (`.github/actions/*/action.yml`), a Dockerfile, a script or a CMake module (`set(NAME "v")`
lines, as in `cmake/MinidumpStackwalk.cmake`), never into a workflow file: the
Renovate Checksums workflow pushes with `GITHUB_TOKEN`, which may not change `.github/workflows/`, so
`update-checksums.py --list` fails on a pair there. That is why OpenSSL (`.github/actions/windows-openssl`) and
sentry-cli (`.github/actions/install-sentry-cli`) are installed by composite actions:

```yaml
# renovate: datasource=github-releases depName=anchore/grype
GRYPE_VERSION: 0.118.0
GRYPE_SHA256: 1d444c5e…
```

**Checksums.** Renovate's hosted app cannot download a release and hash it, so its PR changes the version and
leaves the SHA-256 stale. The **Renovate Checksums** workflow runs on every PR from a `renovate/*` branch, recomputes
the hash of each pair whose version differs from the PR's base branch and pushes a commit with the corrected hashes.
A hash that no longer matches a version the PR did not change is never rewritten: the run fails naming the
dependency and URL, since the release's bytes changed under the same version and need a look first. GitHub starts no workflows for a
push made with `GITHUB_TOKEN`, so **close and reopen the PR** after that commit appears to run CI Build on it. The
new hashes are what the URL served at that moment: where upstream publishes checksums (grype, CMake, Boost), compare
before merging. Renovate stops rebasing a branch someone else has pushed to; tick *rebase* on the PR to get a fresh
one (the workflow then fixes the hashes again). Locally:

```bash
.github/scripts/update-checksums.py --list                 # parse only: every pair has a URL rule (the Format job runs this)
.github/scripts/update-checksums.py --check                # download and verify every hash
.github/scripts/update-checksums.py --base origin/master   # rewrite the hashes of versions changed against master
```

The Format job also runs `renovate-config-validator` on `renovate.json5`.

**Setting it up.** Install the [Renovate GitHub App](https://github.com/apps/renovate) for this repository only.
Because `renovate.json5` already exists, Renovate skips its onboarding PR; on its first scheduled run it opens the
Dependency Dashboard issue and the PRs for everything already outdated (for example Catch2, xxHash, mimalloc,
simdutf, CMake). Merge them one at a time, closing and reopening each PR once the checksum commit is on it.

### Repository settings

Some guarantees live in the repository settings rather than in a workflow file: `GITHUB_TOKEN` is read-only unless a
job asks for more, workflows cannot create or approve pull requests, only GitHub-owned actions and an explicit list
of third-party actions may run, SHA pinning is required, and `v*` tags can only be created by an admin and never
moved or deleted. `.github/scripts/repo-settings.sh` holds that list and both checks and applies it (admin `gh` login
needed):

```bash
.github/scripts/repo-settings.sh check   # exit 1 on drift
.github/scripts/repo-settings.sh apply
```

`--defer-sha-pinning` (for both) leaves required SHA pinning untouched. Use it while master still has workflows with
unpinned actions, since requiring pins fails every such run.

A new third-party action has to be added to the script's `ALLOWED_ACTIONS` and applied before the workflow using it
can run. The allowlist also applies to actions that other actions call internally (for example `aquasecurity/trivy-action`
runs `aquasecurity/setup-trivy`), and `owner/repo@*` does not cover a subdirectory such as
`jurplel/install-qt-action/action`. `.github/scripts/check-action-allowlist.py` follows every `uses:` into the
referenced actions at their pinned commits and fails with the missing pattern; the Workflow Security workflow runs it on
every pull request that touches `.github/`, and it runs locally with any `gh` login. Because workflows cannot create `v*` tags, push the release tag before dispatching CI Release by hand.
