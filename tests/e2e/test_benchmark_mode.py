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

logsquirl_grep writes a report of the same format for its Search (#667): the
times of indexing, searching and writing the matches, counted from the open of
the Log File, so the e2e performance suite can tell the Search from the
process startup.

The search and quickfind scenarios measure a Search and a QuickFind typed
character by character on a loaded Log File (#668). A Search they time must be
a correct one: on a Log File written by the generator of the performance
suite, the Matches it reports are the Log Lines counted here without LogSquirl.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from benchmark_mode import (
    EXIT_FAILED,
    EXIT_PASSED,
    EXIT_USAGE,
    SEARCH_VARIANTS,
    SearchVariant,
    event,
    run_benchmark,
    run_grep_benchmark,
)
from generate_test_data import log_lines

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


# Log Lines of the generated Log File the Search is checked on: 300 ERROR Log
# Lines (one in 101, from Log Line 0), and as many screens of Log Lines as the
# Searches and QuickFind need.
GENERATED_LOG_LINES = 30_300


@pytest.fixture(scope="module")
def performance_log(tmp_path_factory) -> Path:
    """A Log File as generate_test_data.py writes the performance suite's, smaller."""
    path = tmp_path_factory.mktemp("benchmark") / "generated_small.log"
    path.write_bytes(log_lines(0, GENERATED_LOG_LINES).encode("utf-8"))
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


def test_grep_reports_its_search_timed_from_the_open(logsquirl_grep_binary, generated_log, tmp_path):
    # Log Lines 3, 11, 19, ...: one in eight.
    run = run_grep_benchmark(logsquirl_grep_binary, "worker-3 ", generated_log, tmp_path / "grep.json")

    assert run.process.returncode == 0, run.process.stderr
    # The matches are written as without a report.
    matches = run.process.stdout.splitlines()
    assert len(matches) == LOG_LINES // 8
    assert all("worker-3 " in line for line in matches)

    report = run.report
    assert report is not None
    assert report["scenario"] == "grep"
    assert report["outcome"] == "passed"

    indexed = event(report, "index_finished")
    searched = event(report, "search_finished")
    written = event(report, "matches_written")
    assert 0 < indexed["since_scenario_start_ms"]
    assert indexed["since_scenario_start_ms"] <= searched["since_scenario_start_ms"]
    assert searched["since_scenario_start_ms"] <= written["since_scenario_start_ms"]
    assert indexed["data"]["log_line_count"] == LOG_LINES
    assert searched["data"]["match_count"] == LOG_LINES // 8

    # The startup came before the measured part and is not in it.
    assert report["scenario_started_ms"] >= report["process"]["main_entered_ms"] >= 0
    assert written["since_process_start_ms"] == pytest.approx(
        report["scenario_started_ms"] + written["since_scenario_start_ms"], abs=0.01
    )

    assert report["results"]["match_count"] == LOG_LINES // 8
    assert report["results"]["log_file_bytes"] == generated_log.stat().st_size
    assert [log_file["size_bytes"] for log_file in report["log_files"]] == [
        generated_log.stat().st_size
    ]


def test_grep_reports_why_its_search_failed(logsquirl_grep_binary, generated_log, tmp_path):
    run = run_grep_benchmark(logsquirl_grep_binary, "(unclosed", generated_log, tmp_path / "grep.json")

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert run.report["outcome"] == "failed"
    assert run.report["failure"]
    assert not any(e["name"] == "matches_written" for e in run.report["events"])


@pytest.mark.parametrize("variant", SEARCH_VARIANTS, ids=lambda variant: variant.label)
def test_search_finds_the_known_matches_of_the_generated_log_file(
    isolated_gui, performance_log, tmp_path, variant: SearchVariant
):
    run = run_benchmark(isolated_gui, "search", [performance_log], tmp_path / "report.json",
                        options=variant.options())

    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    report = run.report
    assert report is not None and report["scenario"] == "search"
    assert report["options"] == variant.options()

    known = variant.known_match_count(performance_log)
    assert report["results"]["match_count"] == known
    assert report["results"]["undecided_count"] == 0
    finished = event(report, "search_finished")
    assert finished["data"]["match_count"] == known
    assert finished["since_scenario_start_ms"] > 0
    if known:
        displayed = event(report, "first_match_displayed")
        assert displayed["since_scenario_start_ms"] > 0
    else:
        assert not any(e["name"] == "first_match_displayed" for e in report["events"])

    assert report["results"]["log_line_count"] == GENERATED_LOG_LINES
    assert report["results"]["log_file_bytes"] == performance_log.stat().st_size
    assert report["results"]["search_gb_per_s"] > 0


def test_the_known_matches_are_those_the_generator_wrote(performance_log):
    # Counted from how the generator numbers its Log Lines, not by a pattern.
    errors = len(range(0, GENERATED_LOG_LINES, 101))
    warnings = sum(1 for n in range(GENERATED_LOG_LINES) if n % 13 == 0 and n % 101 != 0)
    known = {variant.label: variant.known_match_count(performance_log) for variant in SEARCH_VARIANTS}

    assert known == {
        "plain": errors,
        "regex": errors,
        "no_match": 0,
        "case_insensitive": errors,
        "alternation": errors + warnings,
    }


def test_a_search_with_an_invalid_pattern_reports_why(isolated_gui, performance_log, tmp_path):
    variant = SearchVariant("invalid", "(unclosed", regex=True)
    run = run_benchmark(isolated_gui, "search", [performance_log], tmp_path / "report.json",
                        options=variant.options())

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert run.report["outcome"] == "failed"
    assert "Search failed" in run.report["failure"]
    assert not any(e["name"] == "search_finished" for e in run.report["events"])


def test_a_search_without_a_pattern_reports_why(isolated_gui, performance_log, tmp_path):
    run = run_benchmark(isolated_gui, "search", [performance_log], tmp_path / "report.json")

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert "pattern" in run.report["failure"]


def test_quickfind_reports_the_latency_of_each_keystroke(isolated_gui, performance_log, tmp_path):
    pattern = "slow response"
    run = run_benchmark(isolated_gui, "quickfind", [performance_log], tmp_path / "report.json",
                        options={"pattern": pattern, "keystroke_interval_ms": "20"})

    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    report = run.report
    assert report is not None and report["scenario"] == "quickfind"

    # One keystroke per character, each answered by a paint, in order.
    marked = [e for e in report["events"] if e["name"] == "keystroke_marked"]
    assert [e["data"]["typed"] for e in marked] == [
        pattern[: length] for length in range(1, len(pattern) + 1)
    ]
    assert all(e["data"]["latency_ms"] > 0 for e in marked)
    assert [e["since_scenario_start_ms"] for e in marked] == sorted(
        e["since_scenario_start_ms"] for e in marked
    )
    # Each keystroke no sooner than the interval after the one before.
    keystrokes = [e["since_scenario_start_ms"] - e["data"]["latency_ms"] for e in marked]
    assert all(later - earlier >= 20 - 0.01 for earlier, later in zip(keystrokes, keystrokes[1:]))

    latency = report["results"]["keystroke_latency"]
    assert report["results"]["keystroke_count"] == len(pattern)
    assert latency["count"] == len(pattern)
    latencies = sorted(e["data"]["latency_ms"] for e in marked)
    assert latency["p50_ms"] == pytest.approx(latencies[len(latencies) // 2], abs=1e-6)
    assert latency["min_ms"] <= latency["p50_ms"] <= latency["p99_ms"] <= latency["max_ms"]
    assert latency["p99_ms"] == pytest.approx(latencies[-1], abs=1e-6)
    assert report["results"]["log_line_count"] == GENERATED_LOG_LINES
