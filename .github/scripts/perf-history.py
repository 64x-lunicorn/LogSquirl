#!/usr/bin/env python3
"""Compares a weekly performance run with the runs before it and records it (#441).

The Performance workflow (.github/workflows/performance.yml) measures the e2e
performance suite once a week on a GitHub-hosted runner. Shared runners differ
from week to week, so a fixed baseline recorded on one machine says little.
Each benchmark is compared with the median of the same benchmark over the last
WINDOW recorded runs instead: the reference moves with the runners, and one
slow or fast week does not move the median.

A benchmark is a regression when its median this run is more than
THRESHOLD_PERCENT slower than that reference and also more than MIN_DELTA_SECONDS
slower (so a benchmark of a few milliseconds does not turn red over a
scheduling hiccup). Until WINDOW earlier runs of a benchmark exist, the run
reports it but does not fail on it. A benchmark the previous run measured and
this run did not is a failure from the first run on: a benchmark that silently
stops running would otherwise pass forever.

The history lives on the perf-data branch, one JSON file per run:

  history/<recorded_at>-<commit>-<run id>.json   runs of the default branch
  trial/<recorded_at>-<commit>-<run id>.json     runs dispatched from any other
                                                 branch; never compared with
  trend.csv                                      one row per history/ run

A run recorded with --accept starts a new level: comparisons from then on use
only that run and the ones after it (an intentional slowdown is accepted this
way instead of staying red until the median catches up). The accepting run
itself does not fail.

Usage:
  perf-history.py record --report benchmark_report.json --data-dir DIR \\
      --dest history|trial --commit SHA --ref REF --run-id ID [--version V] \\
      [--accept] [--markdown OUT.md] [--json OUT.json]

Exit status: 0 when nothing regressed, 1 when a benchmark regressed or went
missing (the run is recorded all the same), 2 on unusable input (nothing is
recorded).
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import median

WINDOW = 6
THRESHOLD_PERCENT = 30.0
MIN_DELTA_SECONDS = 0.010
SCHEMA = 1

# What a recorded benchmark keeps of the suite's report: the statistics, not the
# raw runs, which the Actions artifact of the run still holds.
KEPT_FIELDS = (
    "median_seconds", "mean_seconds", "std_seconds", "cv_percent",
    "p5_seconds", "p95_seconds", "min_seconds", "max_seconds",
    "filtered_count", "total_count", "throughput",
)


@dataclass
class Row:
    name: str
    status: str  # ok | faster | regression | accepted | warming-up | new | missing
    measured: float | None
    reference: float | None
    history_count: int
    delta_percent: float | None
    limit: float | None


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


def comparison_window(entries: list[dict], window: int = WINDOW) -> list[dict]:
    """The last `window` runs, never reaching back past the latest accepted one."""
    start = 0
    for index, entry in enumerate(entries):
        if entry.get("accepted"):
            start = index
    return entries[start:][-window:]


def compare(current: dict[str, dict], history: list[dict], *, window: int = WINDOW,
            threshold_percent: float = THRESHOLD_PERCENT,
            min_delta_seconds: float = MIN_DELTA_SECONDS,
            accept: bool = False) -> list[Row]:
    """One row per benchmark of this run or of the latest recorded run."""
    runs = comparison_window(history, window)
    rows = []
    for name in sorted(current):
        measured = current[name]["median_seconds"]
        values = [e["benchmarks"][name]["median_seconds"] for e in runs
                  if name in e.get("benchmarks", {})]
        if not values:
            rows.append(Row(name, "new", measured, None, 0, None, None))
            continue
        reference = median(values)
        delta = (measured - reference) / reference * 100 if reference > 0 else 0.0
        limit = max(reference * (1 + threshold_percent / 100), reference + min_delta_seconds)
        if len(values) < window:
            status = "warming-up"
        elif measured > limit:
            status = "accepted" if accept else "regression"
        elif delta < -threshold_percent:
            status = "faster"
        else:
            status = "ok"
        rows.append(Row(name, status, measured, reference, len(values), delta, limit))

    # A benchmark that stops being measured must not pass silently: the latest
    # recorded run is what this run should at least cover.
    if history:
        for name in sorted(set(history[-1].get("benchmarks", {})) - set(current)):
            rows.append(Row(name, "accepted" if accept else "missing", None, None, 0, None, None))
    return rows


def failed(rows: list[Row]) -> bool:
    return any(r.status in ("regression", "missing") for r in rows)


def make_entry(report: dict, *, commit: str, ref: str, run_id: str, version: str,
               accept: bool, recorded_at: datetime) -> dict:
    return {
        "schema": SCHEMA,
        "recorded_at": recorded_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": commit,
        "ref": ref,
        "run_id": run_id,
        "version": version,
        "accepted": accept,
        "system": report.get("system", {}),
        "config": report.get("config", {}),
        "benchmarks": {
            name: {k: result[k] for k in KEPT_FIELDS if k in result}
            for name, result in sorted(report["benchmarks"].items())
        },
    }


def entry_filename(entry: dict) -> str:
    stamp = entry["recorded_at"].replace(":", "").replace("-", "")
    safe_run = re.sub(r"[^0-9A-Za-z]", "", str(entry["run_id"])) or "local"
    return f"{stamp}-{entry['commit'][:12]}-{safe_run}.json"


def trend_csv(entries: list[dict]) -> str:
    """One row per run, one column per benchmark median in seconds."""
    names = sorted({name for e in entries for name in e.get("benchmarks", {})})
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(["recorded_at", "version", "commit", "accepted", *names])
    for e in entries:
        writer.writerow([
            e.get("recorded_at", ""), e.get("version", ""), e.get("commit", "")[:12],
            "yes" if e.get("accepted") else "",
            *[e["benchmarks"][n]["median_seconds"] if n in e.get("benchmarks", {}) else ""
              for n in names],
        ])
    return out.getvalue()


_LABELS = {
    "ok": "ok",
    "faster": "faster",
    "regression": "**REGRESSION**",
    "accepted": "accepted as new level",
    "warming-up": "report only",
    "new": "new, no history",
    "missing": "**MISSING**",
}


def markdown(rows: list[Row], *, window: int = WINDOW,
             threshold_percent: float = THRESHOLD_PERCENT,
             min_delta_seconds: float = MIN_DELTA_SECONDS,
             title: str = "Weekly performance") -> str:
    def seconds(value: float | None) -> str:
        return "–" if value is None else f"{value:.4f} s"

    lines = [
        f"## {title}",
        "",
        f"Each benchmark's median against the median of its last {window} recorded runs. "
        f"Red above +{threshold_percent:.0f} % and +{min_delta_seconds * 1000:.0f} ms; "
        f"report only while fewer than {window} runs are recorded.",
        "",
        "| Benchmark | This run | Reference | Runs | Delta | Limit | Status |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for r in rows:
        delta = "–" if r.delta_percent is None else f"{r.delta_percent:+.1f} %"
        runs = f"{r.history_count}/{window}"
        label = _LABELS[r.status]
        if r.status == "warming-up" and r.limit is not None and r.measured > r.limit:
            label = "report only, above the limit"
        lines.append(f"| {r.name} | {seconds(r.measured)} | {seconds(r.reference)} | {runs} "
                     f"| {delta} | {seconds(r.limit)} | {label} |")
    lines.append("")
    return "\n".join(lines)


def annotations(rows: list[Row], *, window: int = WINDOW) -> list[str]:
    """GitHub workflow commands for what needs attention."""
    out = []
    for r in rows:
        if r.status == "regression":
            out.append(f"::error::{r.name} regressed: {r.measured:.4f} s against a median of "
                       f"{r.reference:.4f} s over the last {window} runs ({r.delta_percent:+.1f} %)")
        elif r.status == "missing":
            out.append(f"::error::{r.name} was measured by the previous run but not by this one")
    warming = [r.name for r in rows if r.status in ("warming-up", "new")]
    if warming:
        out.append(f"::notice::Report only, fewer than {window} recorded runs: {', '.join(warming)}")
    return out


def record(args: argparse.Namespace) -> int:
    try:
        report = json.loads(Path(args.report).read_text(encoding="utf-8"))
        benchmarks = report["benchmarks"]
        if not isinstance(benchmarks, dict) or not benchmarks:
            raise ValueError("the report holds no benchmarks")
        for name, result in benchmarks.items():
            if not isinstance(result.get("median_seconds"), (int, float)):
                raise ValueError(f"{name} has no median_seconds")
    except (OSError, ValueError, KeyError, AttributeError) as error:
        print(f"::error::Unusable performance report {args.report}: {error}")
        return 2

    data_dir = Path(args.data_dir)
    history = load_entries(data_dir / "history")
    rows = compare(benchmarks, history, accept=args.accept)

    entry = make_entry(report, commit=args.commit, ref=args.ref, run_id=args.run_id,
                       version=args.version, accept=args.accept,
                       recorded_at=datetime.now(timezone.utc))
    dest = data_dir / args.dest
    dest.mkdir(parents=True, exist_ok=True)
    (dest / entry_filename(entry)).write_text(json.dumps(entry, indent=2) + "\n", encoding="utf-8")
    if args.dest == "history":
        (data_dir / "trend.csv").write_text(trend_csv(load_entries(data_dir / "history")),
                                            encoding="utf-8")

    text = markdown(rows)
    if args.markdown:
        Path(args.markdown).write_text(text, encoding="utf-8")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "failed": failed(rows),
            "window": WINDOW,
            "threshold_percent": THRESHOLD_PERCENT,
            "min_delta_seconds": MIN_DELTA_SECONDS,
            "rows": [asdict(r) for r in rows],
        }, indent=2) + "\n", encoding="utf-8")
    print(text)
    for line in annotations(rows):
        print(line)
    return 1 if failed(rows) else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("record", help="compare a run with the history and record it")
    rec.add_argument("--report", required=True, help="the e2e suite's benchmark_report.json")
    rec.add_argument("--data-dir", required=True, help="a checkout of the perf-data branch")
    rec.add_argument("--dest", required=True, choices=("history", "trial"))
    rec.add_argument("--commit", required=True)
    rec.add_argument("--ref", required=True)
    rec.add_argument("--run-id", required=True)
    rec.add_argument("--version", default="")
    rec.add_argument("--accept", action="store_true",
                     help="record this run as the start of a new level; it does not fail")
    rec.add_argument("--markdown", help="write the comparison table here")
    rec.add_argument("--json", help="write the comparison as JSON here")
    args = parser.parse_args(argv)
    return record(args)


if __name__ == "__main__":
    sys.exit(main())
