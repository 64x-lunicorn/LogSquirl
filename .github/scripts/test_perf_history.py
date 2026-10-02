"""Tests for perf-history.py (#441, #677): finding change points in the nightly
performance runs, wall-clock and instruction counts, checking the Budgets, and
recording each run on the data branch."""

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

CPU = "AMD EPYC 7763 64-Core Processor"
BIN = "logsquirl_logdata_benchmark"
CASE = "Indexing a Log File / short lines: whole Log File"
KEY = f"{BIN} / {CASE}"


def sha(day: int) -> str:
    return f"{day:040x}"


def counts_block(instructions: dict[str, int], cpu: str = CPU, log_file_mb: int = 32,
                 failed: list[str] | None = None) -> dict:
    return {"cpu": cpu, "log_file_mb": log_file_mb, "failed_binaries": failed or [],
            "benchmarks": {k: {"instructions": v, "allocations": None, "peak_heap_bytes": None}
                           for k, v in instructions.items()}}


def entry(day: int, medians: dict[str, float] | None = None, *, accepted: bool = False,
          counts: dict | None = None, cpu: str = CPU) -> dict:
    e = {
        "recorded_at": f"2026-10-{day:02d}T02:41:00Z",
        "commit": sha(day),
        "ref": "refs/heads/master",
        "run_id": f"{day}-1",
        "accepted": accepted,
        "system": {"cpu": cpu},
        "benchmarks": {n: {"median_seconds": m} for n, m in (medians or {}).items()},
    }
    if counts is not None:
        e["instruction_counts"] = counts
    return e


def series(values: list[float], name: str = "grep", start: int = 1) -> list[dict]:
    return [entry(start + i, {name: v}) for i, v in enumerate(values)]


def count_series(values: list[int], start: int = 1, **kwargs) -> list[dict]:
    return [entry(start + i, {"grep": 1.0}, counts=counts_block({KEY: v}, **kwargs))
            for i, v in enumerate(values)]


def rows_of(rows, metric: str):
    return [r for r in rows if r.metric == metric]


def only(rows):
    assert len(rows) == 1
    return rows[0]


# --- wall-clock: a change point, recorded as the trend ----------------------

def test_a_flat_wall_clock_series_is_ok():
    runs = series([1.0] * 8)
    row = only(ph.compare(runs[-1], runs[:-1]))
    assert row.metric == "wall-clock"
    assert row.status == "ok"
    assert row.reference == 1.0


def test_a_wall_clock_shift_that_persists_is_a_change_point_with_its_commits():
    runs = series([1.0] * 8 + [1.5, 1.5])
    row = only(ph.compare(runs[-1], runs[:-1]))
    assert row.status == "regression"
    assert row.last_good["commit"] == sha(8)
    assert row.first_bad["commit"] == sha(9)
    assert row.streak == 2


def test_one_slow_night_is_pending():
    runs = series([1.0] * 8 + [1.5])
    assert only(ph.compare(runs[-1], runs[:-1])).status == "pending"


def test_a_budget_may_set_the_absolute_margin_of_its_benchmark():
    # 20 ms -> 28 ms is inside the cap of 10 ms; with 1 ms set beside its
    # Budget (ADR 0018), it is not.
    runs = series([0.020] * 8 + [0.028, 0.028], name="gui_open_1mb_indexed")
    assert only(ph.compare(runs[-1], runs[:-1])).status == "ok"
    data = budgets_file(budget_entry("gui_open_1mb_indexed", 1.0, min_delta_seconds=0.001))
    assert only(ph.compare(runs[-1], runs[:-1], budgets=data)).status == "regression"


def test_too_few_runs_report_only():
    runs = series([1.0] * 3 + [5.0])
    row = only(ph.compare(runs[-1], runs[:-1]))
    assert row.status == "warming-up"
    assert row.history_count == 3


def test_no_history_reports_every_benchmark_as_new():
    current = entry(1, {"grep": 1.0, "load": 2.0})
    assert [r.status for r in ph.compare(current, [])] == ["new", "new"]


def test_the_series_starts_at_the_latest_accepted_run():
    runs = series([1.0] * 8) + [entry(20, {"grep": 2.0}, accepted=True), entry(21, {"grep": 2.0})]
    assert [e["commit"] for e in ph.level(runs)] == [sha(20), sha(21)]
    row = only(ph.compare(entry(22, {"grep": 2.0}), runs))
    assert row.status == "warming-up"
    assert row.reference == 2.0


# --- instruction counts: the change points that file an issue --------------

def test_more_instructions_than_the_threshold_is_a_regression_at_once():
    runs = count_series([1_000_000] * 5 + [1_030_000])
    rows = ph.compare(runs[-1], runs[:-1])
    row = only(rows_of(rows, "instructions"))
    assert row.name == KEY
    assert row.status == "regression"
    assert row.scenario == BIN
    assert row.last_good["commit"] == sha(5)
    assert row.first_bad["commit"] == sha(6)
    assert row.limit == pytest.approx(1_020_000)


def test_a_count_within_its_threshold_is_ok():
    runs = count_series([1_000_000] * 5 + [1_015_000])
    assert only(rows_of(ph.compare(runs[-1], runs[:-1]), "instructions")).status == "ok"


def test_a_benchmark_that_varies_more_has_its_gate_threshold():
    # The gate's 6 % for the indexing of tabs and long lines (#671).
    key = f"{BIN} / Indexing a Log File / tabs and long lines: whole Log File"
    runs = [entry(i + 1, counts=counts_block({key: v}))
            for i, v in enumerate([1_000_000] * 5 + [1_050_000])]
    assert only(rows_of(ph.compare(runs[-1], runs[:-1]), "instructions")).status == "ok"


def test_counts_compare_only_within_one_cpu_model_and_log_file_size():
    # glibc picks its string functions by CPU, so a count of another model is
    # another series.
    other = count_series([1_100_000] * 5, cpu="AMD EPYC 9V74")
    bigger = count_series([2_000_000] * 5, start=10, log_file_mb=64)
    runs = count_series([1_000_000] * 3, start=20)
    current = count_series([1_001_000], start=30)[0]
    row = only(rows_of(ph.compare(current, other + bigger + runs), "instructions"))
    assert row.status == "ok"
    assert row.history_count == 3


def test_the_first_bad_commit_is_where_the_counts_went_up():
    runs = count_series([1_000_000] * 5 + [1_100_000] * 4)
    row = only(rows_of(ph.compare(runs[-1], runs[:-1]), "instructions"))
    assert row.status == "regression"
    assert (row.last_good["commit"], row.first_bad["commit"]) == (sha(5), sha(6))
    assert row.streak == 4


def test_a_regression_of_counts_files_an_issue():
    runs = count_series([1_000_000] * 5 + [1_030_000])
    found = ph.findings(ph.compare(runs[-1], runs[:-1]), [])
    assert len(found) == 1
    f = found[0]
    assert f["kind"] == "regression"
    assert f["metric"] == "instructions"
    assert f["benchmark"] == KEY
    assert f["scenario"] == BIN
    assert f["last_good"]["commit"] == sha(5)
    assert f["first_bad"]["commit"] == sha(6)


# --- nothing passes silently ------------------------------------------------

def test_a_benchmark_the_previous_run_had_is_missing():
    runs = [entry(1, {"grep": 1.0, "grep_100mb": 3.0})]
    rows = ph.compare(entry(2, {"grep": 1.0}), runs)
    missing = only([r for r in rows if r.name == "grep_100mb"])
    assert missing.status == "missing"
    assert (missing.last_good["commit"], missing.first_bad["commit"]) == (sha(1), sha(2))
    assert [f["kind"] for f in ph.findings(rows, [])] == ["missing"]


def test_a_counted_benchmark_the_previous_run_had_is_missing():
    runs = count_series([1_000_000])
    current = entry(2, {"grep": 1.0}, counts=counts_block({}, failed=[BIN]))
    row = only([r for r in ph.compare(current, runs) if r.status == "missing"])
    assert row.name == KEY
    assert row.scenario == BIN


def test_counts_that_were_not_taken_at_all_are_one_finding():
    runs = count_series([1_000_000, 1_000_000])
    rows = ph.compare(entry(3, {"grep": 1.0}), runs)
    row = only([r for r in rows if r.status == "missing"])
    assert row.name == "instruction counts"
    assert row.metric == "instructions"


def test_an_accepting_run_turns_regressions_into_the_new_level():
    runs = count_series([1_000_000] * 5)
    current = count_series([1_100_000], start=6)[0]
    rows = ph.compare(current, runs, accept=True)
    assert only(rows_of(rows, "instructions")).status == "accepted"
    assert ph.findings(rows, []) == []


# --- scenarios --------------------------------------------------------------

def test_the_scenario_of_a_benchmark_comes_from_its_budget_or_its_name():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.3))
    assert ph.scenario_of("gui_open_log_1gb_indexed", "wall-clock", data) == "open-and-index"
    assert ph.scenario_of("gui_search_10mb_plain_finished", "wall-clock", None) == "search"
    assert ph.scenario_of("grep_log_1gb_simple", "wall-clock", None) == "grep"
    assert ph.scenario_of("gui_session_restore_small_x", "wall-clock", None) == "session-restore"
    assert ph.scenario_of("something_else", "wall-clock", None) == "other"
    assert ph.scenario_of(KEY, "instructions", None) == BIN


# --- Budgets ----------------------------------------------------------------

def budget_entry(benchmark: str, value: float, **extra) -> dict:
    return {
        "scenario": "open-and-index", "file": "1 GB Log File", "metric": "open to indexed",
        "report": "e2e", "benchmark": benchmark, "field": "median_seconds", "unit": "s",
        "budget": value, "noise_margin_percent": 10, "source": ["perf-1"], **extra,
    }


def budgets_file(*entries: dict) -> dict:
    return {"schema": 1, "sources": {"perf-1": {"run": 1}},
            "budgets": {e["benchmark"]: e for e in entries}}


def test_a_broken_budget_names_the_runs_since_it_broke():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.30))
    runs = series([0.25] * 4 + [0.40] * 3, name="gui_open_log_1gb_indexed")
    rows = ph.check_budgets(data, runs[-1], runs[:-1])
    row = only(rows)
    assert row.status == "broken"
    assert row.last_good["commit"] == sha(4)
    assert row.first_bad["commit"] == sha(5)
    found = only(ph.findings([], rows))
    assert found["kind"] == "budget"
    assert found["scenario"] == "open-and-index"
    assert found["benchmark"] == "gui_open_log_1gb_indexed"
    assert found["budget"] == 0.30


def test_a_budget_broken_since_the_first_run_has_no_last_good_run():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.30))
    runs = series([0.40] * 2, name="gui_open_log_1gb_indexed")
    row = only(ph.check_budgets(data, runs[-1], runs[:-1]))
    assert row.last_good is None
    assert row.first_bad["commit"] == sha(1)


def test_a_budget_that_holds_files_nothing():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.30))
    runs = series([0.25] * 2, name="gui_open_log_1gb_indexed")
    rows = ph.check_budgets(data, runs[-1], runs[:-1])
    assert only(rows).status == "ok"
    assert ph.findings([], rows) == []


def test_a_budget_of_the_counts_reads_the_recorded_counts():
    data = budgets_file({
        **budget_entry("x", 100.0), "scenario": "memory", "report": "instruction-counts",
        "benchmark": KEY, "field": "peak_heap_bytes", "unit": "bytes", "scale": 0.5,
    })
    block = counts_block({KEY: 1})
    block["benchmarks"][KEY]["peak_heap_bytes"] = 300
    row = only(ph.check_budgets(data, entry(1, counts=block), []))
    assert row.status == "broken"
    assert row.measured == 150


def test_budgets_without_counts_are_not_measured():
    data = budgets_file({**budget_entry("x", 100.0), "report": "instruction-counts",
                         "benchmark": KEY, "field": "peak_heap_bytes"})
    assert only(ph.check_budgets(data, entry(1, {"grep": 1.0}), [])).status == "not-measured"


# --- the issue the record job hands on --------------------------------------

def test_a_synthetic_regression_on_a_branch_is_found_with_its_commit_range(tmp_path):
    # Acceptance of #677: a test commit dispatched from a branch adds 3 % to
    # one benchmark's count. The run lands in trial/ and names the range from
    # master's last run to the branch's commit.
    for day in range(1, 6):
        assert run_record(tmp_path, {"grep": 1.0}, day=day,
                          counts={KEY: 1_000_000, f"{BIN} / other": 500})[0] == 0
    status, verdict = run_record(tmp_path, {"grep": 1.0}, day=9, dest="trial",
                                 counts={KEY: 1_030_000, f"{BIN} / other": 500},
                                 ref="refs/heads/test-regression")
    assert status == 1
    assert verdict["failed"] is True
    assert verdict["dest"] == "trial"
    found = only(verdict["findings"])
    assert found["benchmark"] == KEY
    assert found["last_good"]["commit"] == sha(5)
    assert found["first_bad"]["commit"] == sha(9)
    assert found["first_bad"]["ref"] == "refs/heads/test-regression"


# --- recording --------------------------------------------------------------

def report(medians: dict[str, float]) -> dict:
    return {
        "date": "2026-10-05",
        "system": {"cpu": CPU},
        "config": {"runs": 21},
        "benchmarks": {n: {"median_seconds": m, "cv_percent": 2.0, "runs": [m] * 21}
                       for n, m in medians.items()},
    }


def counts_report(counts: dict[str, int], failed: list[str] | None = None) -> dict:
    return {"schema_version": 1, "failed_binaries": failed or [],
            "benchmarks": [{"binary": k.split(" / ", 1)[0], "name": k.split(" / ", 1)[1],
                            "instructions": v, "allocations": 3, "peak_heap_bytes": 4096}
                           for k, v in counts.items()]}


def run_record(tmp_path: Path, medians: dict[str, float], dest: str = "history",
               day: int = 1, extra: list[str] | None = None,
               counts: dict[str, int] | None = None,
               ref: str = "refs/heads/master") -> tuple[int, dict]:
    path = tmp_path / f"report-{day}.json"
    path.write_text(json.dumps(report(medians)))
    out = tmp_path / f"verdict-{day}.json"
    args = [
        "record", "--report", str(path), "--data-dir", str(tmp_path / "data"),
        "--dest", dest, "--commit", sha(day), "--ref", ref,
        "--run-id", f"{day}-1", "--version", "26.11.0", "--json", str(out),
        "--markdown", str(tmp_path / "summary.md"),
        "--recorded-at", f"2026-10-{day:02d}T02:41:00Z", *(extra or []),
    ]
    if counts is not None:
        counts_path = tmp_path / f"counts-{day}.json"
        counts_path.write_text(json.dumps(counts_report(counts)))
        args += ["--instruction-counts", str(counts_path), "--counts-cpu", CPU,
                 "--counts-log-file-mb", "32"]
    status = ph.main(args)
    return status, json.loads(out.read_text()) if out.exists() else {}


def test_record_writes_the_entry_and_the_trend(tmp_path):
    status, verdict = run_record(tmp_path, {"grep": 1.0}, counts={KEY: 1000})
    assert status == 0
    assert verdict["failed"] is False
    files = list((tmp_path / "data" / "history").glob("*.json"))
    assert len(files) == 1
    stored = json.loads(files[0].read_text())
    assert stored["version"] == "26.11.0"
    assert stored["ref"] == "refs/heads/master"
    # Statistics are kept, the raw runs stay in the run's artifact.
    assert stored["benchmarks"]["grep"] == {"median_seconds": 1.0, "cv_percent": 2.0}
    assert stored["instruction_counts"] == {
        "cpu": CPU, "log_file_mb": 32, "failed_binaries": [],
        "benchmarks": {KEY: {"instructions": 1000, "allocations": 3, "peak_heap_bytes": 4096}},
    }
    trend = list(csv.reader(io.StringIO((tmp_path / "data" / "trend.csv").read_text())))
    assert trend[0] == ["recorded_at", "version", "commit", "accepted", "cpu",
                        "median_cv_percent", "unusable", "reference", "grep",
                        f"instructions: {KEY}"]
    assert trend[1][1:] == ["26.11.0", sha(1)[:12], "", CPU, "2.0", "", "", "1.0", "1000"]


def test_record_checks_the_budgets(tmp_path):
    budgets = tmp_path / "budgets.json"
    budgets.write_text(json.dumps(budgets_file(budget_entry("grep", 0.5))))
    status, verdict = run_record(tmp_path, {"grep": 1.0}, extra=["--budgets", str(budgets)])
    assert status == 1
    assert only(verdict["budgets"])["status"] == "broken"
    assert only(verdict["findings"])["kind"] == "budget"
    assert "Performance Budgets" in (tmp_path / "summary.md").read_text()


def test_a_regressing_run_is_recorded_and_fails(tmp_path):
    for day in range(1, 5):
        assert run_record(tmp_path, {"grep": 1.0}, day=day, counts={KEY: 1000})[0] == 0
    status, verdict = run_record(tmp_path, {"grep": 1.0}, day=5, counts={KEY: 2000})
    assert status == 1
    assert verdict["failed"] is True
    assert len(list((tmp_path / "data" / "history").glob("*.json"))) == 5


def test_a_trial_run_is_compared_but_never_becomes_history(tmp_path):
    for day in range(1, 5):
        run_record(tmp_path, {"grep": 1.0}, day=day, counts={KEY: 1000})
    status, _ = run_record(tmp_path, {"grep": 1.0}, dest="trial", day=9, counts={KEY: 2000})
    assert status == 1
    assert len(list((tmp_path / "data" / "history").glob("*.json"))) == 4
    assert len(list((tmp_path / "data" / "trial").glob("*.json"))) == 1
    trend = (tmp_path / "data" / "trend.csv").read_text().splitlines()
    assert len(trend) == 5


def test_an_empty_report_is_an_error_and_records_nothing(tmp_path):
    path = tmp_path / "report.json"
    path.write_text(json.dumps({"benchmarks": {}}))
    status = ph.main(["record", "--report", str(path), "--data-dir", str(tmp_path / "data"),
                      "--dest", "history", "--commit", "a" * 40, "--ref", "x", "--run-id", "1"])
    assert status == 2
    assert not (tmp_path / "data").exists()


def test_unusable_counts_are_an_error_and_record_nothing(tmp_path):
    path = tmp_path / "report.json"
    path.write_text(json.dumps(report({"grep": 1.0})))
    counts = tmp_path / "counts.json"
    counts.write_text(json.dumps({"benchmarks": [{"binary": "x"}]}))
    status = ph.main(["record", "--report", str(path), "--data-dir", str(tmp_path / "data"),
                      "--dest", "history", "--commit", "a" * 40, "--ref", "x", "--run-id", "1",
                      "--instruction-counts", str(counts), "--counts-cpu", CPU])
    assert status == 2
    assert not (tmp_path / "data").exists()


def make(suite_report: dict) -> dict:
    return ph.make_entry(suite_report, commit="a" * 40, ref="refs/heads/master", run_id="1",
                         version="26.11.0", accept=False,
                         recorded_at=datetime(2026, 10, 5, tzinfo=timezone.utc))


def test_the_entry_keeps_what_each_benchmark_measures():
    # The suite says what each number times (#667), the frames over budget
    # of a scroll case (#669), whether a follow case's chart kept up (#670),
    # the parallelism of an indexing (#686) and the spread of the runs (#705).
    suite_report = report({"b": 0.002})
    kept = {
        "measures": "open to last match written",
        "frames_over_budget": {"budget_ms": 16.7, "median": 2.0},
        "chart_following": {"budget_ms": 1000, "kept_up_runs": 7},
        "indexing_parallelism": {"median": 3.1},
        "iqr_seconds": 0.01,
    }
    suite_report["benchmarks"]["b"].update(kept)
    stored = make(suite_report)["benchmarks"]["b"]
    for field, value in kept.items():
        assert stored[field] == value


def test_the_entry_filename_sorts_by_time():
    e = ph.make_entry(report({"grep": 1.0}), commit="b" * 40, ref="r", run_id="123-4/",
                      version="", accept=False,
                      recorded_at=datetime(2026, 10, 5, 4, 23, tzinfo=timezone.utc))
    assert ph.entry_filename(e) == "20261005T042300Z-bbbbbbbbbbbb-123-4.json"


def test_the_table_tells_a_read_of_microseconds_apart():
    # Four decimals of a second would show 0.1 µs and 60 µs both as 0.0000 s.
    runs = series([0.1e-6] * 8 + [60e-6, 60e-6], name="read")
    table = ph.markdown(ph.compare(runs[-1], runs[:-1]), [])
    assert "60.0 µs" in table
    assert "0.1 µs" in table


def test_the_summary_names_the_commit_range_of_a_finding():
    runs = count_series([1_000_000] * 5 + [1_030_000])
    rows = ph.compare(runs[-1], runs[:-1])
    text = ph.markdown(rows, [])
    assert f"{sha(5)[:12]}..{sha(6)[:12]}" in text
    assert any(KEY in a for a in ph.annotations(rows, []))


def test_a_wall_clock_benchmark_that_scatters_within_its_runs_gets_room_for_it():
    # The longest read of a 1 GB indexing: 232 µs on one runner, 515 µs on
    # another, with an IQR of its runs of about 0.5 ms (#676).
    runs = series([232e-6] * 8 + [515e-6, 515e-6], name="read")
    for e in runs[:-2]:
        e["benchmarks"]["read"]["iqr_seconds"] = 529e-6
    assert only(ph.compare(runs[-1], runs[:-1])).status == "ok"


# --- #685: wall-clock within one CPU model ----------------------------------

OTHER_CPU = "Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz"


def test_wall_clock_compares_only_with_runs_of_the_same_cpu_model():
    # Medians differ by up to 24 % between CPU models, by about 1.4 % on one
    # (#675): a run compares only with earlier runs of its own model.
    epyc = series([1.0] * 8)
    xeon = [entry(20 + i, {"grep": 1.25}, cpu=OTHER_CPU) for i in range(3)]
    row = only(ph.compare(entry(30, {"grep": 1.25}, cpu=OTHER_CPU), epyc + xeon))
    assert row.status == "warming-up"
    assert row.history_count == 3
    row = only(ph.compare(entry(31, {"grep": 1.0}), epyc + xeon))
    assert row.status == "ok"
    assert row.history_count == 8


def test_a_run_landing_on_another_cpu_model_is_no_change_point():
    runs = series([1.0] * 8) + [entry(9 + i, {"grep": 1.25}, cpu=OTHER_CPU) for i in range(2)]
    row = only(ph.compare(runs[-1], runs[:-1]))
    assert row.status == "warming-up"
    assert ph.findings([row], []) == []


def test_the_summary_names_the_cpu_model_and_its_earlier_runs():
    runs = series([1.0] * 5) + [entry(10, {"grep": 1.0}, cpu=OTHER_CPU)]
    current = entry(11, {"grep": 1.0})
    info = ph.run_info(current, runs)
    assert info["cpu"] == CPU
    assert info["earlier_runs_of_cpu"] == 5
    text = ph.markdown(ph.compare(current, runs), [], info=info)
    assert f"`{CPU}`" in text
    assert "5 earlier runs of this CPU model" in text


def test_wall_clock_still_files_nothing():
    # Instruction counts stay the gate (#671, #672); the same-model spread
    # that would make wall-clock trustworthy enough to file is measured over
    # eight weeks of nightly runs first (#675).
    runs = series([1.0] * 8 + [1.5, 1.5])
    rows = ph.compare(runs[-1], runs[:-1])
    assert only(rows).status == "regression"
    assert ph.findings(rows, []) == []


# --- #685: a run that scatters too much is recorded, not compared ------------

def noisy(e: dict, cv: float = 61.0) -> dict:
    e["median_cv_percent"] = cv
    e["usable"] = False
    return e


def test_the_entry_records_the_runs_median_cv_and_whether_it_is_usable():
    calm = report({"a": 1.0, "b": 2.0, "c": 3.0})
    assert make(calm)["median_cv_percent"] == 2.0
    assert make(calm)["usable"] is True
    scattered = report({"a": 1.0, "b": 2.0, "c": 3.0})
    for name, cv in (("a", 10.0), ("b", 61.0), ("c", 94.0)):
        scattered["benchmarks"][name]["cv_percent"] = cv
    stored = make(scattered)
    assert stored["median_cv_percent"] == 61.0
    assert stored["usable"] is False


def test_a_cv_of_exactly_the_limit_is_still_usable():
    suite_report = report({"a": 1.0})
    suite_report["benchmarks"]["a"]["cv_percent"] = ph.MAX_USABLE_CV_PERCENT
    assert make(suite_report)["usable"] is True


def test_unusable_runs_are_left_out_of_the_series():
    runs = series([1.0] * 6) + [noisy(e) for e in series([3.0] * 3, start=7)]
    row = only(ph.compare(entry(10, {"grep": 1.0}), runs))
    assert row.status == "ok"
    assert row.history_count == 6
    assert ph.run_info(entry(10, {"grep": 1.0}), runs)["earlier_runs_of_cpu"] == 6


def test_an_unusable_run_is_not_compared_but_its_counts_are():
    runs = [entry(i + 1, {"grep": 1.0}, counts=counts_block({KEY: 1_000_000}))
            for i in range(8)]
    current = noisy(entry(9, {"grep": 5.0}, counts=counts_block({KEY: 1_100_000})))
    rows = ph.compare(current, runs)
    assert only(rows_of(rows, "wall-clock")).status == "unusable"
    assert only(rows_of(rows, "instructions")).status == "regression"
    assert [f["metric"] for f in ph.findings(rows, [])] == ["instructions"]


def test_an_unusable_run_checks_no_wall_clock_budget():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.30))
    current = noisy(entry(2, {"gui_open_log_1gb_indexed": 0.90}))
    row = only(ph.check_budgets(data, current, []))
    assert row.status == "not-measured"
    assert ph.findings([], [row]) == []


def test_a_broken_budget_looks_past_unusable_runs_for_the_last_good_one():
    data = budgets_file(budget_entry("gui_open_log_1gb_indexed", 0.30))
    # The unusable run cannot tell, so the range spans it.
    runs = series([0.25] * 2 + [0.40] * 3, name="gui_open_log_1gb_indexed")
    noisy(runs[2])
    row = only(ph.check_budgets(data, runs[-1], runs[:-1]))
    assert row.last_good["commit"] == sha(2)
    assert row.first_bad["commit"] == sha(4)


def test_the_summary_flags_an_unusable_run():
    current = noisy(entry(2, {"grep": 1.0}))
    info = ph.run_info(current, series([1.0]))
    text = ph.markdown(ph.compare(current, series([1.0])), [], info=info)
    assert "unusable" in text.lower()
    assert "61.0 %" in text


# --- #685: the ratio to a fixed reference build on the same runner -----------

def reference_report(medians: dict[str, float], cv: float = 2.0) -> dict:
    out = report(medians)
    for result in out["benchmarks"].values():
        result["cv_percent"] = cv
    return out


def test_the_entry_records_the_ratio_to_the_reference_build():
    stored = ph.make_entry(report({"grep": 1.2, "open": 2.0, "new": 0.5}), commit="a" * 40,
                           ref="refs/heads/master", run_id="1", version="", accept=False,
                           recorded_at=datetime(2026, 10, 5, tzinfo=timezone.utc),
                           reference=reference_report({"grep": 1.0, "open": 2.5, "gone": 3.0}),
                           reference_tag="v26.10.0", reference_commit="b" * 40)
    assert stored["reference"]["tag"] == "v26.10.0"
    assert stored["reference"]["commit"] == "b" * 40
    assert stored["reference"]["usable"] is True
    assert stored["reference"]["benchmarks"]["grep"] == {"median_seconds": 1.0, "cv_percent": 2.0}
    # Only a benchmark both sides measured has a ratio.
    assert stored["ratios"] == {"grep": pytest.approx(1.2), "open": pytest.approx(0.8)}


def test_a_reference_that_scatters_too_much_is_flagged():
    stored = ph.make_entry(report({"grep": 1.2}), commit="a" * 40, ref="r", run_id="1",
                           version="", accept=False,
                           recorded_at=datetime(2026, 10, 5, tzinfo=timezone.utc),
                           reference=reference_report({"grep": 1.0}, cv=40.0),
                           reference_tag="v26.10.0")
    assert stored["reference"]["usable"] is False
    assert stored["ratios"] == {"grep": pytest.approx(1.2)}


def test_the_wall_clock_rows_carry_the_ratio():
    current = entry(2, {"grep": 1.2, "new": 0.5})
    current["ratios"] = {"grep": 1.2}
    rows = {r.name: r for r in ph.compare(current, series([1.0]))}
    assert rows["grep"].ratio == 1.2
    assert rows["new"].ratio is None


def test_the_summary_shows_the_reference_and_what_only_one_side_measured():
    current = entry(2, {"grep": 1.2, "new": 0.5})
    current["reference"] = {"tag": "v26.10.0", "commit": "b" * 40, "usable": True,
                            "median_cv_percent": 2.0,
                            "benchmarks": {"grep": {"median_seconds": 1.0},
                                           "gone": {"median_seconds": 3.0}}}
    current["ratios"] = {"grep": 1.2}
    info = ph.run_info(current, [])
    text = ph.markdown(ph.compare(current, []), [], info=info)
    assert "v26.10.0" in text
    assert "1.200" in text
    assert "only this commit: new" in text
    assert "only the reference: gone" in text


def run_record_with_reference(tmp_path: Path, medians: dict[str, float],
                              reference: dict | str | None, day: int = 1,
                              cv: float = 2.0) -> tuple[int, dict]:
    extra = []
    if reference is not None:
        path = tmp_path / f"reference-{day}.json"
        path.write_text(reference if isinstance(reference, str)
                        else json.dumps(reference_report(reference, cv=cv)))
        extra = ["--reference-report", str(path), "--reference-tag", "v26.10.0",
                 "--reference-commit", "b" * 40]
    return run_record(tmp_path, medians, day=day, extra=extra)


def test_record_keeps_the_reference_and_writes_ratio_columns(tmp_path):
    status, verdict = run_record_with_reference(tmp_path, {"grep": 1.2, "new": 0.5},
                                                {"grep": 1.0})
    assert status == 0
    stored = json.loads(next((tmp_path / "data" / "history").glob("*.json")).read_text())
    assert stored["ratios"] == {"grep": pytest.approx(1.2)}
    assert verdict["run_info"]["reference"]["tag"] == "v26.10.0"
    trend = list(csv.DictReader(io.StringIO((tmp_path / "data" / "trend.csv").read_text())))
    assert trend[0]["reference"] == "v26.10.0"
    assert float(trend[0]["ratio: grep"]) == pytest.approx(1.2)
    assert "ratio: new" not in trend[0]  # a column only for what had a reference
    assert trend[0]["cpu"] == CPU
    assert trend[0]["median_cv_percent"] == "2.0"
    assert trend[0]["unusable"] == ""


def test_trend_flags_an_unusable_run_and_an_unusable_reference(tmp_path):
    run_record_with_reference(tmp_path, {"grep": 1.2}, {"grep": 1.0}, cv=40.0)
    trend = list(csv.DictReader(io.StringIO((tmp_path / "data" / "trend.csv").read_text())))
    assert trend[0]["unusable"] == "reference"


def test_an_unreadable_reference_report_records_the_run_without_ratios(tmp_path):
    status, verdict = run_record_with_reference(tmp_path, {"grep": 1.0}, "{not json")
    assert status == 0
    stored = json.loads(next((tmp_path / "data" / "history").glob("*.json")).read_text())
    assert "ratios" not in stored
    assert "reference report" in (tmp_path / "summary.md").read_text()


def test_record_without_a_reference_has_no_ratio_columns(tmp_path):
    run_record(tmp_path, {"grep": 1.0})
    header = (tmp_path / "data" / "trend.csv").read_text().splitlines()[0]
    assert "ratio:" not in header


# --- #685: the spread within one CPU model, for #675 -------------------------

def with_reference(e: dict, medians: dict[str, float], tag: str = "v26.10.0") -> dict:
    e["reference"] = {"tag": tag, "usable": True,
                      "benchmarks": {n: {"median_seconds": m} for n, m in medians.items()}}
    e["ratios"] = {n: e["benchmarks"][n]["median_seconds"] / m
                   for n, m in medians.items() if n in e["benchmarks"]}
    return e


def test_spread_is_the_cv_of_the_medians_within_each_cpu_model():
    epyc = [with_reference(entry(i + 1, {"grep": v}), {"grep": r})
            for i, (v, r) in enumerate([(1.0, 2.0), (1.1, 2.2), (0.9, 1.8)])]
    xeon = [with_reference(entry(10 + i, {"grep": 1.3}, cpu=OTHER_CPU), {"grep": 2.6})
            for i in range(2)]
    result = ph.spread(epyc + xeon)
    models = {m["cpu"]: m for m in result["models"]}
    assert models[CPU]["runs"] == 3
    assert models[CPU]["medians"]["benchmarks"]["grep"]["cv_percent"] == pytest.approx(10.0)
    assert models[CPU]["reference"]["v26.10.0"]["median_cv_percent"] == pytest.approx(10.0)
    assert models[CPU]["ratios"]["median_cv_percent"] == pytest.approx(0.0)
    # A model with fewer than two runs has no spread yet; the series of all
    # models together is the comparison #675 made.
    assert models[OTHER_CPU]["runs"] == 2
    assert result["all_models"]["runs"] == 5
    assert result["all_models"]["medians"]["median_cv_percent"] > 10.0


def test_spread_leaves_out_unusable_runs_and_unusable_references():
    runs = [with_reference(entry(i + 1, {"grep": 1.0}), {"grep": 2.0}) for i in range(3)]
    runs.append(noisy(with_reference(entry(5, {"grep": 9.0}), {"grep": 9.0})))
    late = with_reference(entry(6, {"grep": 1.0}), {"grep": 7.0})
    late["reference"]["usable"] = False
    model = only(ph.spread(runs + [late])["models"])
    assert model["runs"] == 4
    assert model["medians"]["benchmarks"]["grep"]["cv_percent"] == 0.0
    assert model["reference"]["v26.10.0"]["runs"] == 3


def test_spread_keeps_the_reference_tags_apart():
    runs = [with_reference(entry(1, {"grep": 1.0}), {"grep": 2.0}),
            with_reference(entry(2, {"grep": 1.0}), {"grep": 2.0}),
            with_reference(entry(3, {"grep": 1.0}), {"grep": 1.0}, tag="v26.11.0")]
    model = only(ph.spread(runs)["models"])
    assert model["reference"]["v26.10.0"]["median_cv_percent"] == 0.0
    assert model["reference"]["v26.11.0"]["runs"] == 1


def test_the_spread_command_writes_markdown_and_json(tmp_path):
    for day in range(1, 4):
        run_record_with_reference(tmp_path, {"grep": 1.0 + day / 100}, {"grep": 2.0}, day=day)
    out_md, out_json = tmp_path / "spread.md", tmp_path / "spread.json"
    assert ph.main(["spread", "--data-dir", str(tmp_path / "data"),
                    "--markdown", str(out_md), "--json", str(out_json)]) == 0
    text = out_md.read_text()
    assert CPU in text
    assert "#675" in text
    data = json.loads(out_json.read_text())
    assert only(data["models"])["runs"] == 3


def test_the_spread_command_can_start_at_a_date(tmp_path):
    for day in range(1, 5):
        run_record(tmp_path, {"grep": 1.0}, day=day)
    out_json = tmp_path / "spread.json"
    ph.main(["spread", "--data-dir", str(tmp_path / "data"), "--since", "2026-10-03",
             "--json", str(out_json)])
    assert only(json.loads(out_json.read_text())["models"])["runs"] == 2
