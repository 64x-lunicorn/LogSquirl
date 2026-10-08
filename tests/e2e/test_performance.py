"""
Performance regression tests for LogSquirl ("Safari Rule").

Every benchmark reports what a user waits for, timed by the application at the
event itself (#667): the GUI cases run the benchmark mode (BUILD.md, "Benchmark
mode") and report when the first Log Line was displayed and when the Index was
finished, when the first Match of a Search was displayed and when it finished,
how long each keystroke of a QuickFind took to be marked (#668), how long
each frame of a scripted scroll took to paint in the Text View and the Table
View (#669), how long a Log Line appended to a followed Log File took to be
displayed and charted, how long a Session of several tabs took to restore
(#670), and how long the UI thread's reads of a Log File took while it was
indexed, with the wall and CPU time of the indexing (#686), and how long
Marks took to set and remove and the Filtered View's Log Lines to save
(#730); the grep cases run
logsquirl_grep with a benchmark report and time its Search from the open of
the Log File to the last match written. Neither
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
benchmark mode gets a table of its own, usually of a ScenarioCase, and a test
that turns one run's report into {benchmark name: seconds} for
measure_events(), like test_perf_gui_quickfind; _record() does the rest.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

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
    new_baseline_entry,
    save_baseline,
    summarize_chart_following,
    summarize_frames_over_budget,
    summarize_indexing_parallelism,
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


def _scenario_report(instance, scenario: str, ticket: str, log_files: list[Path],
                     report_path: Path, options: dict[str, str] | None = None,
                     timeout: float = 300.0) -> dict:
    """The report of one run of a scenario of the benchmark mode, which passed.

    A logsquirl without the scenario skips the case: the before side of the
    Benchmarks workflow runs this suite on the binaries of its own commit,
    which may predate the scenario's ticket.
    """
    try:
        run = run_known_scenario(instance, scenario, log_files, report_path,
                                 options=options, timeout=timeout)
    except ScenarioUnknown:
        pytest.skip(f"this logsquirl has no {scenario} scenario ({ticket})")
    assert run.process.returncode == EXIT_PASSED and run.report is not None, (
        run.process.stdout + run.process.stderr
    )
    return run.report


@dataclass(frozen=True)
class ScenarioCase:
    """A case of a scenario of the benchmark mode on a generated Log File.

    Its benchmarks are gui_<scenario>_<label>_<what>. A subclass names the
    scenario and the ticket that added it, and has a timeout (and its Log File)
    of its own.
    """

    SCENARIO: ClassVar[str]
    TICKET: ClassVar[str]

    label: str

    @property
    def name(self) -> str:
        return f"gui_{self.SCENARIO.replace('-', '_')}_{self.label}"

    @property
    def generated(self) -> bool:
        # Written by generate_test_data.py, or by the scenario itself: `slow`.
        return True

    def options(self) -> dict[str, str]:
        """The --benchmark-option values of a run."""
        return {}

    def run(self, instance, log_files: list[Path], report_path: Path) -> dict:
        """The report of one run of the case, which passed."""
        return _scenario_report(instance, self.SCENARIO, self.TICKET, log_files, report_path,
                                options=self.options(), timeout=self.timeout)


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
class SearchCase(ScenarioCase):
    # The benchmarks are gui_search_<label>_first_match (none for a Search
    # without a Match) and gui_search_<label>_finished.
    SCENARIO: ClassVar[str] = "search"
    TICKET: ClassVar[str] = "#668"

    log_file: str  # in test_data/, generated
    variant: SearchVariant
    large: bool = False
    timeout: float = 900

    def options(self) -> dict[str, str]:
        return self.variant.options()

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
        report = case.run(isolated_gui_module, [filepath], report_path)
        assert report["results"]["match_count"] == known, (
            f"the Search found {report['results']['match_count']} Matches, not {known}"
        )
        measured = {finished: seconds_since_scenario_start(report, "search_finished")}
        if first_match in case.benchmark_names():
            measured[first_match] = seconds_since_scenario_start(report, "first_match_displayed")
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
class QuickFindCase(ScenarioCase):
    # The benchmarks are gui_quickfind_<label>_keystroke_p50 and _p99: of one
    # run's keystrokes, the median and the 99th percentile of the time from a
    # keystroke to the Matches on screen marked.
    SCENARIO: ClassVar[str] = "quickfind"
    TICKET: ClassVar[str] = "#668"

    log_file: str  # in test_data/, generated
    pattern: str
    large: bool = False
    timeout: float = 900

    def options(self) -> dict[str, str]:
        return {"pattern": self.pattern}

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
        report = case.run(isolated_gui_module, [filepath], report_path)
        latency = report["results"]["keystroke_latency"]
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
class ScrollCase(ScenarioCase):
    # The benchmarks are gui_scroll_<label>_frame_p50, _frame_p99 and
    # _frame_max: of one run's frames -- every paint of the view's Viewport
    # while the script scrolls -- the median, the 99th percentile and the
    # longest. _frame_p99 also carries the frames over budget of each run.
    SCENARIO: ClassVar[str] = "scroll"
    TICKET: ClassVar[str] = "#669"

    log_file: str  # in test_data/, generated
    view: str  # text or table
    description: str
    highlighters: bool = False
    # The setting "ANSI color sequences": text, hide or colors.
    ansi: str = "text"
    timeout: float = 600

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
        results = case.run(isolated_gui_module, [filepath], report_path)["results"]
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
# GUI: the benchmark mode's follow scenario, a Log File growing while followed
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class FollowCase(ScenarioCase):
    # The benchmarks are gui_follow_<label>_display_p50 and _display_p99, of one
    # run's appended Log Lines the median and the 99th percentile time from the
    # append to the Log Line displayed in the Text View, and _chart_p99, the
    # 99th percentile time to the Log Line charted; _chart_p99 also says
    # whether the chart kept up with the Text View in each run. The scenario
    # writes its Log File itself; the case is slow for its duration.
    SCENARIO: ClassVar[str] = "follow"
    TICKET: ClassVar[str] = "#670"

    lines_per_second: int
    description: str
    duration_ms: int = 5000
    timeout: float = 120

    def options(self) -> dict[str, str]:
        return {"lines_per_second": str(self.lines_per_second),
                "duration_ms": str(self.duration_ms)}

    def benchmark_names(self) -> set[str]:
        return {f"{self.name}_display_p50", f"{self.name}_display_p99", f"{self.name}_chart_p99"}


# A Log File a service writes now and then, and one a busy service floods.
FOLLOW_CASES = [
    FollowCase("10_per_s", 10, "10 Log Lines a second"),
    FollowCase("1000_per_s", 1000, "1000 Log Lines a second"),
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(FOLLOW_CASES))
def test_perf_gui_follow(
    case: FollowCase, isolated_gui_module, tmp_path, baseline, collected_results, bench_config,
    request,
):
    """The GUI follows a growing Log File: each appended Log Line until displayed and charted."""
    report_path = tmp_path / "follow.json"
    p50, p99, chart = (f"{case.name}_display_p50", f"{case.name}_display_p99",
                       f"{case.name}_chart_p99")
    kept_up, behind_p99, budget_ms = [], [], []

    def follow() -> dict[str, float]:
        results = case.run(isolated_gui_module, [], report_path)["results"]
        expected = case.lines_per_second * case.duration_ms // 1000
        assert results["appended_count"] == expected
        assert results["display_latency"]["count"] == expected
        kept_up.append(results["chart_kept_up"])
        behind_p99.append(results["chart_behind_display"]["p99_ms"])
        budget_ms.append(results["chart_budget_ms"])
        return {p50: results["display_latency"]["p50_ms"] / 1000.0,
                p99: results["display_latency"]["p99_ms"] / 1000.0,
                chart: results["chart_latency"]["p99_ms"] / 1000.0}

    # Each run takes its duration and times every Log Line appended in it.
    results = measure_events(follow, **_runs(bench_config, large=True))
    measured_runs = len(results[p50]["runs"])
    what = (f"A Log File growing by {case.description} for {case.duration_ms / 1000:g} s, "
            f"followed, with a chart: from the append of a Log Line")
    results[p50]["measures"] = f"{what} to its display in the Text View; median of a run"
    results[p99]["measures"] = f"{what} to its display in the Text View; 99th percentile of a run"
    results[chart]["measures"] = f"{what} to the chart showing it; 99th percentile of a run"
    # The warmup runs counted too: only the measured ones are reported.
    results[chart]["chart_following"] = summarize_chart_following(
        kept_up[-measured_runs:], behind_p99[-measured_runs:], budget_ms[-1]
    )
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's session-restore scenario
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SessionRestoreCase:
    # The benchmarks are gui_session_restore_<label>_current_tab_usable, the
    # restore to the tab in front usable, and _all_tabs_indexed, to the Index
    # of the last tab finished; with Kept Searches also _all_tabs_restored, to
    # the last of them finished.
    label: str
    log_files: tuple[str, ...]  # in test_data/, a tab each, in order
    current: int
    description: str
    generated: bool = False
    timeout: float = 300
    # Kept Searches saved with each tab, which run again beside the Indexes of
    # the tabs still loading (#704). The cases without them measure what their
    # Budgets were derived from, so their spread stays that of the restore
    # alone (#776).
    searches: int = 0

    @property
    def name(self) -> str:
        return f"gui_session_restore_{self.label}"

    def benchmark_names(self) -> set[str]:
        names = {f"{self.name}_current_tab_usable", f"{self.name}_all_tabs_indexed"}
        if self.searches:
            names.add(f"{self.name}_all_tabs_restored")
        return names


SESSION_RESTORE_CASES = [
    SessionRestoreCase("small", ("random_block_1Mb.txt", "random_block_1.5Mb.txt",
                                 "random_block_512k.txt"), 0,
                       "3 tabs of 0.5 to 1.5 MB, the 1 MB one in front"),
    SessionRestoreCase("log_220mb", (SCROLL_ANSI_LOG_FILE, "generated_100Mb.log", SCROLL_LOG_FILE), 1,
                       "3 tabs of 20 to 100 MB, a 100 MB Log File in front", generated=True,
                       timeout=600),
    SessionRestoreCase("log_220mb_kept_searches",
                       (SCROLL_ANSI_LOG_FILE, "generated_100Mb.log", SCROLL_LOG_FILE), 1,
                       "3 tabs of 20 to 100 MB, a 100 MB Log File in front, each with 3 Kept Searches",
                       generated=True, timeout=600, searches=3),
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(SESSION_RESTORE_CASES))
def test_perf_gui_session_restore(
    case: SessionRestoreCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI restores a Session of several tabs: the tab in front usable, every tab indexed."""
    log_files = [_log_file(test_data_dir, name, case.generated) for name in case.log_files]
    report_path = tmp_path / "session-restore.json"
    usable, indexed = f"{case.name}_current_tab_usable", f"{case.name}_all_tabs_indexed"
    restored = f"{case.name}_all_tabs_restored"

    def restore() -> dict[str, float]:
        report = _scenario_report(isolated_gui_module, "session-restore", "#670", log_files,
                                  report_path, options={"current": str(case.current),
                                                        "searches": str(case.searches)},
                                  timeout=case.timeout)
        assert report["results"]["tab_count"] == len(log_files)
        seconds = {usable: seconds_since_scenario_start(report, "current_tab_usable"),
                   indexed: seconds_since_scenario_start(report, "all_tabs_indexed")}
        if case.searches:
            seconds[restored] = seconds_since_scenario_start(report, "all_tabs_restored")
        return seconds

    results = measure_events(restore, **_runs(bench_config, large=case.generated))
    what = f"A Session of {case.description}, restored"
    results[usable]["measures"] = f"{what}: to the tab in front indexed and painted"
    results[indexed]["measures"] = f"{what}: to the Index of every tab finished"
    if case.searches:
        results[restored]["measures"] = f"{what}: to the last Kept Search of every tab finished"
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's read-while-indexing scenario
# ---------------------------------------------------------------------------

# The reads of read-while-indexing, by the label their benchmarks carry.
INDEXING_READS = {
    "nb_line": ("nb_line_latency", "getNbLine"),
    "line_string": ("line_string_latency", "getLineString of one Log Line"),
    "expanded_lines": ("expanded_lines_latency", "getExpandedLines of 60 Log Lines"),
}


@dataclass(frozen=True)
class ReadWhileIndexingCase(ScenarioCase):
    # The benchmarks are gui_read_while_indexing_<label>_<read>_p50, _p99 and
    # _max for each read of INDEXING_READS: of one run's reads, the median, the
    # 99th percentile and the longest; and _index_wall and _index_cpu, the wall
    # time of the indexing and the process's CPU time in it. _index_wall also
    # carries the parallelism, CPU over wall time, of each run.
    SCENARIO: ClassVar[str] = "read-while-indexing"
    TICKET: ClassVar[str] = "#686"

    log_file: str  # in test_data/, generated
    large: bool = False
    timeout: float = 900

    def benchmark_names(self) -> set[str]:
        names = {f"{self.name}_index_wall", f"{self.name}_index_cpu"}
        for read in INDEXING_READS:
            names |= {f"{self.name}_{read}_{stat}" for stat in ("p50", "p99", "max")}
        return names


# Read every 2 ms, 60 Log Lines a screen: the scenario's defaults.
READ_WHILE_INDEXING_CASES = [
    ReadWhileIndexingCase(label, log_file, large=large, timeout=1800 if large else 900)
    for label, (log_file, large) in GENERATED_LOG_FILES.items()
]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(READ_WHILE_INDEXING_CASES))
def test_perf_gui_read_while_indexing(
    case: ReadWhileIndexingCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The UI thread reads a Log File while it is indexed: each read, and the indexing's CPU use."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "read_while_indexing.json"
    wall, cpu = f"{case.name}_index_wall", f"{case.name}_index_cpu"
    parallelism = []

    def read_while_indexing() -> dict[str, float]:
        results = case.run(isolated_gui_module, [filepath], report_path)["results"]
        measured = {}
        for read, (result, _) in INDEXING_READS.items():
            latency = results[result]
            assert latency["count"] > 0
            measured[f"{case.name}_{read}_p50"] = latency["p50_ms"] / 1000.0
            measured[f"{case.name}_{read}_p99"] = latency["p99_ms"] / 1000.0
            measured[f"{case.name}_{read}_max"] = latency["max_ms"] / 1000.0
        indexing = results["indexing"]
        measured[wall] = indexing["wall_ms"] / 1000.0
        # The CPU time is told on Linux, macOS and Windows.
        measured[cpu] = indexing["cpu_ms"] / 1000.0
        parallelism.append(indexing["parallelism"])
        return measured

    results = measure_events(read_while_indexing, **_runs(bench_config, case.large))
    measured_runs = len(results[wall]["runs"])
    what = f"While {case.log_file} is indexed, read from the UI thread every 2 ms"
    for read, (_, call) in INDEXING_READS.items():
        results[f"{case.name}_{read}_p50"]["measures"] = f"{what}: {call}, the median of a run"
        results[f"{case.name}_{read}_p99"]["measures"] = (
            f"{what}: {call}, the 99th percentile of a run"
        )
        results[f"{case.name}_{read}_max"]["measures"] = f"{what}: {call}, the longest of a run"
    results[wall]["measures"] = (
        f"{what}: the indexing, from the request to open it to its Index finished"
    )
    results[cpu]["measures"] = (
        f"{what}: the process's CPU time during the indexing, every thread together"
    )
    # The warmup runs counted too: only the measured ones are reported.
    results[wall]["indexing_parallelism"] = summarize_indexing_parallelism(
        parallelism[-measured_runs:]
    )
    results[wall]["throughput"] = calculate_throughput(filepath, results[wall]["median_seconds"])
    _record(results, collected_results, baseline, request)


# ---------------------------------------------------------------------------
# GUI: the benchmark mode's save scenario, Marks and the Filtered View saved
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SaveCase(ScenarioCase):
    # The benchmarks are gui_save_<label>_marks_added, _saved and
    # _marks_removed: the Marks set, the Filtered View's Log Lines saved after
    # them, and the Marks removed after that. It is also what a profile-guided
    # build trains Marks and saving on (#730).
    SCENARIO: ClassVar[str] = "save"
    TICKET: ClassVar[str] = "#730"

    log_file: str  # in test_data/, generated
    pattern: str
    marks: int = 200
    timeout: float = 900

    def options(self) -> dict[str, str]:
        return {"pattern": self.pattern, "marks": str(self.marks)}

    def benchmark_names(self) -> set[str]:
        return {f"{self.name}_marks_added", f"{self.name}_saved", f"{self.name}_marks_removed"}


# "slow response" is in every WARN Log Line, one in 13: about 100,000 of the
# 100 MB Log File, as many as the micro-benchmark saves.
SAVE_CASES = [SaveCase("log_100mb", GENERATED_LOG_FILES["log_100mb"][0], "slow response")]


@pytest.mark.performance
@pytest.mark.parametrize("case", _cases(SAVE_CASES))
def test_perf_gui_save(
    case: SaveCase, isolated_gui_module, test_data_dir, tmp_path, baseline,
    collected_results, bench_config, request,
):
    """The GUI marks Log Lines, saves the Filtered View's Log Lines and removes the Marks."""
    filepath = _log_file(test_data_dir, case.log_file, case.generated)
    report_path = tmp_path / "save.json"
    added, saved, removed = (f"{case.name}_marks_added", f"{case.name}_saved",
                             f"{case.name}_marks_removed")

    def mark_and_save() -> dict[str, float]:
        report = case.run(isolated_gui_module, [filepath], report_path)
        assert report["results"]["saved_line_count"] >= report["results"]["match_count"] > 0
        marked = seconds_since_scenario_start(report, "marks_added")
        written = seconds_since_scenario_start(report, "saved")
        return {added: marked, saved: written - marked,
                removed: seconds_since_scenario_start(report, "marks_removed") - written}

    results = measure_events(mark_and_save, **_runs(bench_config, False))
    what = f"Searched for '{case.pattern}' in {case.log_file}, Search finished before"
    results[added]["measures"] = (
        f"{what}: {case.marks} Marks set, each with the Text View's Mark action"
    )
    results[saved]["measures"] = (
        f"{what}: the Filtered View's Matches and Marks selected and saved to a file"
    )
    results[removed]["measures"] = f"{what}: the {case.marks} Marks removed again"
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
    for case in [*SEARCH_CASES, *QUICKFIND_CASES, *SCROLL_CASES, *FOLLOW_CASES,
                 *SESSION_RESTORE_CASES, *READ_WHILE_INDEXING_CASES, *SAVE_CASES]:
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
            bl["benchmarks"][name] = new_baseline_entry(bl["benchmarks"].get(name), result)

        from datetime import date
        bl["_meta"]["updated"] = date.today().isoformat()
        save_baseline(bl)
