"""Tests for perf-budgets.py (#676): checking a performance run against the
Budgets of ADR 0018, kept in tests/e2e/budgets.json."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "perf_budgets", Path(__file__).with_name("perf-budgets.py"))
perf_budgets = importlib.util.module_from_spec(_SPEC)
sys.modules["perf_budgets"] = perf_budgets
_SPEC.loader.exec_module(perf_budgets)


def budget(benchmark: str = "gui_open_log_1gb_indexed", value: float = 0.30,
           noise: float = 5, **extra) -> dict:
    return {
        "scenario": "open-and-index",
        "file": "1 GB Log File, generated",
        "metric": "open to Index finished, median of the runs",
        "report": "e2e",
        "benchmark": benchmark,
        "field": "median_seconds",
        "unit": "s",
        "budget": value,
        "noise_margin_percent": noise,
        "source": ["perf-1"],
        **extra,
    }


def budgets(*entries: dict) -> dict:
    return {
        "schema": 1,
        "sources": {"perf-1": {"workflow": "Performance", "run": 1, "commit": "a" * 40}},
        "budgets": {e["benchmark"]: e for e in entries},
    }


def e2e(**medians: float) -> dict:
    return {"benchmarks": {n: {"median_seconds": m} for n, m in medians.items()}}


def only(rows):
    assert len(rows) == 1
    return rows[0]


# --- the budget and its noise margin ----------------------------------------

def test_a_median_within_its_budget_keeps_it():
    rows = perf_budgets.check(budgets(budget(value=0.30)), e2e=e2e(gui_open_log_1gb_indexed=0.25))
    assert only(rows).status == "ok"
    assert not perf_budgets.failed(rows)


def test_a_median_beyond_the_budget_and_its_noise_margin_breaks_it():
    rows = perf_budgets.check(budgets(budget(value=0.30, noise=5)),
                              e2e=e2e(gui_open_log_1gb_indexed=0.316))
    assert only(rows).status == "broken"
    assert only(rows).limit == pytest.approx(0.315)
    assert perf_budgets.failed(rows)


def test_a_median_within_the_noise_margin_keeps_the_budget():
    rows = perf_budgets.check(budgets(budget(value=0.30, noise=5)),
                              e2e=e2e(gui_open_log_1gb_indexed=0.314))
    assert only(rows).status == "ok"


# --- what was not measured --------------------------------------------------

def test_a_budgeted_benchmark_the_report_lacks_is_missing():
    # A scenario that silently stops running must not keep its Budget.
    rows = perf_budgets.check(budgets(budget()), e2e=e2e(gui_startup_version=0.01))
    assert only(rows).status == "missing"
    assert perf_budgets.failed(rows)


def test_a_budget_of_a_report_not_given_is_not_measured():
    # The memory Budgets come from the instruction counts, which a run of
    # the e2e suite alone does not have.
    rows = perf_budgets.check(budgets(budget(report="instruction-counts")), e2e=e2e())
    assert only(rows).status == "not-measured"
    assert not perf_budgets.failed(rows)


# --- memory, from the instruction counts -------------------------------------

def memory_budget(value: float) -> dict:
    return budget(
        benchmark="logsquirl_linepositionarray_benchmark / Line positions of an Index / "
                  "append, line by line",
        report="instruction-counts", field="peak_heap_bytes", unit="bytes per million Log Lines",
        scale=0.5, value=value, noise=0)


def instruction_counts(peak_heap_bytes: int) -> dict:
    return {"schema_version": 1, "benchmarks": [{
        "binary": "logsquirl_linepositionarray_benchmark",
        "name": "Line positions of an Index / append, line by line",
        "instructions": 56232403, "allocations": 37, "peak_heap_bytes": peak_heap_bytes,
    }]}


def test_the_memory_of_an_index_is_its_peak_heap_per_million_log_lines():
    # The benchmark indexes 2 million Log Lines: 6,489,088 bytes at its peak
    # in CI Build run 36961939625 are 3,244,544 per million.
    entry = memory_budget(3_300_000)
    row = only(perf_budgets.check(budgets(entry), instruction_counts=instruction_counts(6_489_088)))
    assert row.measured == 3_244_544
    assert row.status == "ok"
    row = only(perf_budgets.check(budgets(entry), instruction_counts=instruction_counts(6_700_000)))
    assert row.status == "broken"


# --- the budgets file --------------------------------------------------------

def write(tmp_path: Path, data: dict) -> Path:
    path = tmp_path / "budgets.json"
    path.write_text(json.dumps(data))
    return path


def test_the_budgets_file_loads(tmp_path):
    loaded = perf_budgets.load(write(tmp_path, budgets(budget())))
    assert loaded["budgets"]["gui_open_log_1gb_indexed"]["budget"] == 0.30


@pytest.mark.parametrize("key", ["unit", "budget", "noise_margin_percent", "source", "metric"])
def test_a_budget_without_what_the_adr_asks_for_is_refused(tmp_path, key):
    entry = budget()
    del entry[key]
    with pytest.raises(perf_budgets.BudgetError, match=key):
        perf_budgets.load(write(tmp_path, budgets(entry)))


def test_a_budget_must_cite_a_known_measurement(tmp_path):
    with pytest.raises(perf_budgets.BudgetError, match="perf-2"):
        perf_budgets.load(write(tmp_path, budgets(budget(source=["perf-2"]))))


@pytest.mark.parametrize("change", [{"budget": 0}, {"noise_margin_percent": -1},
                                    {"report": "valgrind"}])
def test_a_budget_that_cannot_be_checked_is_refused(tmp_path, change):
    with pytest.raises(perf_budgets.BudgetError):
        perf_budgets.load(write(tmp_path, budgets(budget(**change))))


def test_an_unknown_schema_is_refused(tmp_path):
    data = budgets(budget())
    data["schema"] = 2
    with pytest.raises(perf_budgets.BudgetError, match="schema"):
        perf_budgets.load(write(tmp_path, data))


# --- the command line --------------------------------------------------------

def run_check(tmp_path: Path, data: dict, report: dict) -> tuple[int, dict, str]:
    report_path = tmp_path / "benchmark_report.json"
    report_path.write_text(json.dumps(report))
    out, md = tmp_path / "verdict.json", tmp_path / "budgets.md"
    status = perf_budgets.main(["check", "--budgets", str(write(tmp_path, data)),
                                "--e2e", str(report_path), "--json", str(out),
                                "--markdown", str(md)])
    verdict = json.loads(out.read_text()) if out.exists() else {}
    return status, verdict, md.read_text(encoding="utf-8") if md.exists() else ""


def test_a_run_within_every_budget_passes(tmp_path):
    status, verdict, table = run_check(tmp_path, budgets(budget()),
                                       e2e(gui_open_log_1gb_indexed=0.25))
    assert status == 0
    assert verdict["failed"] is False
    assert "| open-and-index | gui_open_log_1gb_indexed | 0.25 s | 0.3 s | ok |" in table


def test_a_broken_budget_fails_the_run(tmp_path):
    status, verdict, table = run_check(tmp_path, budgets(budget()),
                                       e2e(gui_open_log_1gb_indexed=0.4))
    assert status == 1
    assert verdict["rows"][0]["status"] == "broken"
    assert "**BROKEN**" in table


def test_proposed_budgets_are_reported_and_fail_nothing(tmp_path):
    # ADR 0018: until the maintainer accepts the Budgets, nothing fails on them.
    data = {**budgets(budget()), "status": "proposed: awaits the maintainer's approval"}
    status, verdict, table = run_check(tmp_path, data, e2e(gui_open_log_1gb_indexed=0.4))
    assert status == 0
    assert verdict["failed"] is False
    assert verdict["enforced"] is False
    assert verdict["rows"][0]["status"] == "broken"
    assert "**BROKEN**" in table
    assert "proposed" in table


@pytest.mark.parametrize("status, enforced", [
    ("proposed: awaits the maintainer's approval", False),
    ("proposed", False),
    ("accepted", True),
    (None, True),
])
def test_budgets_are_enforced_once_accepted(status, enforced):
    data = budgets(budget())
    if status is not None:
        data["status"] = status
    assert perf_budgets.enforced(data) is enforced


def test_an_unusable_budgets_file_is_an_error(tmp_path):
    data = budgets(budget())
    del data["budgets"]["gui_open_log_1gb_indexed"]["unit"]
    status, verdict, _ = run_check(tmp_path, data, e2e(gui_open_log_1gb_indexed=0.25))
    assert status == 2
    assert verdict == {}


# --- the Budgets of ADR 0018 -------------------------------------------------

REPO_BUDGETS = Path(__file__).resolve().parents[2] / "tests" / "e2e" / "budgets.json"


def test_the_budgets_of_the_adr_load():
    data = perf_budgets.load(REPO_BUDGETS)
    assert (REPO_BUDGETS.parents[2] / data["adr"]).is_file()
    scenarios = {e["scenario"] for e in data["budgets"].values()}
    assert scenarios == {"startup", "open-and-index", "search", "grep", "quickfind", "scroll",
                         "follow", "session-restore", "read-while-indexing", "memory"}


def test_every_budget_holds_what_it_was_derived_from():
    # A Budget is derived from measurements, with its headroom stated: it is
    # never below the slowest of them, and the headroom is what it says.
    for key, entry in perf_budgets.load(REPO_BUDGETS)["budgets"].items():
        worst = max(entry["measured"].values())
        assert entry["worst"] == worst, key
        assert entry["budget"] >= worst, key
        if entry["headroom_percent"] is not None:
            assert entry["budget"] / worst - 1 == pytest.approx(
                entry["headroom_percent"] / 100, abs=0.001), key
        else:
            assert entry.get("note"), key


def test_the_memory_is_shown_in_megabytes():
    rows = perf_budgets.check(budgets(memory_budget(4_100_000)),
                    instruction_counts=instruction_counts(6_489_088))
    assert "3.24 MB" in perf_budgets.markdown(rows)
