"""Tests for perf-history.py (#441): comparing a weekly performance run with the
median of the runs recorded before it, and recording it on the data branch."""

from __future__ import annotations

import csv
import importlib.util
import io
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "perf_history", Path(__file__).with_name("perf-history.py"))
ph = importlib.util.module_from_spec(_SPEC)
sys.modules["perf_history"] = ph
_SPEC.loader.exec_module(ph)


def entry(day: int, medians: dict[str, float], accepted: bool = False) -> dict:
    return {
        "recorded_at": f"2026-10-{day:02d}T04:23:00Z",
        "commit": f"{day:040d}",
        "accepted": accepted,
        "benchmarks": {n: {"median_seconds": m} for n, m in medians.items()},
    }


def history(values: list[float], name: str = "grep") -> list[dict]:
    return [entry(i + 1, {name: v}) for i, v in enumerate(values)]


def current(**medians: float) -> dict[str, dict]:
    return {n: {"median_seconds": m} for n, m in medians.items()}


def only(rows):
    assert len(rows) == 1
    return rows[0]


# --- the reference is the median of the last WINDOW runs --------------------

def test_the_reference_is_the_median_of_the_last_window_runs():
    # The oldest run (10.0) falls out of the window of 6.
    runs = history([10.0, 1.0, 1.1, 0.9, 1.0, 5.0, 1.2])
    row = only(ph.compare(current(grep=1.0), runs))
    assert row.reference == pytest.approx(1.05)
    assert row.history_count == ph.WINDOW
    assert row.status == "ok"


def test_one_slow_week_does_not_move_the_reference():
    row = only(ph.compare(current(grep=1.0), history([1.0, 1.0, 1.0, 9.0, 1.0, 1.0])))
    assert row.reference == 1.0
    assert row.status == "ok"


# --- the threshold ----------------------------------------------------------

def test_more_than_the_threshold_slower_is_a_regression():
    rows = ph.compare(current(grep=1.31), history([1.0] * 6))
    assert only(rows).status == "regression"
    assert ph.failed(rows)


def test_up_to_the_threshold_slower_is_ok():
    rows = ph.compare(current(grep=1.29), history([1.0] * 6))
    assert only(rows).status == "ok"
    assert not ph.failed(rows)


def test_a_tiny_benchmark_needs_the_absolute_margin_too():
    # 20 ms -> 28 ms is +40 %, but only 8 ms: below MIN_DELTA_SECONDS.
    rows = ph.compare(current(grep=0.028), history([0.020] * 6))
    assert only(rows).status == "ok"
    rows = ph.compare(current(grep=0.031), history([0.020] * 6))
    assert only(rows).status == "regression"


def test_much_faster_is_reported_not_failed():
    rows = ph.compare(current(grep=0.5), history([1.0] * 6))
    assert only(rows).status == "faster"
    assert not ph.failed(rows)


# --- warm-up: report only until WINDOW runs exist ---------------------------

def test_fewer_than_window_runs_report_only():
    rows = ph.compare(current(grep=5.0), history([1.0] * 5))
    row = only(rows)
    assert row.status == "warming-up"
    assert row.history_count == 5
    assert not ph.failed(rows)
    assert "report only, above the limit" in ph.markdown(rows)


def test_no_history_at_all_reports_every_benchmark_as_new():
    rows = ph.compare(current(grep=1.0, load=2.0), [])
    assert [r.status for r in rows] == ["new", "new"]
    assert not ph.failed(rows)
    notice = ph.annotations(rows)
    assert notice == ["::notice::Report only, fewer than 6 recorded runs: grep, load"]


# --- nothing passes silently ------------------------------------------------

def test_a_benchmark_the_previous_run_had_is_missing():
    runs = [entry(1, {"grep": 1.0, "grep_100mb": 3.0})]
    rows = ph.compare(current(grep=1.0), runs)
    missing = [r for r in rows if r.name == "grep_100mb"]
    assert [r.status for r in missing] == ["missing"]
    assert ph.failed(rows)
    assert any("grep_100mb was measured by the previous run" in a for a in ph.annotations(rows))


# --- accepting a new level --------------------------------------------------

def test_the_window_starts_at_the_latest_accepted_run():
    runs = history([1.0] * 6) + [entry(20, {"grep": 2.0}, accepted=True), entry(21, {"grep": 2.0})]
    window = ph.comparison_window(runs)
    assert [e["recorded_at"][:10] for e in window] == ["2026-10-20", "2026-10-21"]
    row = only(ph.compare(current(grep=2.0), runs))
    assert row.reference == 2.0
    assert row.status == "warming-up"


def test_an_accepting_run_does_not_fail():
    rows = ph.compare(current(grep=2.0), history([1.0] * 6), accept=True)
    assert only(rows).status == "accepted"
    assert not ph.failed(rows)


# --- recording --------------------------------------------------------------

def report(medians: dict[str, float]) -> dict:
    return {
        "date": "2026-10-05",
        "system": {"cpu": "AMD EPYC"},
        "config": {"runs": 21},
        "benchmarks": {n: {"median_seconds": m, "cv_percent": 2.0, "runs": [m] * 21}
                       for n, m in medians.items()},
    }


def run_record(tmp_path: Path, medians: dict[str, float], dest: str = "history",
               run_id: str = "1", extra: list[str] | None = None) -> tuple[int, dict]:
    path = tmp_path / f"report-{run_id}.json"
    path.write_text(json.dumps(report(medians)))
    out = tmp_path / f"verdict-{run_id}.json"
    status = ph.main([
        "record", "--report", str(path), "--data-dir", str(tmp_path / "data"),
        "--dest", dest, "--commit", "a" * 40, "--ref", "refs/heads/master",
        "--run-id", run_id, "--version", "26.10.0", "--json", str(out),
        "--markdown", str(tmp_path / "summary.md"), *(extra or []),
    ])
    return status, json.loads(out.read_text()) if out.exists() else {}


def test_record_writes_the_entry_and_the_trend(tmp_path):
    status, verdict = run_record(tmp_path, {"grep": 1.0})
    assert status == 0
    assert verdict["failed"] is False
    files = list((tmp_path / "data" / "history").glob("*.json"))
    assert len(files) == 1
    stored = json.loads(files[0].read_text())
    assert stored["version"] == "26.10.0"
    assert stored["ref"] == "refs/heads/master"
    # Statistics are kept, the raw runs stay in the run's artifact.
    assert stored["benchmarks"]["grep"] == {"median_seconds": 1.0, "cv_percent": 2.0}
    trend = list(csv.reader(io.StringIO((tmp_path / "data" / "trend.csv").read_text())))
    assert trend[0] == ["recorded_at", "version", "commit", "accepted", "grep"]
    assert trend[1][1:] == ["26.10.0", "a" * 12, "", "1.0"]


def test_a_regressing_run_is_recorded_and_fails(tmp_path):
    for i in range(6):
        assert run_record(tmp_path, {"grep": 1.0}, run_id=str(i))[0] == 0
    status, verdict = run_record(tmp_path, {"grep": 2.0}, run_id="9")
    assert status == 1
    assert verdict["failed"] is True
    assert len(list((tmp_path / "data" / "history").glob("*.json"))) == 7


def test_a_trial_run_is_compared_but_never_becomes_history(tmp_path):
    for i in range(6):
        run_record(tmp_path, {"grep": 1.0}, run_id=str(i))
    status, _ = run_record(tmp_path, {"grep": 2.0}, dest="trial", run_id="9")
    assert status == 1
    assert len(list((tmp_path / "data" / "history").glob("*.json"))) == 6
    assert len(list((tmp_path / "data" / "trial").glob("*.json"))) == 1
    trend = (tmp_path / "data" / "trend.csv").read_text().splitlines()
    assert len(trend) == 7


def test_an_empty_report_is_an_error_and_records_nothing(tmp_path):
    path = tmp_path / "report.json"
    path.write_text(json.dumps({"benchmarks": {}}))
    status = ph.main(["record", "--report", str(path), "--data-dir", str(tmp_path / "data"),
                      "--dest", "history", "--commit", "a" * 40, "--ref", "x", "--run-id", "1"])
    assert status == 2
    assert not (tmp_path / "data").exists()


def test_the_entry_filename_sorts_by_time():
    e = ph.make_entry(report({"grep": 1.0}), commit="b" * 40, ref="r", run_id="123-4/",
                      version="", accept=False,
                      recorded_at=datetime(2026, 10, 5, 4, 23, tzinfo=timezone.utc))
    assert ph.entry_filename(e) == "20261005T042300Z-bbbbbbbbbbbb-123-4.json"
