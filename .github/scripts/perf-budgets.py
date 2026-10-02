#!/usr/bin/env python3
"""Checks a performance run against the Budgets of ADR 0018 (#676).

The Budgets live in one file, tests/e2e/budgets.json, which the ADR points
to. Each entry names a benchmark of a report and the field of it that is
measured, the Budget in the entry's unit, and a noise margin in percent: a
value is over its Budget only when it is above budget * (1 + margin / 100).

  report "e2e"                 the e2e suite's benchmark_report.json; the
                               benchmark is its key, the field usually
                               median_seconds
  report "instruction-counts"  the instruction counts' after.json; the
                               benchmark is "<binary> / <name>", the field
                               e.g. peak_heap_bytes; "scale" turns it into
                               the unit (bytes of 2 million Log Lines into
                               bytes per million: 0.5)

A Budget whose report was not given is "not measured" and fails nothing; a
budgeted benchmark the report does not hold is "missing" and fails.

Usage:
  perf-budgets.py check --budgets tests/e2e/budgets.json \\
      [--e2e benchmark_report.json] [--instruction-counts after.json] \\
      [--markdown OUT.md] [--json OUT.json]

Exit status: 0 when every measured Budget holds, 1 when one is broken or
missing, 2 on an unusable budgets file or report.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

SCHEMA = 1
REPORTS = ("e2e", "instruction-counts")
REQUIRED = ("scenario", "file", "metric", "report", "benchmark", "field", "unit", "budget",
            "noise_margin_percent", "source")


class BudgetError(ValueError):
    """The budgets file cannot be used."""


@dataclass
class Row:
    key: str
    scenario: str
    status: str  # ok | broken | missing | not-measured
    measured: float | None
    budget: float
    limit: float
    unit: str


def load(path: Path | str) -> dict:
    """The budgets file, refused when a Budget could not be checked or traced."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BudgetError(f"{path}: {error}") from error
    if data.get("schema") != SCHEMA:
        raise BudgetError(f"{path}: schema {data.get('schema')!r}, expected {SCHEMA}")
    sources = data.get("sources", {})
    entries = data.get("budgets")
    if not isinstance(entries, dict) or not entries:
        raise BudgetError(f"{path}: no budgets")
    for key, entry in entries.items():
        missing = [k for k in REQUIRED if k not in entry]
        if missing:
            raise BudgetError(f"{key}: no {', '.join(missing)}")
        if entry["report"] not in REPORTS:
            raise BudgetError(f"{key}: report {entry['report']!r} is none of {REPORTS}")
        if not isinstance(entry["budget"], (int, float)) or entry["budget"] <= 0:
            raise BudgetError(f"{key}: the budget must be a positive number")
        if not isinstance(entry["noise_margin_percent"], (int, float)) \
                or entry["noise_margin_percent"] < 0:
            raise BudgetError(f"{key}: the noise margin must be a number of 0 or more")
        unknown = [s for s in entry["source"] if s not in sources]
        if not entry["source"] or unknown:
            raise BudgetError(f"{key}: cites no measurement of the sources: {unknown or '[]'}")
    return data


def _e2e_results(report: dict | None) -> dict[str, dict] | None:
    return None if report is None else report["benchmarks"]


def _instruction_count_results(report: dict | None) -> dict[str, dict] | None:
    if report is None:
        return None
    return {f"{b['binary']} / {b['name']}": b for b in report["benchmarks"]}


def check(budgets: dict, *, e2e: dict | None = None,
          instruction_counts: dict | None = None) -> list[Row]:
    """One row per Budget, in the order of the file."""
    results = {"e2e": _e2e_results(e2e),
               "instruction-counts": _instruction_count_results(instruction_counts)}
    rows = []
    for key, entry in budgets["budgets"].items():
        limit = entry["budget"] * (1 + entry["noise_margin_percent"] / 100)
        report = results[entry["report"]]
        measured = None
        if report is None:
            status = "not-measured"
        elif not isinstance(report.get(entry["benchmark"], {}).get(entry["field"]),
                            (int, float)):
            status = "missing"
        else:
            measured = report[entry["benchmark"]][entry["field"]] * entry.get("scale", 1)
            status = "broken" if measured > limit else "ok"
        rows.append(Row(key, entry["scenario"], status, measured, entry["budget"], limit,
                        entry["unit"]))
    return rows


def failed(rows: list[Row]) -> bool:
    return any(r.status in ("broken", "missing") for r in rows)


_LABELS = {"ok": "ok", "broken": "**BROKEN**", "missing": "**MISSING**",
           "not-measured": "not measured in this run"}


def format_value(value: float | None, unit: str) -> str:
    if value is None:
        return "–"
    if unit == "s" and abs(value) < 0.001:
        return f"{value * 1e6:.1f} µs"
    if unit.startswith("bytes"):
        return f"{value / 1e6:.2f} MB{unit[len('bytes'):]}"
    return f"{value:g} {unit}"


def markdown(rows: list[Row], title: str = "Performance Budgets") -> str:
    lines = [
        f"## {title}",
        "",
        "Each Budget of ADR 0018 (`tests/e2e/budgets.json`); broken above the Budget plus "
        "its noise margin.",
        "",
        "| Scenario | Benchmark | Measured | Budget | Status |",
        "|---|---|---:|---:|---|",
    ]
    for r in rows:
        lines.append(f"| {r.scenario} | {r.key} | {format_value(r.measured, r.unit)} "
                     f"| {format_value(r.budget, r.unit)} | {_LABELS[r.status]} |")
    lines.append("")
    return "\n".join(lines)


def annotations(rows: list[Row]) -> list[str]:
    out = []
    for r in rows:
        if r.status == "broken":
            out.append(f"::error::{r.key} broke its Budget: {format_value(r.measured, r.unit)} "
                       f"against {format_value(r.budget, r.unit)} "
                       f"(limit {format_value(r.limit, r.unit)})")
        elif r.status == "missing":
            out.append(f"::error::{r.key} has a Budget but was not in the report")
    return out


def _read_report(path: str | None) -> dict | None:
    if path is None:
        return None
    try:
        report = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BudgetError(f"{path}: {error}") from error
    if not isinstance(report.get("benchmarks"), (dict, list)):
        raise BudgetError(f"{path}: holds no benchmarks")
    return report


def run_check(args: argparse.Namespace) -> int:
    try:
        data = load(args.budgets)
        rows = check(data, e2e=_read_report(args.e2e),
                     instruction_counts=_read_report(args.instruction_counts))
    except BudgetError as error:
        print(f"::error::{error}")
        return 2
    text = markdown(rows)
    if args.markdown:
        Path(args.markdown).write_text(text, encoding="utf-8")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "failed": failed(rows), "rows": [asdict(r) for r in rows],
        }, indent=2) + "\n", encoding="utf-8")
    print(text)
    for line in annotations(rows):
        print(line)
    return 1 if failed(rows) else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    chk = sub.add_parser("check", help="check a run against the Budgets")
    chk.add_argument("--budgets", required=True, help="tests/e2e/budgets.json")
    chk.add_argument("--e2e", help="the e2e suite's benchmark_report.json")
    chk.add_argument("--instruction-counts", help="the instruction counts' after.json")
    chk.add_argument("--markdown", help="write the table here")
    chk.add_argument("--json", help="write the verdict as JSON here")
    return run_check(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
