"""
The application runs a benchmark scenario headless and reports its events as
JSON (#666).

A small Log File is generated for the run, the open-and-index scenario opens
it in an isolated instance, and the report must say when its first Log Line
was displayed and when its Index was finished -- times taken from those events,
not from a sleep -- with the peak RSS, and the run must exit with 0.

A Benchmark Run reads and writes nothing of the user's: the tests also check
that it leaves the settings, Session and data of the instance it runs in as
they were, and nothing behind in the temporary directory.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from benchmark_mode import EXIT_FAILED, EXIT_PASSED, EXIT_USAGE, event, run_benchmark

pytestmark = pytest.mark.slow

LOG_LINES = 20_000


@pytest.fixture(scope="module")
def generated_log(tmp_path_factory) -> Path:
    """A Log File of LOG_LINES Log Lines, each ending in a line feed."""
    path = tmp_path_factory.mktemp("benchmark") / "generated.log"
    with path.open("w", encoding="utf-8", newline="\n") as log:
        for number in range(LOG_LINES):
            log.write(
                f"2026-09-30 12:{number // 60 % 60:02d}:{number % 60:02d}.{number % 1000:03d} "
                f"INFO worker-{number % 8} request {number} handled in {number % 97} ms\n"
            )
    return path


def _stored_state(isolated_gui) -> dict[str, str]:
    """The settings, Session and data an instance keeps, with their digests.

    What the application stores is named after it (``logsquirl.conf``,
    ``.config/logsquirl``, ``AppData/Roaming/logsquirl`` ...), except in the
    portable clone of the app bundle on macOS, which keeps all of it beside
    its executable. The temporary directory is not part of it.
    """
    root = isolated_gui.root
    beside_binary = isolated_gui.binary.parent
    state: dict[str, str] = {}
    for entry in sorted(root.rglob("*")):
        relative = entry.relative_to(root)
        if relative.parts[0] == "t" or entry == isolated_gui.binary:
            continue
        inside_bundle = relative.parts[0].endswith(".app")
        if inside_bundle:
            if beside_binary not in entry.parents:
                continue
        elif not any(part.lower().startswith("logsquirl") for part in relative.parts):
            continue
        if entry.is_file():
            state[str(relative)] = hashlib.sha256(entry.read_bytes()).hexdigest()
        else:
            state[str(relative)] = "dir"
    return state


def test_open_and_index_reports_first_line_displayed_and_index_finished(
    isolated_gui, generated_log, tmp_path
):
    run = run_benchmark(isolated_gui, "open-and-index", [generated_log], tmp_path / "report.json")

    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    report = run.report
    assert report is not None
    assert report["outcome"] == "passed"
    assert report["scenario"] == "open-and-index"

    displayed = event(report, "first_log_line_displayed")
    indexed = event(report, "index_finished")
    for measured in (displayed, indexed):
        # Timed since the open and since the process start, which came first.
        assert measured["since_scenario_start_ms"] > 0
        assert measured["since_process_start_ms"] > measured["since_scenario_start_ms"]
        assert measured["since_process_start_ms"] == pytest.approx(
            report["scenario_started_ms"] + measured["since_scenario_start_ms"], abs=0.01
        )
    assert displayed["data"]["log_line_count"] > 0
    assert indexed["data"]["log_line_count"] == LOG_LINES

    assert report["scenario_started_ms"] >= report["process"]["main_entered_ms"] >= 0
    assert report["process"]["peak_rss_bytes"] > 1024 * 1024

    assert report["results"]["log_line_count"] == LOG_LINES
    assert report["results"]["log_file_bytes"] == generated_log.stat().st_size
    assert report["results"]["index_mb_per_s"] > 0
    assert report["log_files"] == [
        {"path": generated_log.as_posix(), "size_bytes": generated_log.stat().st_size}
    ]
    assert report["platform"]["qpa_platform"] == "offscreen"


def test_a_benchmark_run_keeps_nothing_of_its_own(isolated_gui, generated_log, tmp_path):
    before = _stored_state(isolated_gui)
    temporary = isolated_gui.root / "t"
    temporary_before = {entry.name for entry in temporary.iterdir()}

    run = run_benchmark(isolated_gui, "open-and-index", [generated_log], tmp_path / "report.json")
    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr

    # Neither the settings nor the Session of the instance it ran in were
    # read into a Session of the run's or written by it.
    assert _stored_state(isolated_gui) == before
    # Its own settings, Session and data went with it.
    left_behind = {
        entry.name for entry in temporary.iterdir() if entry.name.startswith("logsquirl-benchmark")
    }
    assert left_behind - temporary_before == set()


def test_an_unknown_scenario_lists_the_scenarios(isolated_gui, generated_log, tmp_path):
    run = run_benchmark(isolated_gui, "no-such-scenario", [generated_log], tmp_path / "report.json")

    assert run.process.returncode == EXIT_USAGE
    assert run.report is None
    assert "no-such-scenario" in run.process.stderr
    assert "open-and-index" in run.process.stderr


def test_a_scenario_that_cannot_run_reports_why(isolated_gui, tmp_path):
    run = run_benchmark(isolated_gui, "open-and-index", [], tmp_path / "report.json")

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert run.report["outcome"] == "failed"
    assert "exactly one Log File" in run.report["failure"]
    assert run.report["events"] == []
