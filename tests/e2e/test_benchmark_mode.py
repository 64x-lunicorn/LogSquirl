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

The scroll scenario scrolls a loaded Log File by a script in the Text View or
the Table View and reports every frame, the paint of the view's Viewport
(#669). It prepares the settings it measures under -- a Highlighter Set, ANSI
colors, Format Recognition -- in the run's own settings, so the instance it runs
in keeps its own as they were.

The follow scenario writes a Log File of its own that grows at a fixed rate and
reports the time from each append to its Log Line displayed, and whether a
chart following it kept up (#670). The session-restore scenario generates a
Session of several tabs in the run's own data location, never the instance's,
restores it and reports when the tab in front was usable and every tab indexed.
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
from generate_test_data import log_lines, scroll_log_lines

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


# Log Lines of the small scroll Log Files: more than the script scrolls by.
SCROLL_LOG_LINES = 5_000


@pytest.fixture(scope="module")
def scroll_log(tmp_path_factory) -> Path:
    """A scroll Log File as generate_test_data.py writes it, smaller."""
    path = tmp_path_factory.mktemp("benchmark") / "scroll_small.log"
    path.write_bytes(scroll_log_lines(0, SCROLL_LOG_LINES).encode("utf-8"))
    return path


@pytest.fixture(scope="module")
def scroll_ansi_log(tmp_path_factory) -> Path:
    """The same Log Lines with ANSI color sequences."""
    path = tmp_path_factory.mktemp("benchmark") / "scroll_ansi_small.log"
    path.write_bytes(scroll_log_lines(0, SCROLL_LOG_LINES, ansi=True).encode("utf-8"))
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


# A short script: 20 line steps, 5 page steps and the jump to the end.
SCROLL_SCRIPT = {"line_steps": "20", "page_steps": "5"}


def _scroll(isolated_gui, log_file: Path, tmp_path: Path, **options: str) -> dict:
    run = run_benchmark(isolated_gui, "scroll", [log_file], tmp_path / "report.json",
                        options={**SCROLL_SCRIPT, **options})
    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    assert run.report is not None and run.report["scenario"] == "scroll"
    return run.report


def _check_frames(report: dict, view: str):
    results = report["results"]
    assert results["view"] == view
    assert results["step_count"] == 26
    assert (results["line_step_count"], results["page_step_count"]) == (20, 5)
    assert results["unmoved_step_count"] == 0
    assert results["log_line_count"] == SCROLL_LOG_LINES

    # Every step was answered by a paint, and every paint is a frame.
    frames = results["frame_time"]
    assert frames["count"] >= results["step_count"]
    assert 0 < frames["min_ms"] <= frames["p50_ms"] <= frames["p99_ms"] <= frames["max_ms"]
    assert frames["budget_ms"] == pytest.approx(1000 / 60)
    assert 0 <= frames["over_budget_count"] <= frames["count"]
    assert event(report, "scrolled_to_end")["data"]["frame_count"] == frames["count"]


def test_scroll_reports_every_frame_of_the_text_view(isolated_gui, scroll_log, tmp_path):
    report = _scroll(isolated_gui, scroll_log, tmp_path)

    _check_frames(report, "text")
    assert report["results"]["highlighter_count"] == 0
    assert report["options"] == SCROLL_SCRIPT


def test_scroll_reports_every_frame_of_the_table_view(isolated_gui, scroll_log, tmp_path):
    # The scroll Log File has a Log Format the Catalog knows, so a Table View.
    report = _scroll(isolated_gui, scroll_log, tmp_path, view="table")

    _check_frames(report, "table")


def test_scroll_with_highlighters_keeps_the_instances_own(isolated_gui, scroll_log, tmp_path):
    before = _stored_state(isolated_gui)

    for view in ("text", "table"):
        report = _scroll(isolated_gui, scroll_log, tmp_path, view=view, highlighters="true")
        _check_frames(report, view)
        assert report["results"]["highlighter_count"] == 5

    # The Highlighter Set and the settings went into the run's own directory.
    assert _stored_state(isolated_gui) == before


@pytest.mark.parametrize("ansi", ["hide", "colors"])
def test_scroll_shows_ansi_colors_as_the_option_says(isolated_gui, scroll_ansi_log, tmp_path, ansi):
    report = _scroll(isolated_gui, scroll_ansi_log, tmp_path, ansi=ansi)

    _check_frames(report, "text")
    assert report["options"]["ansi"] == ansi


def test_scroll_in_the_table_view_needs_a_log_format(isolated_gui, performance_log, tmp_path):
    # The Log Lines of the other generated Log Files have no Log Format.
    run = run_benchmark(isolated_gui, "scroll", [performance_log], tmp_path / "report.json",
                        options={"view": "table"})

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert "no Log Format was recognized" in run.report["failure"]


@pytest.mark.parametrize("option, value", [
    ("view", "grid"), ("highlighters", "yes"), ("ansi", "rainbow"), ("line_steps", "-1"),
])
def test_scroll_with_a_wrong_option_reports_why(isolated_gui, scroll_log, tmp_path, option, value):
    run = run_benchmark(isolated_gui, "scroll", [scroll_log], tmp_path / "report.json",
                        options={option: value})

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert option in run.report["failure"]
    assert run.report["events"] == []


# A short follow: 2 s at 20 Log Lines a second.
FOLLOW_OPTIONS = {"lines_per_second": "20", "duration_ms": "2000", "initial_lines": "500"}


def _follow(isolated_gui, tmp_path: Path, **options: str) -> dict:
    run = run_benchmark(isolated_gui, "follow", [], tmp_path / "report.json",
                        options={**FOLLOW_OPTIONS, **options})
    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    assert run.report is not None and run.report["scenario"] == "follow"
    return run.report


def _check_latency(latency: dict, count: int):
    assert latency["count"] == count
    assert 0 <= latency["min_ms"] <= latency["p50_ms"] <= latency["p99_ms"] <= latency["max_ms"]


def test_follow_reports_each_appended_log_line_until_displayed_and_charted(
    isolated_gui, tmp_path
):
    report = _follow(isolated_gui, tmp_path)

    results = report["results"]
    assert results["lines_per_second"] == 20
    assert results["appended_count"] == 40
    assert results["log_line_count"] == 540
    _check_latency(results["display_latency"], 40)
    _check_latency(results["writer_lateness"], 40)
    _check_latency(results["chart_latency"], 40)
    _check_latency(results["chart_behind_display"], 40)
    assert results["chart_charted_count"] == 40
    assert isinstance(results["chart_kept_up"], bool)
    assert results["chart_budget_ms"] == 1000

    # The writer took the time it was given; then the last Log Line was
    # displayed and charted.
    finished = event(report, "writer_finished")
    assert finished["data"]["appended_count"] == 40
    assert finished["since_scenario_start_ms"] >= 39 * 50
    displayed = event(report, "last_log_line_displayed")
    assert displayed["since_scenario_start_ms"] >= finished["since_scenario_start_ms"]
    assert event(report, "last_log_line_charted")["since_scenario_start_ms"] >= (
        finished["since_scenario_start_ms"]
    )


def test_follow_without_a_chart_reports_the_display_only(isolated_gui, tmp_path):
    before = _stored_state(isolated_gui)

    report = _follow(isolated_gui, tmp_path, chart="false")

    results = report["results"]
    _check_latency(results["display_latency"], 40)
    assert "chart_latency" not in results and "chart_kept_up" not in results
    # The Log File it grew was the run's own.
    assert _stored_state(isolated_gui) == before


@pytest.mark.parametrize("option, value", [
    ("lines_per_second", "0"), ("duration_ms", "soon"), ("chart", "yes"), ("initial_lines", "0"),
])
def test_follow_with_a_wrong_option_reports_why(isolated_gui, tmp_path, option, value):
    run = run_benchmark(isolated_gui, "follow", [], tmp_path / "report.json",
                        options={option: value})

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert option in run.report["failure"]
    assert run.report["events"] == []


def test_follow_takes_no_log_file(isolated_gui, generated_log, tmp_path):
    run = run_benchmark(isolated_gui, "follow", [generated_log], tmp_path / "report.json")

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert "takes none" in run.report["failure"]


@pytest.fixture(scope="module")
def session_logs(tmp_path_factory) -> list[Path]:
    """Three Log Files of different sizes, for the tabs of a Session."""
    directory = tmp_path_factory.mktemp("session")
    paths = []
    for tab, lines in enumerate((5_000, 20_000, 10_000)):
        path = directory / f"tab{tab}.log"
        path.write_bytes(log_lines(0, lines).encode("utf-8"))
        paths.append(path)
    return paths


def test_session_restore_reports_the_tab_in_front_usable_and_every_tab_indexed(
    isolated_gui, session_logs, tmp_path
):
    before = _stored_state(isolated_gui)

    run = run_benchmark(isolated_gui, "session-restore", session_logs, tmp_path / "report.json",
                        options={"current": "1"})

    assert run.process.returncode == EXIT_PASSED, run.process.stdout + run.process.stderr
    report = run.report
    results = report["results"]
    assert (results["tab_count"], results["current_tab"]) == (3, 1)
    assert results["marks_per_tab"] == 10
    assert results["log_line_count"] == 35_000
    assert results["log_file_bytes"] == sum(path.stat().st_size for path in session_logs)

    indexed = {e["data"]["tab"]: e for e in report["events"] if e["name"] == "tab_indexed"}
    assert sorted(indexed) == [0, 1, 2]
    assert [indexed[tab]["data"]["log_line_count"] for tab in range(3)] == [5_000, 20_000, 10_000]
    # The tab in front loads first, and is usable once its Index is done.
    first = min(indexed.values(), key=lambda e: e["since_scenario_start_ms"])
    assert first["data"]["tab"] == 1
    usable = event(report, "current_tab_usable")
    assert usable["data"]["tab"] == 1
    assert usable["since_scenario_start_ms"] >= indexed[1]["since_scenario_start_ms"] > 0
    all_indexed = event(report, "all_tabs_indexed")
    assert all_indexed["since_scenario_start_ms"] == max(
        e["since_scenario_start_ms"] for e in indexed.values()
    )

    # The Session was generated in the run's own data location: the
    # instance's settings and Session are as they were.
    assert _stored_state(isolated_gui) == before


@pytest.mark.parametrize("options, says", [
    ({"current": "3"}, "current"), ({"current": "front"}, "current"), ({"marks": "-1"}, "marks"),
])
def test_session_restore_with_a_wrong_option_reports_why(
    isolated_gui, session_logs, tmp_path, options, says
):
    run = run_benchmark(isolated_gui, "session-restore", session_logs, tmp_path / "report.json",
                        options=options)

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert says in run.report["failure"]
    assert run.report["events"] == []


def test_session_restore_needs_several_log_files(isolated_gui, session_logs, tmp_path):
    run = run_benchmark(isolated_gui, "session-restore", session_logs[:1],
                        tmp_path / "report.json")

    assert run.process.returncode == EXIT_FAILED
    assert run.report is not None
    assert "at least two Log Files" in run.report["failure"]
