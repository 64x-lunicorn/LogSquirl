"""
Shared fixtures and helpers for LogSquirl E2E tests.

Provides binary paths, test data access, subprocess runners,
and performance baseline comparison utilities with full statistical analysis.
"""

import json
import math
import os
import platform
import shutil
import subprocess
import tempfile
import time
from datetime import date
from pathlib import Path
from statistics import mean, median, stdev

import pytest

# ---------------------------------------------------------------------------
# CLI options
# ---------------------------------------------------------------------------

def pytest_addoption(parser):
    parser.addoption(
        "--binary-dir",
        action="store",
        default=None,
        help="Path to directory containing logsquirl binaries (default: auto-detect from build/output)",
    )
    parser.addoption(
        "--update-baseline",
        action="store_true",
        default=False,
        help="Update performance baselines with measured values instead of comparing",
    )
    parser.addoption(
        "--bench-runs",
        action="store",
        type=int,
        default=21,
        help="Number of measured runs per benchmark (default: 21)",
    )
    parser.addoption(
        "--bench-warmup",
        action="store",
        type=int,
        default=3,
        help="Number of warmup runs before measurement (default: 3)",
    )
    parser.addoption(
        "--bench-report",
        action="store",
        default="markdown",
        choices=["markdown", "json", "none"],
        help="Benchmark report format (default: markdown)",
    )


# ---------------------------------------------------------------------------
# Path fixtures
# ---------------------------------------------------------------------------

def _find_repo_root() -> Path:
    """Walk up from this file to find the repository root (contains CMakeLists.txt)."""
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "test_data").is_dir():
            return current
        current = current.parent
    raise RuntimeError("Could not find repository root")


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return _find_repo_root()


@pytest.fixture(scope="session")
def binary_dir(request, repo_root) -> Path:
    override = request.config.getoption("--binary-dir")
    if override:
        p = Path(override).resolve()
    else:
        p = repo_root / "build" / "output"
    if not p.is_dir():
        pytest.skip(f"Binary directory not found: {p}")
    return p


@pytest.fixture(scope="session")
def logsquirl_grep_binary(binary_dir) -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    binary = binary_dir / f"logsquirl_grep{suffix}"
    if not binary.exists():
        pytest.skip(f"logsquirl_grep not found at {binary}")
    return binary


@pytest.fixture(scope="session")
def logsquirl_binary(binary_dir) -> Path:
    system = platform.system()
    if system == "Darwin":
        binary = binary_dir / "logsquirl.app" / "Contents" / "MacOS" / "logsquirl"
    elif system == "Windows":
        binary = binary_dir / "logsquirl.exe"
    else:
        binary = binary_dir / "logsquirl"
    if not binary.exists():
        pytest.skip(f"logsquirl not found at {binary}")
    return binary


@pytest.fixture(scope="session")
def test_data_dir(repo_root) -> Path:
    p = repo_root / "test_data"
    if not p.is_dir():
        pytest.skip(f"test_data directory not found: {p}")
    return p


@pytest.fixture(scope="session")
def compressed_test_files(test_data_dir, tmp_path_factory) -> dict[str, Path]:
    """
    Generate compressed variants of the 1 MB random-block test file.

    Returns a dict mapping format name ("gz", "bz2", "xz", "zst", "lz4")
    to the path of the compressed file.  Skips formats whose CLI tool is
    not available on the system.
    """
    source = test_data_dir / "random_block_1Mb.txt"
    if not source.exists():
        pytest.skip(f"Test file not found: {source}")

    out_dir = tmp_path_factory.mktemp("compressed")
    result: dict[str, Path] = {}

    # (format_name, file_suffix, compress_command_builder)
    formats = [
        ("gz", ".gz", lambda src, dst: ["gzip", "-k", "-c"]),
        ("bz2", ".bz2", lambda src, dst: ["bzip2", "-k", "-c"]),
        ("xz", ".xz", lambda src, dst: ["xz", "-k", "-c"]),
        ("zst", ".zst", lambda src, dst: ["zstd", "-q", "-c"]),
        ("lz4", ".lz4", lambda src, dst: ["lz4", "-q", "-c"]),
    ]

    for name, suffix, cmd_builder in formats:
        tool = cmd_builder(source, None)[0]
        if not shutil.which(tool):
            continue

        dest = out_dir / f"random_block_1Mb.txt{suffix}"
        cmd = cmd_builder(source, dest)
        with open(source, "rb") as fin, open(dest, "wb") as fout:
            subprocess.run(cmd, stdin=fin, stdout=fout, check=True)

        if dest.stat().st_size > 0:
            result[name] = dest

    if not result:
        pytest.skip("No compression tools available")

    return result


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

def run_grep(binary: Path, pattern: str, filepath: Path, timeout: int = 30) -> subprocess.CompletedProcess:
    """Run logsquirl_grep and return the completed process."""
    return subprocess.run(
        [str(binary), "-e", pattern, str(filepath)],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def run_gui(binary: Path, args: list[str], timeout: int = 10) -> subprocess.CompletedProcess:
    """Run logsquirl GUI with given args (adds -platform offscreen on non-macOS)."""
    cmd = [str(binary)] + args
    system = platform.system()
    if system != "Darwin":
        cmd.extend(["-platform", "offscreen"])
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def grep_output_lines(result: subprocess.CompletedProcess) -> list[str]:
    """Extract matched lines from grep output, filtering internal log messages."""
    lines = result.stdout.strip().splitlines() if result.stdout.strip() else []
    # Filter out internal logging lines (contain "[IndexOperation::doIndex" or similar)
    return [l for l in lines if "[IndexOperation::" not in l]


# ---------------------------------------------------------------------------
# Performance baseline utilities
# ---------------------------------------------------------------------------

_BASELINE_DIR = Path(__file__).resolve().parent


def _baseline_path() -> Path:
    """Return the OS-specific baseline file, falling back to the default."""
    import platform

    if platform.system() == "Windows":
        win_path = _BASELINE_DIR / "baseline-windows.json"
        if win_path.exists():
            return win_path
    return _BASELINE_DIR / "baseline.json"


_BASELINE_PATH = _baseline_path()


def load_baseline() -> dict:
    with open(_BASELINE_PATH) as f:
        return json.load(f)


def save_baseline(data: dict):
    with open(_BASELINE_PATH, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def measure_execution(func, warmup: int = 3, runs: int = 21) -> dict:
    """
    Run func() multiple times and return comprehensive timing statistics.

    Performs IQR-based outlier filtering and returns median, mean, std, CV%,
    percentiles, and raw run data for statistical comparison.

    Args:
        func: callable to benchmark (no arguments)
        warmup: number of warmup runs (discarded)
        runs: number of measured runs

    Returns:
        dict with full statistical summary and raw timings
    """
    # Warmup
    for _ in range(warmup):
        func()

    times = []
    for _ in range(runs):
        start = time.perf_counter()
        func()
        elapsed = time.perf_counter() - start
        times.append(elapsed)

    times.sort()
    n = len(times)

    # IQR-based outlier filtering
    q1_idx = n // 4
    q3_idx = (3 * n) // 4
    q1 = times[q1_idx]
    q3 = times[q3_idx]
    iqr = q3 - q1
    lower_fence = q1 - 1.5 * iqr
    upper_fence = q3 + 1.5 * iqr
    filtered = [t for t in times if lower_fence <= t <= upper_fence]

    if len(filtered) < 3:
        # Not enough data after filtering — use all runs
        filtered = times

    f_median = median(filtered)
    f_mean = mean(filtered)
    f_std = stdev(filtered) if len(filtered) > 1 else 0.0
    f_cv = (f_std / f_mean * 100) if f_mean > 0 else 0.0

    f_sorted = sorted(filtered)
    fn = len(f_sorted)
    p5_idx = max(0, int(fn * 0.05))
    p95_idx = min(fn - 1, int(fn * 0.95))

    return {
        "median_seconds": round(f_median, 6),
        "mean_seconds": round(f_mean, 6),
        "std_seconds": round(f_std, 6),
        "cv_percent": round(f_cv, 2),
        "p5_seconds": round(f_sorted[p5_idx], 6),
        "p95_seconds": round(f_sorted[p95_idx], 6),
        "iqr_seconds": round(iqr, 6),
        "min_seconds": round(f_sorted[0], 6),
        "max_seconds": round(f_sorted[-1], 6),
        "runs": [round(t, 6) for t in times],
        "filtered_count": len(filtered),
        "total_count": n,
    }


def _welch_t_test(sample_a: list[float], sample_b: list[float]) -> float:
    """
    Perform Welch's t-test and return the approximate two-tailed p-value.

    Uses the t-distribution approximation via the Welch-Satterthwaite equation.
    Returns 1.0 if either sample is too small (< 3) or has zero variance.
    """
    na, nb = len(sample_a), len(sample_b)
    if na < 3 or nb < 3:
        return 1.0

    mean_a, mean_b = mean(sample_a), mean(sample_b)
    var_a = stdev(sample_a) ** 2
    var_b = stdev(sample_b) ** 2

    if var_a == 0 and var_b == 0:
        return 1.0

    se = math.sqrt(var_a / na + var_b / nb)
    if se == 0:
        return 1.0

    t_stat = abs(mean_a - mean_b) / se

    # Welch-Satterthwaite degrees of freedom
    num = (var_a / na + var_b / nb) ** 2
    denom = (var_a / na) ** 2 / (na - 1) + (var_b / nb) ** 2 / (nb - 1)
    if denom == 0:
        return 1.0
    df = num / denom

    # Approximate p-value using the incomplete beta function regularized form.
    # For large df, t approaches normal; use a simple approximation.
    x = df / (df + t_stat ** 2)
    # Regularized incomplete beta function approximation via continued fraction
    p_value = _betai(df / 2.0, 0.5, x)
    return p_value


def _betai(a: float, b: float, x: float) -> float:
    """Regularized incomplete beta function I_x(a, b) via continued fraction."""
    if x < 0.0 or x > 1.0:
        return 1.0
    if x == 0.0 or x == 1.0:
        return x

    # Use the log-beta for numerical stability
    ln_beta = math.lgamma(a) + math.lgamma(b) - math.lgamma(a + b)
    front = math.exp(math.log(x) * a + math.log(1 - x) * b - ln_beta)

    # Lentz's continued fraction algorithm
    if x < (a + 1.0) / (a + b + 2.0):
        return front * _betacf(a, b, x) / a
    else:
        return 1.0 - front * _betacf(b, a, 1.0 - x) / b


def _betacf(a: float, b: float, x: float) -> float:
    """Continued fraction for incomplete beta function."""
    max_iter = 200
    eps = 1e-12
    qab = a + b
    qap = a + 1.0
    qam = a - 1.0

    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < 1e-30:
        d = 1e-30
    d = 1.0 / d
    h = d

    for m in range(1, max_iter + 1):
        m2 = 2 * m
        # Even step
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-30:
            d = 1e-30
        c = 1.0 + aa / c
        if abs(c) < 1e-30:
            c = 1e-30
        d = 1.0 / d
        h *= d * c

        # Odd step
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-30:
            d = 1e-30
        c = 1.0 + aa / c
        if abs(c) < 1e-30:
            c = 1e-30
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < eps:
            break

    return h


def assert_performance(benchmark_name: str, measured: dict, baseline: dict):
    """
    Assert that measured performance does not exceed baseline by more than tolerance.

    Uses two criteria:
    1. Median must not exceed baseline median + tolerance (existing check)
    2. If baseline has raw runs, Welch's t-test must show p < 0.05 for the
       regression to be statistically significant (both conditions required)

    Emits a pytest warning if the benchmark is noisy (CV% > 15%).
    """
    entry = baseline.get("benchmarks", {}).get(benchmark_name)
    if not entry or entry.get("median_seconds") is None:
        return  # No baseline yet — skip comparison

    tolerance = baseline.get("_meta", {}).get("tolerance_percent", 5) / 100
    max_allowed = entry["median_seconds"] * (1 + tolerance)
    measured_median = measured["median_seconds"]

    # Stability warning
    cv = measured.get("cv_percent", 0)
    if cv > 15:
        import warnings
        warnings.warn(
            f"Benchmark '{benchmark_name}' is noisy (CV={cv:.1f}%). "
            f"Results may be unreliable.",
            stacklevel=2,
        )

    # If median looks like a regression, check statistical significance
    if measured_median > max_allowed:
        baseline_runs = entry.get("runs")
        measured_runs = measured.get("runs")

        if baseline_runs and measured_runs and len(baseline_runs) >= 3 and len(measured_runs) >= 3:
            p_value = _welch_t_test(measured_runs, baseline_runs)
            if p_value >= 0.05:
                # Not statistically significant — don't fail
                return

        delta = ((measured_median - entry["median_seconds"]) / entry["median_seconds"]) * 100

        assert False, (
            f"Performance regression detected for '{benchmark_name}'!\n"
            f"  Measured median:  {measured_median:.6f}s\n"
            f"  Baseline median:  {entry['median_seconds']:.6f}s\n"
            f"  Delta:            +{delta:.1f}%\n"
            f"  Tolerance:        {tolerance * 100:.0f}%\n"
            f"  Max allowed:      {max_allowed:.6f}s\n"
            f"  Measured CV:      {cv:.1f}%\n"
            f"  Measured runs:    {measured.get('runs', 'N/A')}"
        )


# ---------------------------------------------------------------------------
# System info collection
# ---------------------------------------------------------------------------

def collect_system_info() -> dict:
    """Collect system information for benchmark context."""
    info = {
        "os": f"{platform.system()} {platform.release()}",
        "os_version": platform.version(),
        "architecture": platform.machine(),
        "python_version": platform.python_version(),
    }

    # CPU info
    system = platform.system()
    if system == "Darwin":
        try:
            brand = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                capture_output=True, text=True, timeout=5,
            )
            if brand.returncode == 0:
                info["cpu"] = brand.stdout.strip()
            cores = subprocess.run(
                ["sysctl", "-n", "hw.ncpu"],
                capture_output=True, text=True, timeout=5,
            )
            if cores.returncode == 0:
                info["cpu_cores"] = int(cores.stdout.strip())
            mem = subprocess.run(
                ["sysctl", "-n", "hw.memsize"],
                capture_output=True, text=True, timeout=5,
            )
            if mem.returncode == 0:
                info["ram_gb"] = round(int(mem.stdout.strip()) / (1024 ** 3), 1)
        except (subprocess.TimeoutExpired, ValueError, OSError):
            pass
    elif system == "Linux":
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.startswith("model name"):
                        info["cpu"] = line.split(":", 1)[1].strip()
                        break
            info["cpu_cores"] = os.cpu_count() or 0
            with open("/proc/meminfo") as f:
                for line in f:
                    if line.startswith("MemTotal"):
                        kb = int(line.split()[1])
                        info["ram_gb"] = round(kb / (1024 ** 2), 1)
                        break
        except (OSError, ValueError):
            pass
    else:
        info["cpu_cores"] = os.cpu_count() or 0

    return info


# ---------------------------------------------------------------------------
# Throughput calculation
# ---------------------------------------------------------------------------

def calculate_throughput(file_path: Path, median_seconds: float) -> dict:
    """Calculate throughput metrics for a file-based benchmark."""
    if median_seconds <= 0:
        return {}

    file_size = file_path.stat().st_size
    mb_per_sec = (file_size / (1024 * 1024)) / median_seconds

    # Count lines (cached per file path)
    if not hasattr(calculate_throughput, "_line_cache"):
        calculate_throughput._line_cache = {}

    cache_key = str(file_path)
    if cache_key not in calculate_throughput._line_cache:
        try:
            with open(file_path, "rb") as f:
                calculate_throughput._line_cache[cache_key] = sum(1 for _ in f)
        except OSError:
            calculate_throughput._line_cache[cache_key] = 0

    line_count = calculate_throughput._line_cache[cache_key]
    lines_per_sec = line_count / median_seconds if line_count > 0 else 0

    return {
        "file_size_bytes": file_size,
        "file_size_mb": round(file_size / (1024 * 1024), 2),
        "mb_per_sec": round(mb_per_sec, 2),
        "lines_per_sec": round(lines_per_sec, 0),
    }


# ---------------------------------------------------------------------------
# Benchmark report generation
# ---------------------------------------------------------------------------

_REPORT_DIR = Path(__file__).resolve().parent


def generate_benchmark_report(
    collected_results: dict,
    baseline: dict,
    system_info: dict,
    report_format: str = "markdown",
    bench_runs: int = 21,
    bench_warmup: int = 3,
):
    """Generate a benchmark report after all performance tests complete."""
    if report_format == "none" or not collected_results:
        return

    if report_format == "json":
        _generate_json_report(collected_results, baseline, system_info, bench_runs, bench_warmup)
    else:
        _generate_markdown_report(collected_results, baseline, system_info, bench_runs, bench_warmup)


def _generate_markdown_report(
    results: dict, baseline: dict, system_info: dict,
    bench_runs: int, bench_warmup: int,
):
    """Generate a Markdown benchmark report."""
    today = date.today().isoformat()
    cpu = system_info.get("cpu", "Unknown")
    os_name = system_info.get("os", "Unknown")
    cores = system_info.get("cpu_cores", "?")
    ram = system_info.get("ram_gb", "?")

    lines = [
        f"# LogSquirl Benchmark Report",
        f"",
        f"**Date:** {today}  ",
        f"**System:** {os_name} / {cpu} / {cores} cores / {ram} GB RAM  ",
        f"**Runs:** {bench_runs} measured ({bench_warmup} warmup)  ",
        f"**Outlier filtering:** IQR × 1.5",
        f"",
        f"## Summary",
        f"",
        f"| Benchmark | Median | Mean ± Std | CV% | P5 | P95 | vs Baseline | Status |",
        f"|-----------|--------|-----------|-----|----|----|-------------|--------|",
    ]

    tolerance = baseline.get("_meta", {}).get("tolerance_percent", 5)

    for name, r in sorted(results.items()):
        med = r["median_seconds"]
        m = r.get("mean_seconds", med)
        s = r.get("std_seconds", 0)
        cv = r.get("cv_percent", 0)
        p5 = r.get("p5_seconds", med)
        p95 = r.get("p95_seconds", med)

        bl = baseline.get("benchmarks", {}).get(name, {})
        bl_med = bl.get("median_seconds")
        if bl_med and bl_med > 0:
            delta_pct = ((med - bl_med) / bl_med) * 100
            delta_str = f"{delta_pct:+.1f}%"
            status = "PASS" if med <= bl_med * (1 + tolerance / 100) else "FAIL"
        else:
            delta_str = "NEW"
            status = "PASS"

        lines.append(
            f"| {name} | {med:.4f}s | {m:.4f} ± {s:.4f} | {cv:.1f}% "
            f"| {p5:.4f} | {p95:.4f} | {delta_str} | {status} |"
        )

    # Throughput section
    throughput_rows = []
    for name, r in sorted(results.items()):
        tp = r.get("throughput")
        if tp:
            throughput_rows.append(
                f"| {name} | {tp['file_size_mb']:.1f} MB | {r['median_seconds']:.4f}s "
                f"| {tp['mb_per_sec']:.1f} MB/s | {tp['lines_per_sec']:.0f} |"
            )

    if throughput_rows:
        lines.extend([
            f"",
            f"## Throughput",
            f"",
            f"| Benchmark | File Size | Median | MB/s | Lines/s |",
            f"|-----------|-----------|--------|------|---------|",
        ] + throughput_rows)

    # Stability section
    lines.extend([
        f"",
        f"## Stability Analysis",
        f"",
        f"| Benchmark | CV% | IQR | Outliers Removed | Verdict |",
        f"|-----------|-----|-----|-----------------|---------|",
    ])

    for name, r in sorted(results.items()):
        cv = r.get("cv_percent", 0)
        iqr = r.get("iqr_seconds", 0)
        total = r.get("total_count", 0)
        filtered = r.get("filtered_count", total)
        removed = total - filtered
        verdict = "Stable" if cv < 5 else ("Acceptable" if cv < 10 else ("Noisy" if cv < 15 else "Unstable"))
        lines.append(
            f"| {name} | {cv:.1f}% | {iqr:.4f}s | {removed}/{total} | {verdict} |"
        )

    # Environment section
    lines.extend([
        f"",
        f"## Environment",
        f"",
        f"- **OS:** {os_name}",
        f"- **CPU:** {cpu}",
        f"- **Cores:** {cores}",
        f"- **RAM:** {ram} GB",
        f"- **Python:** {system_info.get('python_version', '?')}",
        f"- **Architecture:** {system_info.get('architecture', '?')}",
        f"",
    ])

    report_path = _REPORT_DIR / "benchmark_report.md"
    report_path.write_text("\n".join(lines), encoding="utf-8")


def _generate_json_report(
    results: dict, baseline: dict, system_info: dict,
    bench_runs: int, bench_warmup: int,
):
    """Generate a JSON benchmark report."""
    report = {
        "date": date.today().isoformat(),
        "system": system_info,
        "config": {
            "warmup": bench_warmup,
            "runs": bench_runs,
            "outlier_method": "iqr_1.5",
        },
        "benchmarks": results,
    }

    report_path = _REPORT_DIR / "benchmark_report.json"
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2)
        f.write("\n")
