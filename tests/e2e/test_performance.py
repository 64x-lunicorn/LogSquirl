"""
Performance regression tests for LogSquirl ("Safari Rule").

Each benchmark measures execution time and compares against baseline.json.
If measured time exceeds baseline by more than the configured tolerance (default 5%),
the test fails with a clear regression message.

Run with --update-baseline to save new measurements as the baseline.

Methodology:
  - 1 warmup run (discarded)
  - 5 measured runs
  - Median and P95 are recorded
  - Comparison uses median against baseline median + tolerance
"""

import pytest

from conftest import (
    assert_performance,
    grep_output_lines,
    load_baseline,
    measure_execution,
    run_grep,
    run_gui,
    save_baseline,
)


@pytest.fixture(scope="module")
def baseline():
    return load_baseline()


@pytest.fixture(scope="module")
def collected_results():
    """Collect all benchmark results in a module-scoped dict for batch baseline update."""
    return {}


# ---------------------------------------------------------------------------
# Grep performance benchmarks
# ---------------------------------------------------------------------------


@pytest.mark.performance
class TestGrepPerformance:
    """Performance benchmarks for logsquirl_grep."""

    def test_perf_grep_1mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results, request
    ):
        """Benchmark: simple pattern search in 1MB file."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath)

        result = measure_execution(search)
        collected_results["grep_search_1mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_simple", result, baseline)

    def test_perf_grep_1mb_regex(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results, request
    ):
        """Benchmark: complex regex search in 1MB file."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, r"[A-Z]{5,}\+[a-z]{3,}", filepath)

        result = measure_execution(search)
        collected_results["grep_search_1mb_regex"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_regex", result, baseline)

    def test_perf_grep_1_5mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results, request
    ):
        """Benchmark: simple search in 1.5MB file."""
        filepath = test_data_dir / "random_block_1.5Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath)

        result = measure_execution(search)
        collected_results["grep_search_1_5mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1_5mb_simple", result, baseline)

    def test_perf_grep_utf16_1mb(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results, request
    ):
        """Benchmark: search in 1MB UTF-16LE file (encoding overhead)."""
        filepath = test_data_dir / "random_block_1Mb_utf16le.txt"

        def search():
            run_grep(logsquirl_grep_binary, "test", filepath)

        result = measure_execution(search)
        collected_results["grep_search_utf16_1mb"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_utf16_1mb", result, baseline)


# ---------------------------------------------------------------------------
# GUI performance benchmarks
# ---------------------------------------------------------------------------


@pytest.mark.performance
class TestGuiPerformance:
    """Performance benchmarks for the LogSquirl GUI."""

    def test_perf_gui_startup(
        self, logsquirl_binary, baseline, collected_results, request
    ):
        """Benchmark: GUI startup time (--version flag, measures process init)."""

        def startup():
            run_gui(logsquirl_binary, ["--version"])

        result = measure_execution(startup)
        collected_results["gui_startup_version"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("gui_startup_version", result, baseline)

    def test_perf_gui_load_1mb(
        self, logsquirl_binary, test_data_dir, baseline, collected_results, request
    ):
        """Benchmark: GUI loading a 1MB file (process start + file load + exit)."""
        import subprocess
        import time

        filepath = test_data_dir / "random_block_1Mb.txt"

        def load_file():
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n", str(filepath)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            time.sleep(2)
            proc.terminate()
            proc.wait(timeout=5)

        result = measure_execution(load_file, warmup=1, runs=3)
        collected_results["gui_load_1mb"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("gui_load_1mb", result, baseline)


# ---------------------------------------------------------------------------
# Baseline update hook
# ---------------------------------------------------------------------------


def pytest_sessionfinish_update_baseline(session, collected_results):
    """Update baseline if --update-baseline was passed."""
    if not session.config.getoption("--update-baseline", default=False):
        return
    if not collected_results:
        return

    baseline = load_baseline()
    for name, result in collected_results.items():
        baseline["benchmarks"][name] = result

    from datetime import date
    baseline["_meta"]["updated"] = date.today().isoformat()
    save_baseline(baseline)


@pytest.fixture(scope="module", autouse=True)
def update_baseline_on_finish(request, collected_results):
    """After all perf tests in this module, update baseline if requested."""
    yield
    if request.config.getoption("--update-baseline"):
        baseline = load_baseline()
        for name, result in collected_results.items():
            baseline["benchmarks"][name] = result

        from datetime import date
        baseline["_meta"]["updated"] = date.today().isoformat()
        save_baseline(baseline)
