"""
Performance regression tests for LogSquirl ("Safari Rule").

Every benchmark reports what a user waits for, timed by the application at the
event itself (#667): the GUI cases run the benchmark mode (BUILD.md, "Benchmark
mode") and report when the first Log Line was displayed and when the Index was
finished, when the first Match of a Search was displayed and when it finished,
how long each keystroke of a QuickFind took to be marked (#668), and how long
each frame of a scripted scroll took to paint in the Text View and the Table
View (#669); the grep cases run logsquirl_grep with a benchmark report and time
its Search from the open of the Log File to the last match written. Neither
contains the process startup or a fixed wait; the startup is a case of its own
(gui_startup_version), the one case timed around a whole process.

Each benchmark is compared against baseline.json. If the measured median exceeds
the baseline by more than the configured tolerance (default 5%), the test fails --
but only when Welch's t-test confirms statistical significance.

Run with --update-baseline to save new measurements as the baseline.

Methodology:
  - 3 warmup runs (discarded, configurable via --bench-warmup)
  - 21 measured runs (configurable via --bench-runs); the 1 GB cases at most 7
  - IQR-based outlier filtering (1.5× IQR)
  - Full statistics: median, mean, std, CV%, P5, P95, IQR
  - Regression detection: median + tolerance AND Welch's t-test (p < 0.05)
  - Auto-generated benchmark report (Markdown or JSON)

Adding a case: a row in GREP_CASES or GUI_OPEN_CASES. A new scenario of the
benchmark mode gets a table of its own and a test that turns one run's report
into {benchmark name: seconds} for measure_events(), like
test_perf_gui_open_and_index; _record() does the rest.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from benchmark_mode import (
    EXIT_PASSED,
    SEARCH_VARIANTS,
    ScenarioUnknown,
    SearchVariant,
    run_benchmark,
    run_grep_benchmark,
    run_known_scenario,
    seconds_since_scenario_start,
)
from conftest import (
    assert_performance,
    calculate_throughput,
    collect_system_info,
    generate_benchmark_report,
    load_baseline,
    measure_events,
    measure_execution,
    save_baseline,
    summarize_frames_over_budget,
)
from generate_test_data import SCROLL_ANSI_LOG_FILE, SCROLL_LOG_FILE

# The most runs a case on a 1 GB Log File takes, after a single warmup run: a
# run takes seconds there, and its noise is small against them.
LARGE_FILE_RUNS = 7


@pytest.fixture(scope="module")
def baseline(request):
    """baseline.json, or None when the run compares elsewhere (--no-baseline-compare)."""
    if request.config.getoption("--no-baseline-compare"):
        return None
    return load_baseline()


@pytest.fixture(scope="module")
def collected_results():
    """Collect all benchmark results in a module-scoped dict for batch baseline update."""
    return {}


@pytest.fixture(scope="module")
def bench_config(request):
    """Get benchmark configuration from CLI options."""
    return {
        "runs": request.config.getoption("--bench-runs"),
        "warmup": request.config.getoption("--bench-warmup"),
        "report_format": request.config.getoption("--bench-report"),
    }


def _log_file(test_data_dir: Path, filename: str, generated: bool) -> Path:
    """The Log File of a case; a generated one that is missing skips the case."""
    filepath = test_data_dir / filename
    if generated and not filepath.exists():
        pytest.skip(
            f"{filename} not found. Run: python tests/e2e/generate_test_data.py"
        )
    return filepath


def _runs(bench_config: dict, large: bool) -> dict:
    """warmup and runs of a case: the configured ones, fewer for a 1 GB Log File."""
    if large:
        return {"warmup": min(bench_config["warmup"], 1),
                "runs": min(bench_config["runs"], LARGE_FILE_RUNS)}
    return {"warmup": bench_config["warmup"], "runs": bench_config["runs"]}


def _record(results: dict[str, dict], collected_results, baseline, request):
    """Keeps every result for the report and compares each with the baseline.

    All of them are compared before the test fails or skips, so one missing
    baseline entry does not hide a regression of another.
    """
    collected_results.update(results)
    if request.config.getoption("--update-baseline"):
        return
    regressions, not_compared = [], []
    for name, result in results.items():
        try:
            assert_performance(name, result, baseline)
        except AssertionError as error:
            regressions.append(str(error))
        except pytest.skip.Exception as skipped:
            not_compared.append(str(skipped))
    if regressions:
        pytest.fail("\n\n".join(regressions), pytrace=False)
    if not_compared:
        pytest.skip("; ".join(not_compared))


# ---------------------------------------------------------------------------
# Grep: the Search, from the open of the Log File to the last match written
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class GrepCase:
    name: str
    log_file: str  # in test_data/
    pattern: str
    description: str
    # Written by generate_test_data.py, never checked in; the case is `slow`.
    generated: bool = False
    # A 1 GB Log File: fewer runs (LARGE_FILE_RUNS).
    large: bool = False
    timeout: float = 120


GREP_CASES = [
    GrepCase("grep_1mb_simple", "random_block_1Mb.txt", "ZZZZ", "Simple pattern search"),
    GrepCase("grep_1mb_regex", "random_block_1Mb.txt", r"[A-Z]{5,}\+[a-z]{3,}",
             "Complex regex search"),
    GrepCase("grep_1_5mb_simple", "random_block_1.5Mb.txt", "ZZZZ", "Simple pattern search"),
    GrepCase("grep_utf16_1mb", "random_block_1Mb_utf16le.txt", "test",
             "UTF-16LE encoding overhead"),
    GrepCase("grep_1mb_no_match", "random_block_1Mb.txt", "ZZZZZ_NEVER_MATCH_99999",
             "No-match scan-only overhead"),
    GrepCase("grep_1mb_alternation", "random_block_1Mb.txt", "ERROR|WARNING|CRITICAL",
             "Regex alternation"),
    GrepCase("grep_1mb_case_insensitive", "random_block_1Mb.txt", "(?i)zzzz",
             "Case-insensitive regex"),
    GrepCase("grep_10mb_simple", "random_block_10Mb.txt", "ZZZZ",
             "Simple pattern, 1 MB Log Lines", generated=True),
    GrepCase("grep_10mb_regex", "random_block_10Mb.txt", r"[A-Z]{5,}\+[a-z]{3,}",
             "Complex regex, 1 MB Log Lines", generated=True),
    GrepCase("grep_50mb_simple", "random_block_50Mb.txt", "ZZZZ",
             "Simple pattern, 1 MB Log Lines", generated=True, timeout=300),
    GrepCase("grep_100mb_simple", "random_block_100Mb.txt", "ZZZZ",
             "Simple pattern, 1 MB Log Lines", generated=True, timeout=600),
    GrepCase("grep_utf16_10mb", "random_block_10Mb_utf16le.txt", "test",
             "UTF-16LE encoding at scale", generated=True),
    GrepCase("grep_log_100mb_simple", "generated_100Mb.log", "ERROR",
             "The ERROR Log Lines (1 in 101) of a 100 MB Log File", generated=True,
             timeout=600),
    GrepCase("grep_log_100mb_regex", "generated_100Mb.log", r"timed out after [0-9]{4} ms",
             "Regex over a 100 MB Log File", generated=True, timeout=600),
    GrepCase("grep_log_1gb_simple", "generated_1Gb.log", "ERROR",
             "The ERROR Log Lines (1 in 101) of a 1 GB Log File", generated=True,
             large=True, timeout=1800),
]


def _cases(cases):
    return [
        pytest.param(case, id=case.name, marks=[pytest.mark.slow] if case.generated else [])
        for case in cases
    ]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(GREP_CASES))
def test_perf_grep(
    case: GrepCase, logsquirl_grep_binary, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """logsquirl_grep: open, index, search and write the matches, startup excluded."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "grep.json"

    def search() -> dict[str, float]:
        run = run_grep_benchmark(logsquirl_grep_binary, case.pattern, filepath, report_path,
                                 timeout=case.timeout)
        if run.report is None and "benchmark-output" in run.process.stderr:
            # The before side of the Benchmarks workflow runs this suite on
            # the binaries of its own commit, which may predate the report.
            pytest.skip("this logsquirl_grep writes no benchmark report (#667)")
        assert run.process.returncode == 0 and run.report is not None, run.process.stderr
        assert run.report["outcome"] == "passed", run.report.get("failure")
        return {case.name: seconds_since_scenario_start(run.report, "matches_written")}

    result = measure_events(search, **_runs(bench_config, case.large))[case.name]
    result["measures"] = f"{case.description}: open to last match written, startup excluded"
    result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
    _record({case.name: result}, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's open-and-index scenario
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class GuiOpenCase:
    # The benchmarks are gui_open_<label>_first_line and gui_open_<label>_indexed.
    label: str
    log_file: str  # in test_data/
    description: str
    generated: bool = False
    large: bool = False
    timeout: float = 300

    @property
    def name(self) -> str:
        return f"gui_open_{self.label}"


GUI_OPEN_CASES = [
    GuiOpenCase("1mb", "random_block_1Mb.txt", "A 1 MB Log File of one Log Line"),
    GuiOpenCase("log_100mb", "generated_100Mb.log", "A 100 MB Log File", generated=True),
    GuiOpenCase("log_1gb", "generated_1Gb.log", "A 1 GB Log File", generated=True, large=True,
                timeout=1200),
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(GUI_OPEN_CASES))
def test_perf_gui_open_and_index(
    case: GuiOpenCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI opens a Log File: its first Log Line displayed and its Index finished."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "open-and-index.json"
    first_line, indexed = f"{case.name}_first_line", f"{case.name}_indexed"

    def open_and_index() -> dict[str, float]:
        run = run_benchmark(isolated_gui_module, "open-and-index", [filepath], report_path,
                            timeout=case.timeout)
        assert run.process.returncode == EXIT_PASSED and run.report is not None, (
            run.process.stdout + run.process.stderr
        )
        return {
            first_line: seconds_since_scenario_start(run.report, "first_log_line_displayed"),
            indexed: seconds_since_scenario_start(run.report, "index_finished"),
        }

    results = measure_events(open_and_index, **_runs(bench_config, case.large))
    results[first_line]["measures"] = (
        f"{case.description}: open to first Log Line displayed, startup excluded"
    )
    results[indexed]["measures"] = f"{case.description}: open to Index finished, startup excluded"
    results[indexed]["throughput"] = calculate_throughput(
        filepath, results[indexed]["median_seconds"]
    )
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's search scenario, a Search on a loaded Log File
# ---------------------------------------------------------------------------

# The generated Log Files the GUI's Search and QuickFind are measured on, by the
# label their benchmarks carry: (Log File, 1 GB).
GENERATED_LOG_FILES = {
    "log_100mb": ("generated_100Mb.log", False),
    "log_1gb": ("generated_1Gb.log", True),
}


@dataclass(frozen=True)
class SearchCase:
    # The benchmarks are gui_search_<label>_first_match (none for a Search
    # without a Match) and gui_search_<label>_finished.
    label: str
    log_file: str  # in test_data/, generated
    variant: SearchVariant
    large: bool = False
    timeout: float = 900

    @property
    def name(self) -> str:
        return f"gui_search_{self.label}"

    @property
    def generated(self) -> bool:
        return True

    def benchmark_names(self) -> set[str]:
        names = {f"{self.name}_finished"}
        if self.variant.label != "no_match":
            names.add(f"{self.name}_first_match")
        return names


SEARCH_CASES = [
    SearchCase(f"{label}_{variant.label}", log_file, variant, large=large,
               timeout=1800 if large else 900)
    for label, (log_file, large) in GENERATED_LOG_FILES.items()
    for variant in SEARCH_VARIANTS
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(SEARCH_CASES))
def test_perf_gui_search(
    case: SearchCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI searches a loaded Log File: its first Match displayed and the Search finished."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "search.json"
    first_match, finished = f"{case.name}_first_match", f"{case.name}_finished"
    # What a correct Search finds, counted without LogSquirl: a Search that
    # finds anything else is not measured.
    known = case.variant.known_match_count(filepath)

    def search() -> dict[str, float]:
        try:
            run = run_known_scenario(isolated_gui_module, "search", [filepath], report_path,
                                     options=case.variant.options(), timeout=case.timeout)
        except ScenarioUnknown:
            pytest.skip("this logsquirl has no search scenario (#668)")
        assert run.process.returncode == EXIT_PASSED and run.report is not None, (
            run.process.stdout + run.process.stderr
        )
        assert run.report["results"]["match_count"] == known, (
            f"the Search found {run.report['results']['match_count']} Matches, not {known}"
        )
        measured = {finished: seconds_since_scenario_start(run.report, "search_finished")}
        if first_match in case.benchmark_names():
            measured[first_match] = seconds_since_scenario_start(run.report, "first_match_displayed")
        return measured

    results = measure_events(search, **_runs(bench_config, case.large))
    description = f"{case.variant.description} ({case.log_file})"
    if first_match in results:
        results[first_match]["measures"] = (
            f"{description}: Search requested to first Match displayed, Log File loaded before"
        )
    results[finished]["measures"] = (
        f"{description}: Search requested to Search finished, Log File loaded before"
    )
    results[finished]["throughput"] = calculate_throughput(
        filepath, results[finished]["median_seconds"]
    )
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's quickfind scenario, typing a QuickFind pattern
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class QuickFindCase:
    # The benchmarks are gui_quickfind_<label>_keystroke_p50 and _p99: of one
    # run's keystrokes, the median and the 99th percentile of the time from a
    # keystroke to the Matches on screen marked.
    label: str
    log_file: str  # in test_data/, generated
    pattern: str
    large: bool = False
    timeout: float = 900

    @property
    def name(self) -> str:
        return f"gui_quickfind_{self.label}"

    @property
    def generated(self) -> bool:
        return True

    def benchmark_names(self) -> set[str]:
        return {f"{self.name}_keystroke_p50", f"{self.name}_keystroke_p99"}


# "slow response" is in every WARN Log Line, one in 13: a few on every screen.
QUICKFIND_CASES = [
    QuickFindCase(label, log_file, "slow response", large=large,
                  timeout=1800 if large else 900)
    for label, (log_file, large) in GENERATED_LOG_FILES.items()
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(QUICKFIND_CASES))
def test_perf_gui_quickfind(
    case: QuickFindCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI's QuickFind typed character by character: each keystroke until marked on screen."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "quickfind.json"
    p50, p99 = f"{case.name}_keystroke_p50", f"{case.name}_keystroke_p99"

    def type_pattern() -> dict[str, float]:
        try:
            run = run_known_scenario(isolated_gui_module, "quickfind", [filepath], report_path,
                                     options={"pattern": case.pattern}, timeout=case.timeout)
        except ScenarioUnknown:
            pytest.skip("this logsquirl has no quickfind scenario (#668)")
        assert run.process.returncode == EXIT_PASSED and run.report is not None, (
            run.process.stdout + run.process.stderr
        )
        latency = run.report["results"]["keystroke_latency"]
        assert latency["count"] == len(case.pattern)
        return {p50: latency["p50_ms"] / 1000.0, p99: latency["p99_ms"] / 1000.0}

    results = measure_events(type_pattern, **_runs(bench_config, case.large))
    what = f"QuickFind typing '{case.pattern}' in {case.log_file}, a keystroke to its Matches marked"
    results[p50]["measures"] = f"{what}: the median keystroke of a run"
    results[p99]["measures"] = f"{what}: the 99th percentile keystroke of a run"
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's scroll scenario, the frames of a scripted scroll
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ScrollCase:
    # The benchmarks are gui_scroll_<label>_frame_p50, _frame_p99 and
    # _frame_max: of one run's frames -- every paint of the view's Viewport
    # while the script scrolls -- the median, the 99th percentile and the
    # longest. _frame_p99 also carries the frames over budget of each run.
    label: str
    log_file: str  # in test_data/, generated
    view: str  # text or table
    description: str
    highlighters: bool = False
    # The setting "ANSI color sequences": text, hide or colors.
    ansi: str = "text"
    timeout: float = 600

    @property
    def name(self) -> str:
        return f"gui_scroll_{self.label}"

    @property
    def generated(self) -> bool:
        return True

    def options(self) -> dict[str, str]:
        """The --benchmark-option values of the scroll scenario; its script is the default one."""
        return {"view": self.view, "highlighters": "true" if self.highlighters else "false",
                "ansi": self.ansi}

    def benchmark_names(self) -> set[str]:
        return {f"{self.name}_frame_p50", f"{self.name}_frame_p99", f"{self.name}_frame_max"}


# The Text View and the Table View on the same Log Lines, with and without a
# Highlighter Set; ANSI colors hidden and shown on the same Log Lines with ANSI
# color sequences (only the Text View paints ANSI colors).
SCROLL_CASES = [
    ScrollCase("text", SCROLL_LOG_FILE, "text", "The Text View"),
    ScrollCase("text_highlighters", SCROLL_LOG_FILE, "text",
               "The Text View with a Highlighter Set of 5", highlighters=True),
    ScrollCase("text_ansi_hidden", SCROLL_ANSI_LOG_FILE, "text",
               "The Text View, ANSI color sequences hidden", ansi="hide"),
    ScrollCase("text_ansi_colors", SCROLL_ANSI_LOG_FILE, "text",
               "The Text View, ANSI colors shown", ansi="colors"),
    ScrollCase("table", SCROLL_LOG_FILE, "table", "The Table View"),
    ScrollCase("table_highlighters", SCROLL_LOG_FILE, "table",
               "The Table View with a Highlighter Set of 5", highlighters=True),
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(SCROLL_CASES))
def test_perf_gui_scroll(
    case: ScrollCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI scrolls a loaded Log File by line, by page and to the end: every frame's paint."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "scroll.json"
    p50, p99, longest = (f"{case.name}_frame_p50", f"{case.name}_frame_p99",
                         f"{case.name}_frame_max")
    over_budget, frame_counts, budget_ms = [], [], []

    def scroll() -> dict[str, float]:
        try:
            run = run_known_scenario(isolated_gui_module, "scroll", [filepath], report_path,
                                     options=case.options(), timeout=case.timeout)
        except ScenarioUnknown:
            pytest.skip("this logsquirl has no scroll scenario (#669)")
        assert run.process.returncode == EXIT_PASSED and run.report is not None, (
            run.process.stdout + run.process.stderr
        )
        results = run.report["results"]
        assert results["view"] == case.view and results["unmoved_step_count"] == 0
        frames = results["frame_time"]
        assert frames["count"] >= results["step_count"]
        over_budget.append(frames["over_budget_count"])
        frame_counts.append(frames["count"])
        budget_ms.append(frames["budget_ms"])
        return {p50: frames["p50_ms"] / 1000.0, p99: frames["p99_ms"] / 1000.0,
                longest: frames["max_ms"] / 1000.0}

    # Each run paints hundreds of frames: as few runs as a 1 GB case.
    results = measure_events(scroll, **_runs(bench_config, large=True))
    measured_runs = len(results[p50]["runs"])
    what = (f"{case.description} ({case.log_file}): a paint of its Viewport while scrolling "
            f"by line, by page and to the end, Log File loaded before")
    results[p50]["measures"] = f"{what}; the median frame of a run"
    results[p99]["measures"] = f"{what}; the 99th percentile frame of a run"
    results[longest]["measures"] = f"{what}; the longest frame of a run"
    # The warmup runs counted too: only the measured ones are reported.
    results[p99]["frames_over_budget"] = summarize_frames_over_budget(
        over_budget[-measured_runs:], frame_counts[-measured_runs:], budget_ms[-1]
    )
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# Startup: the one case timed around a whole process
# ---------------------------------------------------------------------------


@pytest.mark.performance
def test_perf_gui_startup(isolated_gui_module, baseline, collected_results, bench_config, request):
    """GUI startup time: the wall-clock of `logsquirl --version`, process start to exit."""

    def startup():
        isolated_gui_module.run("--version")

    result = measure_execution(startup, warmup=bench_config["warmup"], runs=bench_config["runs"])
    result["measures"] = "Process start to exit of `logsquirl --version` (wall-clock)"
    _record({"gui_startup_version": result}, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# Baseline update and report generation hook
# ---------------------------------------------------------------------------


def all_benchmark_names() -> set[str]:
    """Every benchmark the suite has; a new table of cases adds its names here."""
    names = {case.name for case in GREP_CASES}
    for case in GUI_OPEN_CASES:
        names |= {f"{case.name}_first_line", f"{case.name}_indexed"}
    for case in [*SEARCH_CASES, *QUICKFIND_CASES, *SCROLL_CASES]:
        names |= case.benchmark_names()
    names.add("gui_startup_version")
    return names



@pytest.fixture(scope="module", autouse=True)
def update_baseline_on_finish(request, collected_results, baseline, bench_config):
    """After all perf tests in this module, update baseline and generate report."""
    yield

    if not collected_results:
        return

    # Generate benchmark report
    system_info = collect_system_info()
    report_format = bench_config.get("report_format", "markdown")
    generate_benchmark_report(
        collected_results, baseline, system_info,
        report_format=report_format,
        bench_runs=bench_config["runs"],
        bench_warmup=bench_config["warmup"],
        not_measured=sorted(all_benchmark_names() - set(collected_results)),
    )

    # Update baseline if requested
    if request.config.getoption("--update-baseline"):
        bl = load_baseline()

        # Upgrade to schema v2
        bl["_meta"]["version"] = 2
        bl["_meta"]["config"] = {
            "warmup": bench_config["warmup"],
            "runs": bench_config["runs"],
            "outlier_method": "iqr_1.5",
        }
        bl["_meta"]["system"] = system_info

        for name, result in collected_results.items():
            bl["benchmarks"][name] = result

        from datetime import date
        bl["_meta"]["updated"] = date.today().isoformat()
        save_baseline(bl)
