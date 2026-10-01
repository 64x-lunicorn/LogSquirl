"""
Performance regression tests for LogSquirl ("Safari Rule").

Every benchmark reports what a user waits for, timed by the application at the
event itself (#667): the GUI cases run the benchmark mode (BUILD.md, "Benchmark
mode") and report when the first Log Line was displayed and when the Index was
finished; the grep cases run logsquirl_grep with a benchmark report and time
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
    run_benchmark,
    run_grep_benchmark,
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
)

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
