"""Tests for perf-issues.py (#677): which issue the nightly Performance run
opens, updates or leaves alone for its findings, and what the issue says."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "perf_issues", Path(__file__).with_name("perf-issues.py"))
pi = importlib.util.module_from_spec(_SPEC)
sys.modules["perf_issues"] = pi
_SPEC.loader.exec_module(pi)

SERVER = "https://github.com"
REPO = "64x-lunicorn/LogSquirl"
BIN = "logsquirl_logdata_benchmark"
KEY = f"{BIN} / Indexing a Log File / short lines: whole Log File"
GOOD = "a" * 40
BAD = "b" * 40


def ref(commit: str, run: str = "100-1") -> dict:
    return {"commit": commit, "recorded_at": "2026-10-05T02:41:00Z", "run_id": run,
            "ref": "refs/heads/master"}


def regression(benchmark: str = KEY, scenario: str = BIN, first_bad: str = BAD) -> dict:
    return {"kind": "regression", "metric": "instructions", "scenario": scenario,
            "benchmark": benchmark, "measured": 1_030_000, "reference": 1_000_000,
            "limit": 1_020_000, "delta_percent": 3.0, "streak": 1,
            "last_good": ref(GOOD, "99-1"), "first_bad": ref(first_bad)}


def broken_budget() -> dict:
    return {"kind": "budget", "metric": "budget", "status": "broken",
            "scenario": "open-and-index", "benchmark": "gui_open_log_1gb_indexed",
            "budget_key": "gui_open_log_1gb_indexed", "measured": 0.40, "budget": 0.32,
            "limit": 0.352, "unit": "s", "last_good": None, "first_bad": ref(BAD)}


def comparison(*found: dict, dest: str = "history") -> dict:
    return {"schema": 2, "failed": bool(found), "dest": dest, "run": ref(BAD, "200-1"),
            "findings": list(found)}


def issue(number: int, body: str, state: str = "OPEN") -> dict:
    return {"number": number, "state": state, "title": "t", "body": body}


def plan(*found: dict, issues: list[dict] | None = None) -> list[dict]:
    return pi.plan(comparison(*found), issues or [], server_url=SERVER, repo=REPO)


def only(items):
    assert len(items) == 1
    return items[0]


# --- one issue per scenario -------------------------------------------------

def test_a_new_finding_opens_an_issue_for_its_scenario():
    action = only(plan(regression()))
    assert action["action"] == "create"
    assert action["scenario"] == BIN
    assert action["labels"] == ["needs-triage", "performance"]
    assert BIN in action["title"]
    assert pi.scenario_marker(BIN) in action["body"]


def test_findings_of_one_scenario_share_an_issue_and_of_two_do_not():
    actions = plan(regression(), regression(benchmark=f"{BIN} / other"), broken_budget())
    assert sorted(a["scenario"] for a in actions) == [BIN, "open-and-index"]
    logdata = only([a for a in actions if a["scenario"] == BIN])
    assert KEY in logdata["body"]
    assert f"{BIN} / other" in logdata["body"]


def test_the_issue_names_the_commit_range_and_the_scenario():
    body = only(plan(regression()))["body"]
    assert f"{SERVER}/{REPO}/compare/{GOOD}...{BAD}" in body
    assert f"{GOOD[:12]}..{BAD[:12]}" in body
    assert f"git log --oneline {GOOD}..{BAD}" in body
    assert "1,030,000" in body
    assert "+3.0 %" in body
    assert f"{SERVER}/{REPO}/actions/runs/200/attempts/1" in body


def test_a_budget_broken_from_the_first_run_names_its_first_commit():
    body = only(plan(broken_budget()))["body"]
    assert f"{SERVER}/{REPO}/commit/{BAD}" in body
    assert "400.0 ms" in body or "0.4 s" in body
    assert "Budget" in body


# --- updating, not duplicating ---------------------------------------------

def test_an_open_issue_of_the_scenario_is_updated_not_duplicated():
    first = only(plan(regression()))
    actions = plan(regression(), issues=[issue(7, first["body"])])
    assert [a["action"] for a in actions] == ["edit"]
    assert actions[0]["number"] == 7


def test_a_new_finding_in_an_open_issue_adds_a_comment():
    first = only(plan(regression()))
    actions = plan(regression(), regression(benchmark=f"{BIN} / other"),
                   issues=[issue(7, first["body"])])
    assert [a["action"] for a in actions] == ["edit", "comment"]
    assert all(a["number"] == 7 for a in actions)
    # The comment names what is new; the description lists them all.
    assert f"{BIN} / other" in actions[1]["body"]
    assert "short lines" not in actions[1]["body"]
    assert "short lines" in actions[0]["body"]


def test_an_issue_closed_for_the_same_findings_stays_closed():
    first = only(plan(regression()))
    action = only(plan(regression(), issues=[issue(7, first["body"], state="CLOSED")]))
    assert action["action"] == "skip"
    assert action["number"] == 7


def test_a_new_change_point_after_a_closed_issue_opens_a_new_one():
    first = only(plan(regression()))
    action = only(plan(regression(first_bad="c" * 40),
                       issues=[issue(7, first["body"], state="CLOSED")]))
    assert action["action"] == "create"


def test_issues_of_other_scenarios_are_left_alone():
    other = only(plan(broken_budget()))
    assert only(plan(regression(), issues=[issue(3, other["body"])]))["action"] == "create"


def test_the_fingerprint_ignores_the_numbers_of_the_night():
    a = regression()
    b = {**regression(), "measured": 1_040_000, "delta_percent": 4.0, "streak": 2}
    assert pi.fingerprint([a]) == pi.fingerprint([b])
    assert pi.fingerprint([a]) != pi.fingerprint([regression(first_bad="c" * 40)])


# --- only master files ------------------------------------------------------

def test_a_run_of_a_branch_files_nothing():
    with pytest.raises(ValueError, match="trial"):
        pi.plan(comparison(regression(), dest="trial"), [], server_url=SERVER, repo=REPO)


def test_no_findings_no_actions():
    assert plan() == []


# --- text that comes from the run ------------------------------------------

def test_a_benchmark_name_cannot_break_the_table_or_mention_anyone():
    body = only(plan(regression(benchmark=f"{BIN} / a | b @someone")))["body"]
    assert "a \\| b" in body
    assert "@someone" not in body


# --- the command ------------------------------------------------------------

def test_the_command_writes_the_plan_and_the_bodies(tmp_path):
    (tmp_path / "comparison.json").write_text(json.dumps(comparison(regression())))
    (tmp_path / "issues.json").write_text("[]")
    status = pi.main(["plan", "--comparison", str(tmp_path / "comparison.json"),
                      "--issues", str(tmp_path / "issues.json"), "--server-url", SERVER,
                      "--repo", REPO, "--out", str(tmp_path / "plan")])
    assert status == 0
    actions = json.loads((tmp_path / "plan" / "plan.json").read_text())
    action = only(actions)
    assert action["action"] == "create"
    assert "body" not in action
    body = Path(action["body_file"]).read_text()
    assert pi.scenario_marker(BIN) in body


def test_the_command_refuses_a_branch_run(tmp_path):
    (tmp_path / "comparison.json").write_text(json.dumps(comparison(regression(), dest="trial")))
    (tmp_path / "issues.json").write_text("[]")
    status = pi.main(["plan", "--comparison", str(tmp_path / "comparison.json"),
                      "--issues", str(tmp_path / "issues.json"), "--server-url", SERVER,
                      "--repo", REPO, "--out", str(tmp_path / "plan")])
    assert status == 2
    assert not (tmp_path / "plan" / "plan.json").exists()


# --- acceptance (#677): a synthetic regression files an issue with its range

def test_a_synthetic_regression_files_an_issue_naming_its_commit_range(tmp_path):
    spec = importlib.util.spec_from_file_location(
        "perf_history", Path(__file__).with_name("perf-history.py"))
    ph = sys.modules.get("perf_history")
    if ph is None:
        ph = importlib.util.module_from_spec(spec)
        sys.modules["perf_history"] = ph
        spec.loader.exec_module(ph)

    def night(day: int, instructions: int) -> int:
        report = tmp_path / f"report-{day}.json"
        report.write_text(json.dumps({"system": {"cpu": "EPYC"}, "benchmarks": {
            "grep_log_1gb_simple": {"median_seconds": 0.8}}}))
        counts = tmp_path / f"counts-{day}.json"
        binary, name = KEY.split(" / ", 1)
        counts.write_text(json.dumps({"schema_version": 1, "failed_binaries": [], "benchmarks": [
            {"binary": binary, "name": name, "instructions": instructions,
             "allocations": 1, "peak_heap_bytes": 1}]}))
        return ph.main([
            "record", "--report", str(report), "--data-dir", str(tmp_path / "data"),
            "--dest", "history", "--commit", f"{day:040x}", "--ref", "refs/heads/master",
            "--run-id", f"{1000 + day}-1", "--recorded-at", f"2026-10-{day:02d}T02:41:00Z",
            "--instruction-counts", str(counts), "--counts-cpu", "EPYC",
            "--counts-log-file-mb", "32", "--json", str(tmp_path / "comparison.json"),
        ])

    for day in range(1, 6):
        assert night(day, 50_000_000) == 0
    # The test commit of day 6 costs 3 % more instructions.
    assert night(6, 51_500_000) == 1

    (tmp_path / "issues.json").write_text("[]")
    assert pi.main(["plan", "--comparison", str(tmp_path / "comparison.json"),
                    "--issues", str(tmp_path / "issues.json"), "--server-url", SERVER,
                    "--repo", REPO, "--out", str(tmp_path / "plan")]) == 0
    action = only(json.loads((tmp_path / "plan" / "plan.json").read_text()))
    assert action["action"] == "create"
    assert action["labels"] == ["needs-triage", "performance"]
    assert action["scenario"] == BIN
    body = Path(action["body_file"]).read_text()
    good, bad = f"{5:040x}", f"{6:040x}"
    assert f"{SERVER}/{REPO}/compare/{good}...{bad}" in body
    assert f"git log --oneline {good}..{bad}" in body
    assert "+3.0 %" in body


def test_the_workflow_finds_the_issues_by_the_marker_they_carry():
    # The issues job hands plan every issue, open or closed, whose body holds
    # the marker, so a closed one with the same findings stays closed.
    workflow = (Path(__file__).resolve().parents[1] / "workflows" / "performance.yml").read_text()
    assert f"--search '\"{pi.MARKER}\" in:body'" in workflow
    assert "--state all" in workflow
    body = pi.render_body(BIN, [regression()], ref(BAD), server_url=SERVER, repo=REPO)
    assert pi.MARKER in body
