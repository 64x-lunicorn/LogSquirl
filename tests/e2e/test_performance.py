"""
Performance regression tests for LogSquirl ("Safari Rule").

Each benchmark measures execution time and compares against baseline.json.
If measured time exceeds baseline by more than the configured tolerance (default 5%),
the test fails — but only when Welch's t-test confirms statistical significance.

Run with --update-baseline to save new measurements as the baseline.

Methodology:
  - 3 warmup runs (discarded, configurable via --bench-warmup)
  - 21 measured runs (configurable via --bench-runs)
  - IQR-based outlier filtering (1.5× IQR)
  - Full statistics: median, mean, std, CV%, P5, P95, IQR
  - Regression detection: median + tolerance AND Welch's t-test (p < 0.05)
  - Auto-generated benchmark report (Markdown or JSON)
"""

import subprocess
import time
from pathlib import Path

import pytest

from conftest import (
    assert_performance,
    calculate_throughput,
    collect_system_info,
    generate_benchmark_report,
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


@pytest.fixture(scope="module")
def bench_config(request):
    """Get benchmark configuration from CLI options."""
    return {
        "runs": request.config.getoption("--bench-runs"),
        "warmup": request.config.getoption("--bench-warmup"),
        "report_format": request.config.getoption("--bench-report"),
    }


def _generated_file(test_data_dir: Path, filename: str) -> Path:
    """Get path to a generated test data file, skip if not available."""
    filepath = test_data_dir / filename
    if not filepath.exists():
        pytest.skip(
            f"{filename} not found. Run: python tests/e2e/generate_test_data.py"
        )
    return filepath


# ---------------------------------------------------------------------------
# Grep performance benchmarks
# ---------------------------------------------------------------------------


@pytest.mark.performance
class TestGrepPerformance:
    """Performance benchmarks for logsquirl_grep."""

    def test_perf_grep_1mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: simple pattern search in 1MB file."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_simple", result, baseline)

    def test_perf_grep_1mb_regex(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: complex regex search in 1MB file."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, r"[A-Z]{5,}\+[a-z]{3,}", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1mb_regex"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_regex", result, baseline)

    def test_perf_grep_1_5mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: simple search in 1.5MB file."""
        filepath = test_data_dir / "random_block_1.5Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1_5mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1_5mb_simple", result, baseline)

    def test_perf_grep_utf16_1mb(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: search in 1MB UTF-16LE file (encoding overhead)."""
        filepath = test_data_dir / "random_block_1Mb_utf16le.txt"

        def search():
            run_grep(logsquirl_grep_binary, "test", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_utf16_1mb"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_utf16_1mb", result, baseline)

    def test_perf_grep_1mb_no_match(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: pattern with no matches in 1MB file (scan-only overhead)."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZZ_NEVER_MATCH_99999", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1mb_no_match"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_no_match", result, baseline)

    def test_perf_grep_1mb_alternation(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: regex alternation pattern in 1MB file (common real-world pattern)."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "ERROR|WARNING|CRITICAL", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1mb_alternation"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_alternation", result, baseline)

    def test_perf_grep_1mb_case_insensitive(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: case-insensitive regex in 1MB file (tests HS_FLAG_CASELESS)."""
        filepath = test_data_dir / "random_block_1Mb.txt"

        def search():
            run_grep(logsquirl_grep_binary, "(?i)zzzz", filepath)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_1mb_case_insensitive"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_1mb_case_insensitive", result, baseline)


# ---------------------------------------------------------------------------
# Large file grep benchmarks (require generated test data)
# ---------------------------------------------------------------------------


@pytest.mark.performance
@pytest.mark.slow
class TestGrepLargeFilePerformance:
    """Performance benchmarks for large files (10-100 MB). Requires generated test data."""

    def test_perf_grep_10mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: simple pattern search in 10MB file (real throughput measurement)."""
        filepath = _generated_file(test_data_dir, "random_block_10Mb.txt")

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath, timeout=120)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_10mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_10mb_simple", result, baseline)

    def test_perf_grep_10mb_regex(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: complex regex search in 10MB file."""
        filepath = _generated_file(test_data_dir, "random_block_10Mb.txt")

        def search():
            run_grep(logsquirl_grep_binary, r"[A-Z]{5,}\+[a-z]{3,}", filepath, timeout=120)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_10mb_regex"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_10mb_regex", result, baseline)

    def test_perf_grep_50mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: simple pattern search in 50MB file (L2/L3 cache boundary test)."""
        filepath = _generated_file(test_data_dir, "random_block_50Mb.txt")

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath, timeout=300)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_50mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_50mb_simple", result, baseline)

    def test_perf_grep_100mb_simple(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: simple pattern search in 100MB file (stress test)."""
        filepath = _generated_file(test_data_dir, "random_block_100Mb.txt")

        def search():
            run_grep(logsquirl_grep_binary, "ZZZZ", filepath, timeout=600)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_100mb_simple"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_100mb_simple", result, baseline)

    def test_perf_grep_utf16_10mb(
        self, logsquirl_grep_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: search in 10MB UTF-16LE file (encoding overhead at scale)."""
        filepath = _generated_file(test_data_dir, "random_block_10Mb_utf16le.txt")

        def search():
            run_grep(logsquirl_grep_binary, "test", filepath, timeout=120)

        result = measure_execution(search, warmup=bench_config["warmup"], runs=bench_config["runs"])
        result["throughput"] = calculate_throughput(filepath, result["median_seconds"])
        collected_results["grep_search_utf16_10mb"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("grep_search_utf16_10mb", result, baseline)


# ---------------------------------------------------------------------------
# GUI performance benchmarks
# ---------------------------------------------------------------------------


@pytest.mark.performance
class TestGuiPerformance:
    """Performance benchmarks for the LogSquirl GUI."""

    def test_perf_gui_startup(
        self, logsquirl_binary, baseline, collected_results, bench_config, request,
    ):
        """Benchmark: GUI startup time (--version flag, measures process init)."""

        def startup():
            run_gui(logsquirl_binary, ["--version"])

        result = measure_execution(startup, warmup=bench_config["warmup"], runs=bench_config["runs"])
        collected_results["gui_startup_version"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("gui_startup_version", result, baseline)

    def test_perf_gui_load_1mb(
        self, logsquirl_binary, test_data_dir, baseline, collected_results,
        bench_config, request,
    ):
        """Benchmark: GUI loading a 1MB file (process start + file load + exit)."""
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

        # GUI load tests use fewer runs due to the sleep overhead
        gui_runs = min(bench_config["runs"], 7)
        result = measure_execution(load_file, warmup=1, runs=gui_runs)
        collected_results["gui_load_1mb"] = result

        if not request.config.getoption("--update-baseline"):
            assert_performance("gui_load_1mb", result, baseline)


# ---------------------------------------------------------------------------
# Baseline update and report generation hook
# ---------------------------------------------------------------------------


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
