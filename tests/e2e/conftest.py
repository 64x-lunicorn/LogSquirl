"""
Shared fixtures and helpers for LogSquirl E2E tests.

Provides binary paths, test data access, subprocess runners,
and performance baseline comparison utilities.
"""

import json
import os
import platform
import subprocess
import time
from pathlib import Path

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
    binary = binary_dir / "logsquirl_grep"
    if not binary.exists():
        pytest.skip(f"logsquirl_grep not found at {binary}")
    return binary


@pytest.fixture(scope="session")
def logsquirl_binary(binary_dir) -> Path:
    system = platform.system()
    if system == "Darwin":
        binary = binary_dir / "logsquirl.app" / "Contents" / "MacOS" / "logsquirl"
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

_BASELINE_PATH = Path(__file__).resolve().parent / "baseline.json"


def load_baseline() -> dict:
    with open(_BASELINE_PATH) as f:
        return json.load(f)


def save_baseline(data: dict):
    with open(_BASELINE_PATH, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def measure_execution(func, warmup: int = 1, runs: int = 5) -> dict:
    """
    Run func() multiple times and return timing statistics.

    Returns dict with 'median_seconds' and 'p95_seconds'.
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
    median = times[n // 2]
    p95_idx = min(int(n * 0.95), n - 1)
    p95 = times[p95_idx]

    return {"median_seconds": round(median, 6), "p95_seconds": round(p95, 6)}


def assert_performance(benchmark_name: str, measured: dict, baseline: dict):
    """
    Assert that measured performance does not exceed baseline by more than tolerance.

    If baseline value is None, the check is skipped (first run).
    """
    entry = baseline.get("benchmarks", {}).get(benchmark_name)
    if not entry or entry.get("median_seconds") is None:
        return  # No baseline yet — skip comparison

    tolerance = baseline.get("_meta", {}).get("tolerance_percent", 5) / 100
    max_allowed = entry["median_seconds"] * (1 + tolerance)
    measured_median = measured["median_seconds"]

    assert measured_median <= max_allowed, (
        f"Performance regression detected for '{benchmark_name}'!\n"
        f"  Measured:  {measured_median:.4f}s\n"
        f"  Baseline:  {entry['median_seconds']:.4f}s\n"
        f"  Tolerance: {tolerance * 100:.0f}%\n"
        f"  Max allowed: {max_allowed:.4f}s"
    )
