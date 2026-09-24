#!/usr/bin/env python3
"""Compares the benchmark results of two builds, before and after (#276).

The Benchmarks workflow (.github/workflows/benchmarks.yml) runs every Catch2
benchmark in tests/benchmarks and the e2e performance suite on both builds and
lays the results out as:

    <side>/catch2/<benchmark binary>.xml      Catch2 `--reporter xml`
    <side>/e2e/benchmark_report.json          tests/e2e `--bench-report json`

Either part may be missing, e.g. when a benchmark binary does not exist on one
side or crashed. This script reads both sides and writes a Markdown table per
suite (for the job summary) and the same comparison as JSON (for the artifact).

A change counts as clear when the two measurements are unlikely to be noise:
for Catch2, when the 95% confidence intervals of the two means do not overlap;
for the e2e suite, which keeps its measured runs, when Welch's t statistic of
the two sets of runs exceeds 2 (about p < 0.05 at the suite's 21 runs). A
shared CI runner is noisy, so treat a small clear change with care.

Usage:
  benchmark-compare.py --before DIR --after DIR [--before-label TEXT] [--after-label TEXT]
                       [--markdown FILE] [--json FILE]
Without --markdown, the Markdown goes to stdout.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from statistics import mean, stdev

CATCH2 = "Catch2"
E2E = "e2e performance"
SUITES = (CATCH2, E2E)

# |t| above this is a clear change; the two-sided 95% quantile of Student's t
# is 2.09 at 20 degrees of freedom and approaches 1.96, so 2 is close enough
# for a verdict that is only a hint.
T_THRESHOLD = 2.0


@dataclass
class Result:
    """One benchmark's measurement on one side, in nanoseconds."""
    suite: str
    key: str
    value_ns: float
    low_ns: float | None = None
    high_ns: float | None = None
    runs_ns: list[float] = field(default_factory=list)


@dataclass
class Comparison:
    suite: str
    key: str
    before: Result | None
    after: Result | None
    change_percent: float | None
    verdict: str


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def read_catch2(path: Path) -> list[Result]:
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as error:
        warn(f"{path}: not a readable Catch2 XML report ({error}); skipped")
        return []

    binary = root.get("name") or path.stem
    results: list[Result] = []
    seen: dict[str, int] = {}

    def walk(element: ET.Element, names: list[str]) -> None:
        for child in element:
            if child.tag in ("TestCase", "Section"):
                walk(child, names + [child.get("name", "")])
            elif child.tag == "Group":
                walk(child, names)
            elif child.tag == "BenchmarkResults":
                mean_element = child.find("mean")
                if mean_element is None or mean_element.get("value") is None:
                    continue
                key = " / ".join([binary] + names + [child.get("name", "")])
                # A benchmark name repeated in the same place (a loop without
                # sections) still gets a row of its own per occurrence.
                seen[key] = seen.get(key, 0) + 1
                if seen[key] > 1:
                    key = f"{key} #{seen[key]}"
                results.append(Result(
                    CATCH2, key,
                    float(mean_element.get("value")),
                    float(mean_element.get("lowerBound", mean_element.get("value"))),
                    float(mean_element.get("upperBound", mean_element.get("value"))),
                ))

    walk(root, [])
    return results


def read_e2e(path: Path) -> list[Result]:
    try:
        benchmarks = json.loads(path.read_text(encoding="utf-8"))["benchmarks"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        warn(f"{path}: not a readable e2e benchmark report ({error}); skipped")
        return []

    results = []
    for name, data in sorted(benchmarks.items()):
        try:
            results.append(Result(
                E2E, name,
                float(data["median_seconds"]) * 1e9,
                float(data["p5_seconds"]) * 1e9 if "p5_seconds" in data else None,
                float(data["p95_seconds"]) * 1e9 if "p95_seconds" in data else None,
                [float(run) * 1e9 for run in data.get("runs", [])],
            ))
        except (KeyError, TypeError, ValueError) as error:
            warn(f"{path}: benchmark {name} has no median ({error}); skipped")
    return results


def read_side(directory: Path) -> list[Result]:
    results: list[Result] = []
    for path in sorted((directory / "catch2").glob("*.xml")):
        results.extend(read_catch2(path))
    e2e_report = directory / "e2e" / "benchmark_report.json"
    if e2e_report.is_file():
        results.extend(read_e2e(e2e_report))
    return results


def welch_t(a: list[float], b: list[float]) -> float | None:
    if len(a) < 3 or len(b) < 3:
        return None
    se = math.sqrt(stdev(a) ** 2 / len(a) + stdev(b) ** 2 / len(b))
    difference = mean(b) - mean(a)
    if se == 0:
        return 0.0 if difference == 0 else math.copysign(math.inf, difference)
    return difference / se


def compare(before: Result | None, after: Result | None) -> Comparison:
    some = before or after
    assert some is not None
    if before is None:
        return Comparison(some.suite, some.key, None, after, None, "only after")
    if after is None:
        return Comparison(some.suite, some.key, before, None, None, "only before")

    change = None
    if before.value_ns > 0:
        change = (after.value_ns - before.value_ns) / before.value_ns * 100

    clear = False
    if before.runs_ns and after.runs_ns:
        t = welch_t(before.runs_ns, after.runs_ns)
        clear = t is not None and abs(t) > T_THRESHOLD
    elif before.suite == CATCH2 and None not in (before.low_ns, before.high_ns,
                                                  after.low_ns, after.high_ns):
        clear = after.low_ns > before.high_ns or after.high_ns < before.low_ns

    if not clear or after.value_ns == before.value_ns:
        verdict = "no clear change"
    elif after.value_ns < before.value_ns:
        verdict = "faster"
    else:
        verdict = "slower"
    return Comparison(some.suite, some.key, before, after, change, verdict)


def compare_sides(before: list[Result], after: list[Result]) -> list[Comparison]:
    before_by_key = {(r.suite, r.key): r for r in before}
    after_by_key = {(r.suite, r.key): r for r in after}
    keys = sorted(set(before_by_key) | set(after_by_key),
                  key=lambda k: (SUITES.index(k[0]) if k[0] in SUITES else len(SUITES), k[1]))
    return [compare(before_by_key.get(k), after_by_key.get(k)) for k in keys]


def format_ns(value: float) -> str:
    for unit, scale in (("s", 1e9), ("ms", 1e6), ("µs", 1e3)):
        if value >= scale:
            scaled = value / scale
            break
    else:
        unit, scaled = "ns", value
    decimals = max(0, 3 - int(math.floor(math.log10(scaled)))) if scaled > 0 else 1
    return f"{scaled:.{decimals}f} {unit}"


def _cell(text: str) -> str:
    return text.replace("|", "\\|")


def to_markdown(comparisons: list[Comparison], before_label: str, after_label: str) -> str:
    lines = [
        "## Benchmarks: before and after",
        "",
        f"- **Before:** {_cell(before_label)}",
        f"- **After:** {_cell(after_label)}",
        "",
        "Catch2 shows the mean, the e2e suite the median. A change is clear when the "
        "Catch2 95% confidence intervals do not overlap, or when Welch's t of the e2e "
        "runs exceeds 2. Shared CI runners are noisy: rerun before trusting a small change.",
    ]
    if not comparisons:
        lines += ["", "No benchmark results were found on either side."]
    for suite in SUITES + tuple(sorted({c.suite for c in comparisons} - set(SUITES))):
        rows = [c for c in comparisons if c.suite == suite]
        if not rows:
            continue
        lines += [
            "",
            f"### {suite}",
            "",
            "| Benchmark | Before | After | Change | Verdict |",
            "|-----------|-------:|------:|-------:|---------|",
        ]
        for c in rows:
            before = format_ns(c.before.value_ns) if c.before else "–"
            after = format_ns(c.after.value_ns) if c.after else "–"
            change = f"{c.change_percent:+.1f}%" if c.change_percent is not None else "–"
            lines.append(f"| {_cell(c.key)} | {before} | {after} | {change} | {c.verdict} |")
    return "\n".join(lines) + "\n"


def _result_json(result: Result | None) -> dict | None:
    if result is None:
        return None
    data: dict = {"value_ns": result.value_ns}
    if result.low_ns is not None:
        data["low_ns"] = result.low_ns
    if result.high_ns is not None:
        data["high_ns"] = result.high_ns
    if result.runs_ns:
        data["runs_ns"] = result.runs_ns
    return data


def to_json(comparisons: list[Comparison], before_label: str, after_label: str) -> dict:
    return {
        "before": {"label": before_label},
        "after": {"label": after_label},
        "benchmarks": [
            {
                "suite": c.suite,
                "key": c.key,
                # Catch2 reports the mean with its 95% confidence interval,
                # the e2e suite the median with P5 and P95.
                "statistic": "mean" if c.suite == CATCH2 else "median",
                "before": _result_json(c.before),
                "after": _result_json(c.after),
                "change_percent": c.change_percent,
                "verdict": c.verdict,
            }
            for c in comparisons
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--before", type=Path, required=True, help="results directory of the before build")
    parser.add_argument("--after", type=Path, required=True, help="results directory of the after build")
    parser.add_argument("--before-label", default="before")
    parser.add_argument("--after-label", default="after")
    parser.add_argument("--markdown", type=Path, help="write the Markdown here instead of stdout")
    parser.add_argument("--json", type=Path, help="write the comparison as JSON here")
    args = parser.parse_args(argv)

    comparisons = compare_sides(read_side(args.before), read_side(args.after))
    markdown = to_markdown(comparisons, args.before_label, args.after_label)
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown, encoding="utf-8")
    else:
        sys.stdout.write(markdown)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(to_json(comparisons, args.before_label, args.after_label), indent=2) + "\n",
            encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
