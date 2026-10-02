#!/usr/bin/env python3
"""Finds the change points of a nightly performance run, checks its Budgets and records it (#441, #677, #685).

The Performance workflow (.github/workflows/performance.yml) measures master
every night on a GitHub-hosted runner, two ways:

- wall-clock: the e2e performance suite's median per benchmark
  (benchmark_report.json), the benchmark mode's scenarios on the large
  generated Log Files;
- instruction counts: every Catch2 benchmark of tests/benchmarks run once
  under Callgrind (instruction-counts.py's side file, after.json), which a
  shared runner repeats to within a fraction of a percent.

Each benchmark's values over the recorded runs form a series, and a
regression is a change point in it (perf_changepoint.py): a run from which
on every run is above the limit of the runs before it, lasting long enough.
The run before it is the last good one, the run itself the first bad one,
and the commits between the two are where it came from.

- Instruction counts (INSTRUCTIONS): a change point is a regression from its
  first run on; the tolerance is the benchmark's threshold of the pull
  request gate (instruction-counts.py, +2 % by default). Counts compare only
  between runs on one CPU model and with one size of generated Log Files.
- Wall-clock (WALL_CLOCK): the change point must last two runs, above 10 %
  and the absolute margin of perf_margin.py (half the reference, at most
  10 ms, or a Budget's min_delta_seconds) and three interquartile ranges of
  the reference runs' medians, or of the runs within them when larger. A
  median compares only with earlier runs on the same runner CPU model
  (system.cpu): medians differ by up to 24 % between models and by about
  1.4 % on one (#675), so a model without enough history only reports. It
  is the trend: it shows in the summary and files no issue
  (WALL_CLOCK_FILES_ISSUES); instruction counts are the gate.
- A run whose median within-run CV is above MAX_USABLE_CV_PERCENT (one in
  four runs had 61 %, #675) is recorded but unusable: its wall-clock is
  neither compared nor part of any later run's series, and its e2e Budgets
  are not checked.
- The reference build (#685): the workflow builds the last release tag and
  runs the same suite on it on the same runner; the run records each
  benchmark's ratio this run / reference, which does not depend on the CPU
  model the run landed on. It is recorded and shown, never a finding.
- Budgets (ADR 0018, tests/e2e/budgets.json, perf-budgets.py): a Budget this
  run breaks is a finding, with the runs since it broke. While the file's
  status is proposed, the Budgets are checked and shown in the summary and are
  no finding: no issue, not red.
- A benchmark the previous run measured and this one did not is a finding.

The findings (in --json) are what the workflow files issues from
(perf-issues.py); this script only reads and writes files.

The history lives on the perf-data branch, one JSON file per run:

  history/<recorded_at>-<commit>-<run id>.json   runs of the default branch
  trial/<recorded_at>-<commit>-<run id>.json     runs dispatched from any other
                                                 branch; compared with
                                                 history/, never compared with
  trend.csv                                      one row per history/ run

A run recorded with --accept starts a new level: series from then on start
at that run (an intentional slowdown is accepted this way). The accepting
run itself has no findings but broken Budgets.

Usage:
  perf-history.py record --report benchmark_report.json --data-dir DIR \\
      --dest history|trial --commit SHA --ref REF --run-id ID [--version V] \\
      [--instruction-counts after.json --counts-cpu CPU [--counts-log-file-mb N]] \\
      [--reference-report reference_report.json --reference-tag TAG [--reference-commit SHA]] \\
      [--budgets tests/e2e/budgets.json] [--accept] [--markdown OUT.md] [--json OUT.json]
  perf-history.py spread --data-dir DIR [--since YYYY-MM-DD] [--markdown OUT.md] [--json OUT.json]

`spread` computes, per runner CPU model, how much the medians of the usable
history/ runs vary (the CV of each benchmark's medians), for the reference
build's medians (fixed code: the runner's own spread) and for the ratios; it
is the number #675 re-decides a dedicated benchmark runner on.

Exit status of record: 0 without findings, 1 with one (the run is recorded
all the same), 2 on unusable input (nothing is recorded). An unreadable
reference report only leaves the ratios out.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import statistics
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

import perf_changepoint
from perf_common import format_seconds, load_script

perf_budgets = load_script("perf_budgets", "perf-budgets.py")
instruction_counts = load_script("instruction_counts", "instruction-counts.py")

WALL_CLOCK = perf_changepoint.Rule(window=14, min_history=6, persistence=2,
                                   tolerance_percent=10.0, min_delta_cap=0.010)
INSTRUCTIONS = perf_changepoint.Rule(
    window=14, min_history=3, persistence=1,
    tolerance_percent=instruction_counts.DEFAULT_THRESHOLD_PERCENT, min_delta_cap=0.0)
# Wall-clock is compared within one runner CPU model (#685), but the spread
# of one model is known from a single pair of runs (1.4 %, #675). Instruction
# counts stay the gate; whether wall-clock may file issues is re-decided with
# the spread `spread` measures over eight weeks of nightly runs.
WALL_CLOCK_FILES_ISSUES = False
# A run whose median within-run CV is above this is recorded, not compared
# (#685): run 36156375955 had 61 %, the others 4-7 % (#675).
MAX_USABLE_CV_PERCENT = 20.0
SCHEMA = 1
COMPARISON_SCHEMA = 2
COUNTS_MISSING = "instruction counts"

# What a recorded benchmark keeps of the suite's report: the statistics and
# what the benchmark times (#667), for a scroll case the frames over budget
# (#669), for a follow case whether its chart kept up (#670) and for a
# read-while-indexing case the parallelism of its indexing (#686), and the
# interquartile range of the runs (#705); not the raw runs, which the Actions
# artifact of the run still holds.
KEPT_FIELDS = (
    "median_seconds", "mean_seconds", "std_seconds", "cv_percent",
    "p5_seconds", "p95_seconds", "iqr_seconds", "min_seconds", "max_seconds",
    "filtered_count", "total_count", "throughput", "measures", "frames_over_budget",
    "chart_following", "indexing_parallelism",
)
KEPT_COUNTS = ("instructions", "allocations", "peak_heap_bytes")

# The scenario of a wall-clock benchmark without a Budget, by its name
# (tests/e2e/test_performance.py names them).
SCENARIO_PREFIXES = (
    ("gui_startup", "startup"),
    ("gui_open_", "open-and-index"),
    ("gui_search_", "search"),
    ("grep_", "grep"),
    ("gui_quickfind_", "quickfind"),
    ("gui_scroll_", "scroll"),
    ("gui_follow_", "follow"),
    ("gui_session_restore_", "session-restore"),
    ("gui_read_while_indexing_", "read-while-indexing"),
)


def format_value(value: float | None, metric: str) -> str:
    if value is None:
        return "–"
    if metric == "instructions":
        return f"{value:,.0f}"
    return format_seconds(value)


@dataclass
class Row:
    name: str
    metric: str  # wall-clock | instructions
    scenario: str
    # ok | faster | pending | regression | accepted | warming-up | new | missing
    status: str
    measured: float | None
    reference: float | None
    limit: float | None
    history_count: int
    delta_percent: float | None = None
    streak: int = 0
    last_good: dict | None = None
    first_bad: dict | None = None
    ratio: float | None = None  # this run / the reference build (#685)


@dataclass
class BudgetRow:
    key: str
    benchmark: str
    scenario: str
    status: str  # ok | broken | missing | not-measured
    measured: float | None
    budget: float
    limit: float
    unit: str
    last_good: dict | None = None
    first_bad: dict | None = None
    enforced: bool = True  # false while the Budgets are proposed (ADR 0018)


# ---------------------------------------------------------------------------
# The history
# ---------------------------------------------------------------------------

def load_entries(directory: Path) -> list[dict]:
    """The recorded runs in directory, oldest first."""
    if not directory.is_dir():
        return []
    entries = []
    for path in sorted(directory.glob("*.json")):
        entry = json.loads(path.read_text(encoding="utf-8"))
        entry["_file"] = path.name
        entries.append(entry)
    entries.sort(key=lambda e: (e.get("recorded_at", ""), e["_file"]))
    return entries


def level(entries: list[dict]) -> list[dict]:
    """The runs since the latest accepted one, which starts the current level."""
    start = 0
    for index, entry in enumerate(entries):
        if entry.get("accepted"):
            start = index
    return entries[start:]


def run_ref(entry: dict) -> dict:
    """What a finding names of a run: enough to link its commit and its run."""
    return {k: entry.get(k, "") for k in ("commit", "recorded_at", "run_id", "ref")}


def wall_clock_group(entry: dict) -> Any:
    """The runs a wall-clock median compares with: those on the same runner
    CPU model (#685), where medians vary by about 1.4 % instead of up to 24 %."""
    return (entry.get("system") or {}).get("cpu")


def usable(entry: dict) -> bool:
    """Whether the run's wall-clock may be compared: its median within-run CV
    is at most MAX_USABLE_CV_PERCENT. A run recorded before #685 is."""
    return entry.get("usable", True) is not False


def median_cv_percent(benchmarks: dict) -> float | None:
    """The median of the benchmarks' within-run CV, the run's own scatter."""
    values = [r["cv_percent"] for r in benchmarks.values()
              if isinstance(r.get("cv_percent"), (int, float))]
    return statistics.median(values) if values else None


def _usable_cv(cv: float | None) -> bool:
    return cv is None or cv <= MAX_USABLE_CV_PERCENT


def wall_clock_runs(current: dict, runs: list[dict]) -> list[dict]:
    """The earlier runs current's wall-clock compares with."""
    group = wall_clock_group(current)
    return [e for e in runs if usable(e) and wall_clock_group(e) == group]


def counts_group(entry: dict) -> Any:
    """Counts compare only on one CPU model (glibc picks its string functions
    by CPU) and with Log Files of one size."""
    counts = entry.get("instruction_counts") or {}
    return counts.get("cpu"), counts.get("log_file_mb")


def wall_clock_value(entry: dict, name: str) -> float | None:
    value = entry.get("benchmarks", {}).get(name, {}).get("median_seconds")
    return value if isinstance(value, (int, float)) else None


def wall_clock_spread(entry: dict, name: str) -> float | None:
    """The interquartile range of the run's own runs (#705)."""
    value = entry.get("benchmarks", {}).get(name, {}).get("iqr_seconds")
    return value if isinstance(value, (int, float)) else None


def count_value(entry: dict, name: str) -> float | None:
    value = ((entry.get("instruction_counts") or {}).get("benchmarks", {})
             .get(name, {}).get("instructions"))
    return value if isinstance(value, (int, float)) else None


def scenario_of(name: str, metric: str, budgets: dict | None) -> str:
    """The scenario an issue about the benchmark is filed under."""
    if metric == "instructions":
        return name.split(" / ", 1)[0]
    for entry in (budgets or {}).get("budgets", {}).values():
        if entry.get("report") == "e2e" and entry.get("benchmark") == name:
            return entry["scenario"]
    for prefix, scenario in SCENARIO_PREFIXES:
        if name.startswith(prefix):
            return scenario
    return "other"


def _min_delta_overrides(budgets: dict | None) -> dict[str, float]:
    """A Budget's min_delta_seconds replaces its benchmark's absolute margin (ADR 0018)."""
    return {e["benchmark"]: e["min_delta_seconds"]
            for e in (budgets or {}).get("budgets", {}).values()
            if e.get("report") == "e2e" and isinstance(e.get("min_delta_seconds"), (int, float))}


# ---------------------------------------------------------------------------
# Change points
# ---------------------------------------------------------------------------

def _series_row(name: str, metric: str, scenario: str, current: dict, runs: list[dict],
                value: Callable[[dict, str], float | None], rule: perf_changepoint.Rule, *,
                tolerance_percent: float | None = None, min_delta: float | None = None,
                spread: Callable[[dict, str], float | None] | None = None,
                accept: bool) -> Row:
    points = [(e, v) for e in runs if (v := value(e, name)) is not None]
    points.append((current, value(current, name)))
    values = [v for _, v in points]
    spreads = [spread(e, name) for e, _ in points] if spread else None
    d = perf_changepoint.detect(values, rule, tolerance_percent=tolerance_percent,
                                min_delta=min_delta, spreads=spreads)
    measured = values[-1]
    delta = None
    if d.reference:
        delta = (measured - d.reference) / d.reference * 100
    row = Row(name, metric, scenario, d.status, measured, d.reference, d.limit,
              d.history_count, delta, d.streak)
    if d.status == "regression":
        row.last_good = run_ref(points[d.change_index - 1][0])
        row.first_bad = run_ref(points[d.change_index][0])
        if accept:
            row.status = "accepted"
    elif d.status == "pending" and accept:
        row.status = "accepted"
    return row


def _missing_row(name: str, metric: str, scenario: str, previous: dict, current: dict,
                 accept: bool) -> Row:
    return Row(name, metric, scenario, "accepted" if accept else "missing", None, None, None, 0,
               last_good=run_ref(previous), first_bad=run_ref(current))


def compare(current: dict, history: list[dict], *, budgets: dict | None = None,
            accept: bool = False) -> list[Row]:
    """One row per benchmark of this run or of the latest recorded run.

    current is this run's entry (make_entry), history the recorded runs of
    the default branch, oldest first.
    """
    runs = level(history)
    overrides = _min_delta_overrides(budgets)
    rows = []

    wall_runs = wall_clock_runs(current, runs)
    ratios = current.get("ratios") or {}
    for name in sorted(current.get("benchmarks", {})):
        scenario = scenario_of(name, "wall-clock", budgets)
        if usable(current):
            row = _series_row(name, "wall-clock", scenario, current, wall_runs,
                              wall_clock_value, WALL_CLOCK, min_delta=overrides.get(name),
                              spread=wall_clock_spread, accept=accept)
        else:
            row = Row(name, "wall-clock", scenario, "unusable", wall_clock_value(current, name),
                      None, None, len(wall_runs))
        row.ratio = ratios.get(name)
        rows.append(row)
    if history:
        previous = history[-1]
        for name in sorted(set(previous.get("benchmarks", {})) - set(current.get("benchmarks", {}))):
            rows.append(_missing_row(name, "wall-clock", scenario_of(name, "wall-clock", budgets),
                                     previous, current, accept))

    counts = current.get("instruction_counts")
    if counts is not None:
        group = counts_group(current)
        count_runs = [e for e in runs if "instruction_counts" in e and counts_group(e) == group]
        for name in sorted(counts.get("benchmarks", {})):
            binary, case = name.split(" / ", 1)
            rows.append(_series_row(name, "instructions", scenario_of(name, "instructions", None),
                                    current, count_runs, count_value, INSTRUCTIONS,
                                    tolerance_percent=instruction_counts.threshold_percent(
                                        binary, case),
                                    accept=accept))
    counted = [e for e in history if "instruction_counts" in e]
    if counted:
        previous = counted[-1]
        if counts is None:
            rows.append(_missing_row(COUNTS_MISSING, "instructions", "instruction-counts",
                                     previous, current, accept))
        else:
            gone = set(previous["instruction_counts"].get("benchmarks", {})) \
                - set(counts.get("benchmarks", {}))
            for name in sorted(gone):
                rows.append(_missing_row(name, "instructions",
                                         scenario_of(name, "instructions", None),
                                         previous, current, accept))
    return rows


# ---------------------------------------------------------------------------
# Budgets
# ---------------------------------------------------------------------------

def _reports(entry: dict) -> dict[str, dict | None]:
    """An entry as the reports perf-budgets.py reads; an unusable run has no
    wall-clock to check."""
    counts = entry.get("instruction_counts")
    instruction_counts = None
    if counts is not None:
        instruction_counts = {"benchmarks": [
            {"binary": key.split(" / ", 1)[0], "name": key.split(" / ", 1)[1], **values}
            for key, values in counts.get("benchmarks", {}).items()]}
    e2e = {"benchmarks": entry.get("benchmarks", {})} if usable(entry) else None
    return {"e2e": e2e, "instruction_counts": instruction_counts}


def check_budgets(budgets: dict, current: dict, history: list[dict]) -> list[BudgetRow]:
    """Each Budget against this run; a broken one with the runs since it broke."""
    statuses: dict[int, dict[str, str]] = {}

    def status_in(index: int, key: str) -> str:
        if index not in statuses:
            statuses[index] = {r.key: r.status for r in
                               perf_budgets.check(budgets, **_reports(history[index]))}
        return statuses[index].get(key, "missing")

    enforce = perf_budgets.enforced(budgets)
    rows = []
    for r in perf_budgets.check(budgets, **_reports(current)):
        entry = budgets["budgets"][r.key]
        row = BudgetRow(r.key, entry["benchmark"], r.scenario, r.status, r.measured, r.budget,
                        r.limit, r.unit, enforced=enforce)
        if r.status == "broken":
            first_bad = current
            for index in range(len(history) - 1, -1, -1):
                if entry["report"] == "e2e" and not usable(history[index]):
                    continue  # it cannot tell; the range spans it
                status = status_in(index, r.key)
                if status == "broken":
                    first_bad = history[index]
                    continue
                if status == "ok":
                    row.last_good = run_ref(history[index])
                break
            row.first_bad = run_ref(first_bad)
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Findings: what files an issue
# ---------------------------------------------------------------------------

def findings(rows: list[Row], budget_rows: list[BudgetRow]) -> list[dict]:
    out = []
    for r in rows:
        files = r.status == "missing" or (
            r.status == "regression"
            and (r.metric == "instructions" or WALL_CLOCK_FILES_ISSUES))
        if files:
            out.append({
                "kind": r.status, "metric": r.metric, "scenario": r.scenario,
                "benchmark": r.name, "measured": r.measured, "reference": r.reference,
                "limit": r.limit, "delta_percent": r.delta_percent, "streak": r.streak,
                "last_good": r.last_good, "first_bad": r.first_bad,
            })
    for b in budget_rows:
        if b.enforced and b.status in ("broken", "missing"):
            out.append({
                "kind": "budget", "metric": "budget", "status": b.status,
                "scenario": b.scenario, "benchmark": b.benchmark, "budget_key": b.key,
                "measured": b.measured, "budget": b.budget, "limit": b.limit, "unit": b.unit,
                "last_good": b.last_good, "first_bad": b.first_bad,
            })
    return out


# ---------------------------------------------------------------------------
# Recording
# ---------------------------------------------------------------------------

def _kept(benchmarks: dict) -> dict:
    return {name: {k: result[k] for k in KEPT_FIELDS if k in result}
            for name, result in sorted(benchmarks.items())}


def make_entry(report: dict, *, commit: str, ref: str, run_id: str, version: str,
               accept: bool, recorded_at: datetime, counts: dict | None = None,
               counts_cpu: str = "", counts_log_file_mb: int | None = None,
               reference: dict | None = None, reference_tag: str = "",
               reference_commit: str = "") -> dict:
    cv = median_cv_percent(report["benchmarks"])
    entry = {
        "schema": SCHEMA,
        "recorded_at": recorded_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": commit,
        "ref": ref,
        "run_id": run_id,
        "version": version,
        "accepted": accept,
        "system": report.get("system", {}),
        "config": report.get("config", {}),
        "median_cv_percent": cv,
        "usable": _usable_cv(cv),
        "benchmarks": _kept(report["benchmarks"]),
    }
    if reference is not None:
        reference_cv = median_cv_percent(reference["benchmarks"])
        entry["reference"] = {
            "tag": reference_tag,
            "commit": reference_commit,
            "median_cv_percent": reference_cv,
            "usable": _usable_cv(reference_cv),
            "benchmarks": _kept(reference["benchmarks"]),
        }
        entry["ratios"] = {
            name: entry["benchmarks"][name]["median_seconds"] / result["median_seconds"]
            for name, result in sorted(reference["benchmarks"].items())
            if name in entry["benchmarks"] and isinstance(result.get("median_seconds"), (int, float))
            and result["median_seconds"] > 0
        }
    if counts is not None:
        entry["instruction_counts"] = {
            "cpu": counts_cpu,
            "log_file_mb": counts_log_file_mb,
            "failed_binaries": list(counts.get("failed_binaries", [])),
            "benchmarks": {
                f"{b['binary']} / {b['name']}": {k: b.get(k) for k in KEPT_COUNTS}
                for b in counts["benchmarks"]
            },
        }
    return entry


def entry_filename(entry: dict) -> str:
    stamp = entry["recorded_at"].replace(":", "").replace("-", "")
    safe_run = re.sub(r"[^0-9A-Za-z-]", "", str(entry["run_id"])) or "local"
    return f"{stamp}-{entry['commit'][:12]}-{safe_run}.json"


def unusable_flag(entry: dict) -> str:
    """What of a run is unusable: "run" (its wall-clock and its ratios),
    "reference" (only its ratios) or nothing."""
    if not usable(entry):
        return "run"
    if (entry.get("reference") or {}).get("usable") is False:
        return "reference"
    return ""


def trend_csv(entries: list[dict]) -> str:
    """One row per run: the runner's CPU model, the run's median CV and what
    of it is unusable, the reference tag; each benchmark's median in seconds,
    its ratio to the reference build, then each count."""
    names = sorted({name for e in entries for name in e.get("benchmarks", {})})
    ratioed = sorted({name for e in entries for name in e.get("ratios") or {}})
    counted = sorted({name for e in entries
                      for name in (e.get("instruction_counts") or {}).get("benchmarks", {})})
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(["recorded_at", "version", "commit", "accepted", "cpu",
                     "median_cv_percent", "unusable", "reference", *names,
                     *[f"ratio: {n}" for n in ratioed],
                     *[f"instructions: {n}" for n in counted]])
    for e in entries:
        values = ([wall_clock_value(e, n) for n in names]
                  + [(e.get("ratios") or {}).get(n) for n in ratioed]
                  + [count_value(e, n) for n in counted])
        cv = e.get("median_cv_percent")
        writer.writerow([
            e.get("recorded_at", ""), e.get("version", ""), e.get("commit", "")[:12],
            "yes" if e.get("accepted") else "", wall_clock_group(e) or "",
            "" if cv is None else cv, unusable_flag(e), (e.get("reference") or {}).get("tag", ""),
            *["" if v is None else v for v in values],
        ])
    return out.getvalue()


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

_LABELS = {
    "ok": "ok",
    "faster": "faster",
    "pending": "above the limit once, watched",
    "regression": "**REGRESSION**",
    "accepted": "accepted as new level",
    "warming-up": "report only",
    "new": "new, no history",
    "missing": "**MISSING**",
    "unusable": "not compared, run unusable",
}


def commit_range(last_good: dict | None, first_bad: dict | None) -> str:
    if first_bad is None:
        return "–"
    bad = first_bad["commit"][:12]
    return f"{last_good['commit'][:12]}..{bad}" if last_good else f"..{bad}"


def _label(r: Row) -> str:
    if r.status == "regression" and r.metric == "wall-clock" and not WALL_CLOCK_FILES_ISSUES:
        return "shifted (trend only)"
    if r.status == "warming-up" and r.limit is not None and r.measured is not None \
            and r.measured > r.limit:
        return "report only, above the limit"
    return _LABELS[r.status]


def _table(rows: list[Row], ratio: str = "") -> list[str]:
    """The rows; with a ratio heading, a column with this run / the reference build."""
    extra = f" {ratio} |" if ratio else ""
    lines = [
        "| Benchmark | This run | Reference | Runs | Delta | Limit | Commits | Status |" + extra,
        "|---|---:|---:|---:|---:|---:|---|---|" + ("---:|" if ratio else ""),
    ]
    for r in rows:
        delta = "–" if r.delta_percent is None else f"{r.delta_percent:+.1f} %"
        name = r.name.replace("|", "\\|")
        cell = (" –" if r.ratio is None else f" {r.ratio:.3f}") + " |" if ratio else ""
        lines.append(f"| {name} | {format_value(r.measured, r.metric)} "
                     f"| {format_value(r.reference, r.metric)} | {r.history_count} "
                     f"| {delta} | {format_value(r.limit, r.metric)} "
                     f"| {commit_range(r.last_good, r.first_bad)} | {_label(r)} |{cell}")
    return lines


def run_info(current: dict, history: list[dict], reference_error: str = "") -> dict:
    """What the summary says about the run itself: the runner's CPU model and
    how many earlier usable runs of it the series has, the run's scatter, and
    the reference build."""
    info = {
        "cpu": wall_clock_group(current),
        "earlier_runs_of_cpu": len(wall_clock_runs(current, level(history))),
        "median_cv_percent": current.get("median_cv_percent"),
        "usable": usable(current),
        "max_usable_cv_percent": MAX_USABLE_CV_PERCENT,
        "reference": None,
        "reference_error": reference_error,
    }
    reference = current.get("reference")
    if reference is not None:
        measured = set(current.get("benchmarks", {}))
        referenced = set(reference.get("benchmarks", {}))
        info["reference"] = {
            "tag": reference.get("tag", ""),
            "commit": reference.get("commit", ""),
            "median_cv_percent": reference.get("median_cv_percent"),
            "usable": reference.get("usable", True),
            "ratios": len(current.get("ratios") or {}),
            "only_this_commit": sorted(measured - referenced),
            "only_reference": sorted(referenced - measured),
        }
    return info


def _cv_text(cv: float | None) -> str:
    return "–" if cv is None else f"{cv:.1f} %"


def _run_lines(info: dict) -> list[str]:
    cpu = info["cpu"] or "unknown"
    lines = [
        f"Runner CPU: `{cpu}`, with {info['earlier_runs_of_cpu']} earlier runs of this CPU model "
        f"in the wall-clock series (a median compares only with runs of its own model; report "
        f"only below {WALL_CLOCK.min_history}). Median within-run CV: "
        f"{_cv_text(info['median_cv_percent'])}.",
        "",
    ]
    if not info["usable"]:
        lines += [
            f"**This run is unusable**: its median within-run CV of "
            f"{_cv_text(info['median_cv_percent'])} is above {info['max_usable_cv_percent']:g} %. "
            "It is recorded, but its wall-clock is not compared, not part of any later series, "
            "and its e2e Budgets are not checked.",
            "",
        ]
    reference = info.get("reference")
    if reference:
        commit = f" ({reference['commit'][:12]})" if reference.get("commit") else ""
        lines.append(
            f"Reference build: `{reference['tag']}`{commit}, built and measured on this runner "
            f"with the same suite: this run ÷ reference for {reference['ratios']} of the "
            f"benchmarks. Its median within-run CV: {_cv_text(reference['median_cv_percent'])}.")
        if not reference["usable"]:
            lines.append(f"**The reference run is unusable** (CV above "
                         f"{info['max_usable_cv_percent']:g} %): its ratios are recorded but "
                         "flagged.")
        if reference["only_this_commit"]:
            lines.append("Measured on only this commit: " + ", ".join(reference["only_this_commit"])
                         + ".")
        if reference["only_reference"]:
            lines.append("Measured on only the reference: " + ", ".join(reference["only_reference"])
                         + ".")
        lines.append("")
    elif info.get("reference_error"):
        lines += [f"No ratios to the reference build: {info['reference_error']}.", ""]
    return lines


def markdown(rows: list[Row], budget_rows: list[BudgetRow],
             title: str = "Nightly performance", info: dict | None = None) -> str:
    wall = [r for r in rows if r.metric == "wall-clock"]
    counts = [r for r in rows if r.metric == "instructions"]
    lines = [f"## {title}", ""]
    if info is not None:
        lines += _run_lines(info)
    lines += [
        "### Instruction counts",
        "",
        "A regression is a change point in a benchmark's series of counts "
        "(`.github/scripts/perf_changepoint.py`): a run from which on every run is more than the "
        f"benchmark's gate threshold (+{INSTRUCTIONS.tolerance_percent:g} % by default) and "
        f"3 IQR above the median of the up to {INSTRUCTIONS.window} runs before it, on the same "
        f"CPU model; report only while fewer than {INSTRUCTIONS.min_history} runs come before. "
        "A regression files an issue.",
        "",
    ]
    lines += _table(counts) if counts else ["Not counted in this run."]
    lines += [
        "",
        "### Wall-clock (trend)",
        "",
        f"A change point once {WALL_CLOCK.persistence} runs in a row are more than "
        f"+{WALL_CLOCK.tolerance_percent:g} %, the absolute margin (half the reference, at "
        f"most {WALL_CLOCK.min_delta_cap * 1000:g} ms, or a Budget's `min_delta_seconds`) and "
        f"3 IQR above the median of the up to {WALL_CLOCK.window} runs before them on the same "
        f"runner CPU model; report only while fewer than {WALL_CLOCK.min_history} runs come "
        "before. Shown here as the trend; instruction counts are the gate.",
        "",
    ]
    reference = (info or {}).get("reference")
    ratio = ""
    if reference or any(r.ratio is not None for r in wall):
        ratio = f"÷ `{reference['tag']}`" if reference and reference.get("tag") else "÷ reference"
    lines += _table(wall, ratio=ratio)
    lines.append("")
    if budget_rows:
        enforce = all(b.enforced for b in budget_rows)
        lines.append(perf_budgets.markdown(budget_rows, title="Performance Budgets",
                                           enforce=enforce).replace("## ", "### ", 1))
    return "\n".join(lines)


def annotations(rows: list[Row], budget_rows: list[BudgetRow]) -> list[str]:
    """GitHub workflow commands for the findings."""
    out = []
    for f in findings(rows, budget_rows):
        span = commit_range(f["last_good"], f["first_bad"])
        if f["kind"] == "regression":
            out.append(f"::error::{f['benchmark']} regressed: "
                       f"{format_value(f['measured'], f['metric'])} against "
                       f"{format_value(f['reference'], f['metric'])} ({f['delta_percent']:+.1f} %), "
                       f"commits {span}")
        elif f["kind"] == "missing":
            out.append(f"::error::{f['benchmark']} was measured by the previous run but not by "
                       f"this one, commits {span}")
        elif f["status"] == "broken":
            out.append(f"::error::{f['budget_key']} broke its Budget: "
                       f"{perf_budgets.format_value(f['measured'], f['unit'])} against "
                       f"{perf_budgets.format_value(f['budget'], f['unit'])}, commits {span}")
        else:
            out.append(f"::error::{f['budget_key']} has a Budget but was not measured")
    return out


# ---------------------------------------------------------------------------
# The command
# ---------------------------------------------------------------------------

def _read_report(path: str) -> dict:
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    benchmarks = report["benchmarks"]
    if not isinstance(benchmarks, dict) or not benchmarks:
        raise ValueError("the report holds no benchmarks")
    for name, result in benchmarks.items():
        if not isinstance(result.get("median_seconds"), (int, float)):
            raise ValueError(f"{name} has no median_seconds")
    return report


def _read_counts(path: str) -> dict:
    counts = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(counts.get("benchmarks"), list):
        raise ValueError("the counts hold no benchmarks")
    for b in counts["benchmarks"]:
        if not (isinstance(b.get("binary"), str) and isinstance(b.get("name"), str)
                and isinstance(b.get("instructions"), int)):
            raise ValueError(f"a benchmark without binary, name or instructions: {b}")
    if not isinstance(counts.get("failed_binaries", []), list):
        raise ValueError("failed_binaries is not a list")
    return counts


def record(args: argparse.Namespace) -> int:
    try:
        report = _read_report(args.report)
        counts = _read_counts(args.instruction_counts) if args.instruction_counts else None
        budgets = perf_budgets.load(args.budgets) if args.budgets else None
    except (OSError, ValueError, KeyError, AttributeError, TypeError) as error:
        print(f"::error::Unusable input: {error}")
        return 2
    # The reference build is an addition: without it the run is still
    # measured, only without ratios.
    reference, reference_error = None, ""
    if args.reference_report:
        try:
            reference = _read_report(args.reference_report)
        except (OSError, ValueError, KeyError, AttributeError, TypeError) as error:
            reference_error = f"the reference report is unusable ({error})"
            print(f"::warning::{reference_error}")

    recorded_at = (datetime.strptime(args.recorded_at, "%Y-%m-%dT%H:%M:%SZ")
                   .replace(tzinfo=timezone.utc) if args.recorded_at
                   else datetime.now(timezone.utc))
    entry = make_entry(report, commit=args.commit, ref=args.ref, run_id=args.run_id,
                       version=args.version, accept=args.accept, recorded_at=recorded_at,
                       counts=counts, counts_cpu=args.counts_cpu,
                       counts_log_file_mb=args.counts_log_file_mb, reference=reference,
                       reference_tag=args.reference_tag, reference_commit=args.reference_commit)

    data_dir = Path(args.data_dir)
    history = load_entries(data_dir / "history")
    rows = compare(entry, history, budgets=budgets, accept=args.accept)
    budget_rows = check_budgets(budgets, entry, history) if budgets else []
    found = findings(rows, budget_rows)
    info = run_info(entry, history, reference_error)

    dest = data_dir / args.dest
    dest.mkdir(parents=True, exist_ok=True)
    (dest / entry_filename(entry)).write_text(json.dumps(entry, indent=2) + "\n", encoding="utf-8")
    if args.dest == "history":
        (data_dir / "trend.csv").write_text(trend_csv(load_entries(data_dir / "history")),
                                            encoding="utf-8")

    text = markdown(rows, budget_rows, info=info)
    if args.markdown:
        Path(args.markdown).write_text(text, encoding="utf-8")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "schema": COMPARISON_SCHEMA,
            "failed": bool(found),
            "dest": args.dest,
            "run": run_ref(entry),
            "run_info": info,
            "rules": {"wall_clock": asdict(WALL_CLOCK), "instructions": asdict(INSTRUCTIONS),
                      "wall_clock_files_issues": WALL_CLOCK_FILES_ISSUES,
                      "max_usable_cv_percent": MAX_USABLE_CV_PERCENT},
            "rows": [asdict(r) for r in rows],
            "budgets": [asdict(b) for b in budget_rows],
            "findings": found,
        }, indent=2) + "\n", encoding="utf-8")
    print(text)
    for line in annotations(rows, budget_rows):
        print(line)
    return 1 if found else 0


# ---------------------------------------------------------------------------
# The spread within one CPU model (#685, for #675)
# ---------------------------------------------------------------------------

def cv_percent(values: list[float]) -> float | None:
    """The coefficient of variation (sample standard deviation / mean), in
    percent, of at least two values."""
    if len(values) < 2:
        return None
    mean = statistics.fmean(values)
    return statistics.stdev(values) / mean * 100 if mean else None


def _spread_of(series: dict[str, list[float]]) -> dict:
    """Each benchmark's CV over the runs, and the median and maximum of them."""
    benchmarks = {}
    for name, values in sorted(series.items()):
        benchmarks[name] = {"runs": len(values), "cv_percent": cv_percent(values),
                            "min": min(values), "max": max(values)}
    cvs = [b["cv_percent"] for b in benchmarks.values() if b["cv_percent"] is not None]
    return {"median_cv_percent": statistics.median(cvs) if cvs else None,
            "max_cv_percent": max(cvs) if cvs else None,
            "benchmarks": benchmarks}


def _series_of(entries: list[dict], values: Callable[[dict], dict[str, float]]) -> dict:
    series: dict[str, list[float]] = {}
    for e in entries:
        for name, value in values(e).items():
            if isinstance(value, (int, float)):
                series.setdefault(name, []).append(value)
    return series


def _medians(e: dict) -> dict[str, float]:
    return {n: r.get("median_seconds") for n, r in e.get("benchmarks", {}).items()}


def _reference_medians(e: dict) -> dict[str, float]:
    return {n: r.get("median_seconds")
            for n, r in (e.get("reference") or {}).get("benchmarks", {}).items()}


def _with_usable_reference(entries: list[dict]) -> list[dict]:
    return [e for e in entries if e.get("reference") and e["reference"].get("usable", True)]


def _model_spread(entries: list[dict]) -> dict:
    referenced = _with_usable_reference(entries)
    by_tag: dict[str, list[dict]] = {}
    for e in referenced:
        by_tag.setdefault(e["reference"].get("tag", ""), []).append(e)
    return {
        "runs": len(entries),
        "first": entries[0].get("recorded_at", "") if entries else "",
        "last": entries[-1].get("recorded_at", "") if entries else "",
        "medians": _spread_of(_series_of(entries, _medians)),
        "reference": {tag: {"runs": len(runs), **_spread_of(_series_of(runs, _reference_medians))}
                      for tag, runs in sorted(by_tag.items())},
        "ratios": _spread_of(_series_of(referenced, lambda e: e.get("ratios") or {})),
    }


def spread(entries: list[dict]) -> dict:
    """How much the medians of the usable runs vary, per runner CPU model and
    over all models together.

    medians mixes the code's own changes into it; the reference build's
    medians (per tag: fixed code) are the runner's own spread, which #675
    re-decides a dedicated runner on; ratios is how steady the trend line
    across models is.
    """
    runs = [e for e in entries if usable(e)]
    models: dict[Any, list[dict]] = {}
    for e in runs:
        models.setdefault(wall_clock_group(e), []).append(e)
    return {
        "max_usable_cv_percent": MAX_USABLE_CV_PERCENT,
        "unusable_runs": len(entries) - len(runs),
        "models": [{"cpu": cpu, **_model_spread(model_runs)}
                   for cpu, model_runs in sorted(models.items(), key=lambda kv: -len(kv[1]))],
        "all_models": _model_spread(runs),
    }


def spread_markdown(result: dict) -> str:
    def cv(value: float | None) -> str:
        return "–" if value is None else f"{value:.1f} %"

    lines = [
        "## Spread of the nightly wall-clock medians within one CPU model",
        "",
        "For the re-decision on a dedicated benchmark runner (#675): a machine pays off if the "
        "same-model medians vary by more than 3 %. *Reference build* is the CV of each "
        "benchmark's medians of the fixed reference build (one release tag: no code change, the "
        "runner's own spread); *this commit* includes master's own changes; *ratio* is the CV of "
        "this commit ÷ reference. Each is the median (and maximum) over the benchmarks. Runs "
        f"with a median within-run CV above {result['max_usable_cv_percent']:g} % are left out "
        f"({result['unusable_runs']} of them).",
        "",
        "| CPU model | Runs | From | To | Reference build | This commit | Ratio |",
        "|---|---:|---|---|---|---|---|",
    ]

    def row(name: str, m: dict) -> str:
        reference = "; ".join(
            f"`{tag}` ({r['runs']} runs): {cv(r['median_cv_percent'])} (max {cv(r['max_cv_percent'])})"
            for tag, r in m["reference"].items()) or "–"
        return (f"| {name} | {m['runs']} | {m['first'][:10]} | {m['last'][:10]} | {reference} "
                f"| {cv(m['medians']['median_cv_percent'])} "
                f"(max {cv(m['medians']['max_cv_percent'])}) "
                f"| {cv(m['ratios']['median_cv_percent'])} "
                f"(max {cv(m['ratios']['max_cv_percent'])}) |")

    for m in result["models"]:
        lines.append(row(f"`{m['cpu'] or 'unknown'}`", m))
    lines.append(row("all models together", result["all_models"]))
    lines.append("")
    return "\n".join(lines)


def spread_command(args: argparse.Namespace) -> int:
    entries = load_entries(Path(args.data_dir) / "history")
    if args.since:
        entries = [e for e in entries if e.get("recorded_at", "") >= args.since]
    result = spread(entries)
    text = spread_markdown(result)
    if args.markdown:
        Path(args.markdown).write_text(text, encoding="utf-8")
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(text)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("record", help="find the change points of a run and record it")
    rec.add_argument("--report", required=True, help="the e2e suite's benchmark_report.json")
    rec.add_argument("--data-dir", required=True, help="a checkout of the perf-data branch")
    rec.add_argument("--dest", required=True, choices=("history", "trial"))
    rec.add_argument("--commit", required=True)
    rec.add_argument("--ref", required=True)
    rec.add_argument("--run-id", required=True)
    rec.add_argument("--version", default="")
    rec.add_argument("--instruction-counts", help="instruction-counts.py's side file (after.json)")
    rec.add_argument("--counts-cpu", default="", help="the CPU model the counts were taken on")
    rec.add_argument("--counts-log-file-mb", type=int,
                     help="LOGSQUIRL_BENCHMARK_LOG_FILE_MB of the counts")
    rec.add_argument("--reference-report",
                     help="the same suite's benchmark_report.json of the reference build (#685)")
    rec.add_argument("--reference-tag", default="", help="the release tag the reference was built from")
    rec.add_argument("--reference-commit", default="", help="the commit of that tag")
    rec.add_argument("--budgets", help="tests/e2e/budgets.json (ADR 0018)")
    rec.add_argument("--recorded-at", help="the time to record, %%Y-%%m-%%dT%%H:%%M:%%SZ (now)")
    rec.add_argument("--accept", action="store_true",
                     help="record this run as the start of a new level; it finds no regression")
    rec.add_argument("--markdown", help="write the summary here")
    rec.add_argument("--json", help="write the comparison and its findings as JSON here")
    spr = sub.add_parser("spread", help="the spread of the medians within each CPU model (#675)")
    spr.add_argument("--data-dir", required=True, help="a checkout of the perf-data branch")
    spr.add_argument("--since", help="only runs recorded on or after this date, YYYY-MM-DD")
    spr.add_argument("--markdown", help="write the table here")
    spr.add_argument("--json", help="write the numbers as JSON here")
    args = parser.parse_args(argv)
    if args.command == "spread":
        return spread_command(args)
    return record(args)


if __name__ == "__main__":
    sys.exit(main())
