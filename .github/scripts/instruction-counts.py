#!/usr/bin/env python3
"""Instruction counts of the Catch2 benchmarks, before and after a pull request (#671).

The Instruction Counts workflow (.github/workflows/instruction-counts.yml)
runs every benchmark binary of tests/benchmarks under Callgrind in the
benchmarks' fixed-work mode (tests/benchmarks/instruction_count.h), once on
the base of a pull request and once on the pull request merged onto it, with
.github/scripts/instruction-counts.sh. Each benchmark then leaves one dump,
labelled "<test case> / <benchmark>", with the instructions of exactly one run
of its measured code, all threads together:

    <dumps>/<binary>/callgrind.out.<pid>.<part>   one per benchmark
    <dumps>/<binary>/exit_code                    written when the binary ended

Subcommands:

  collect DUMPS --json FILE
      Reads one side's dumps into a side file: every benchmark's count, and the
      binaries that failed (their counts are left out, since a binary that
      stopped early did not run all of its benchmarks).

  compare --before FILE --after FILE --before-sha SHA --after-sha SHA
          --head-sha SHA --pull-request N [--json FILE] [--markdown FILE]
      Compares two side files. The JSON is the workflow's artifact, which the
      Instruction Counts Comment workflow (and, later, the gate of #672) reads:

        {"schema_version": 1, "pull_request": N, "head_sha": "...",
         "before": {"sha": "...", "failed_binaries": [...]},
         "after":  {"sha": "...", "failed_binaries": [...]},
         "benchmarks": [{"binary": "...", "name": "...",
                         "metrics": {"instructions": {"before": int|null,
                                                      "after": int|null,
                                                      "change_percent": float|null}}}]}

      A metric is null on the side a benchmark does not exist on; further
      metrics (#673) go next to "instructions" in the same shape.

  gate --comparison FILE --labels FILE [--json FILE] [--markdown FILE]
      The gate of #672 (CI Build's "Instruction counts / gate" job): exits 1
      when a benchmark costs more than its threshold (THRESHOLD_PERCENT, else
      DEFAULT_THRESHOLD_PERCENT) more instructions than on the base and the
      pull request's labels (a JSON list of names) lack ACCEPT_LABEL, or when a
      benchmark of the base was not counted on the pull request. The JSON is
      the verdict the comment shows:

        {"schema_version": 1, "pull_request": N, "head_sha": "...",
         "passed": bool, "accepted": bool,
         "over_threshold": [{"binary": "...", "name": "...", "threshold_percent": float}],
         "missing": [{"binary": "...", "name": "..."}], "failed_binaries": [...]}

  comment --comparison FILE [--gate FILE] --repo OWNER/REPO --head-sha SHA
      Posts the report as a pull request comment, or updates the one posted
      before (found by MARKER). Run by the comment workflow, which never runs
      the pull request's code: the artifacts are data from that code, so they
      are validated against the schemas above (every benchmark the gate names
      must be one of the comparison's), only their numbers and escaped names
      reach the comment, and the pull request they name must have head-sha,
      the commit the measuring run was started for, as its head. Needs GH_TOKEN.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Callable

SCHEMA_VERSION = 1
MARKER = "<!-- logsquirl-instruction-counts -->"
COMMENT_AUTHOR = "github-actions[bot]"

# Two runs of the same commit differ by less than this (#671); a change within
# it is noise.
NOISE_PERCENT = 0.5

# The gate (#672): a benchmark that costs more than its threshold more
# instructions than on the base fails the pull request, unless a maintainer
# accepts the cost with ACCEPT_LABEL.
ACCEPT_LABEL = "perf-accepted"
DEFAULT_THRESHOLD_PERCENT = 2.0

# The benchmarks whose counts vary by more than 1 % between counts of the same
# code, each with its own threshold: twice the widest spread measured, rounded
# up to a whole percent. Measured as instruction-counts.sh counts (with
# --fair-sched=yes, and the pause that lets idle workers fall asleep): four
# counts of the same code in run 36785236485 (#671), and the later runs of #687,
# #696 and #700 for the benchmarks their changes do not reach. Every other
# benchmark repeats within 1 %, most within 0.5 %, so the default leaves them
# twice that. The spread comes from threads taking turns differently (glibc's
# malloc, oneTBB workers spinning). Keyed by binary and "<test case> /
# <benchmark>", as the comparison names them; the gate warns about an entry
# that names no benchmark any more.
THRESHOLD_PERCENT: dict[tuple[str, str], float] = {
    # widest spread 2.8 %
    ("logsquirl_logdata_benchmark", "Indexing a Log File / tabs and long lines: whole Log File"): 6.0,
    # 2.0 %
    ("logsquirl_textview_scroll_benchmark",
     "text view scroll benchmarks / scrollbar: dragged over 200 values"): 5.0,
    # 1.7 %
    ("logsquirl_textview_scroll_benchmark", "text view scroll benchmarks / data changed: a Log Line appended"): 4.0,
    # 1.6 %
    ("logsquirl_overview_selection_benchmark",
     "Extending a selection of 100,000 Log Lines / Shift+Down 20 times and Shift+Up 20 times"): 4.0,
    # 1.3 %
    ("logsquirl_textview_scroll_benchmark", "text view scroll benchmarks / wheel: 20 notches down and 20 up"): 3.0,
    # 1.2 %
    ("logsquirl_session_restore_benchmark",
     "Restoring a Session of several large Log Files / large Log Files: restore until every tab has loaded"): 3.0,
    # 1.1 %
    ("logsquirl_textview_scroll_benchmark", "text view scroll benchmarks / keys: 5 pages down and 5 up"): 3.0,
}
MAX_THRESHOLD_PERCENT = 1000.0

# Limits for the artifact the comment workflow reads.
MAX_BENCHMARKS = 2000
MAX_NAME_LENGTH = 400
SHA = re.compile(r"[0-9a-f]{40}")
BINARY = re.compile(r"[A-Za-z0-9_]{1,100}")

CLIENT_REQUEST = "Client Request: "


# ---------------------------------------------------------------------------
# Reading the dumps
# ---------------------------------------------------------------------------

def parse_dump(text: str) -> tuple[str | None, int]:
    """The label a benchmark gave a dump (None for any other dump) and its instruction count.

    The count is the Ir column of the "totals:" line: every cost the dump
    wrote, which with --separate-threads=no (the default) is every thread's
    since the counters were last zeroed.
    """
    label = None
    events: list[str] = []
    totals: list[str] | None = None
    for line in text.splitlines():
        if line.startswith("desc: Trigger: "):
            trigger = line[len("desc: Trigger: "):]
            if trigger.startswith(CLIENT_REQUEST):
                label = trigger[len(CLIENT_REQUEST):]
        elif line.startswith("events:"):
            events = line[len("events:"):].split()
        elif line.startswith("totals:"):
            totals = line[len("totals:"):].split()
    if totals is None:
        raise ValueError("the dump has no totals line")
    column = events.index("Ir") if "Ir" in events else 0
    return label, int(totals[column]) if column < len(totals) else 0


def collect(dumps: Path) -> dict[str, Any]:
    """One side's counts: every benchmark of every binary that ran to its end."""
    benchmarks: list[dict[str, Any]] = []
    failed: list[str] = []
    for directory in sorted(p for p in dumps.iterdir() if p.is_dir()):
        binary = directory.name
        exit_code = directory / "exit_code"
        if not exit_code.is_file() or exit_code.read_text(encoding="utf-8").strip() != "0":
            failed.append(binary)
            continue
        counts: dict[str, int] = {}
        order: list[str] = []
        for path in sorted(directory.glob("callgrind.out.*"), key=_dump_order):
            label, instructions = parse_dump(path.read_text(encoding="utf-8", errors="replace"))
            if label is None:
                continue
            if label not in counts:
                order.append(label)
                counts[label] = 0
            counts[label] += instructions
        benchmarks.extend({"binary": binary, "name": label, "instructions": counts[label]}
                          for label in order)
    return {"schema_version": SCHEMA_VERSION, "benchmarks": benchmarks, "failed_binaries": failed}


def _dump_order(path: Path) -> tuple[int, ...]:
    """callgrind.out.<pid>.<part>: by pid, then in the order the dumps were written."""
    return tuple(int(part) for part in path.name.split(".")[2:] if part.isdigit())


# ---------------------------------------------------------------------------
# Comparing
# ---------------------------------------------------------------------------

def change_percent(before: int | None, after: int | None) -> float | None:
    if before is None or after is None or before == 0:
        return None
    return (after - before) / before * 100.0


def compare(before: dict[str, Any], after: dict[str, Any], *, before_sha: str, after_sha: str,
            head_sha: str, pull_request: int) -> dict[str, Any]:
    def counts(data: dict[str, Any]) -> dict[tuple[str, str], int]:
        return {(b["binary"], b["name"]): b["instructions"] for b in data["benchmarks"]}

    before_counts, after_counts = counts(before), counts(after)
    benchmarks = []
    for binary, name in sorted(before_counts.keys() | after_counts.keys()):
        b = before_counts.get((binary, name))
        a = after_counts.get((binary, name))
        benchmarks.append({"binary": binary, "name": name, "metrics": {"instructions": {
            "before": b, "after": a, "change_percent": change_percent(b, a)}}})
    return {
        "schema_version": SCHEMA_VERSION,
        "pull_request": pull_request,
        "head_sha": head_sha,
        "before": {"sha": before_sha, "failed_binaries": list(before["failed_binaries"])},
        "after": {"sha": after_sha, "failed_binaries": list(after["failed_binaries"])},
        "benchmarks": benchmarks,
    }


# ---------------------------------------------------------------------------
# The gate (#672)
# ---------------------------------------------------------------------------

Thresholds = dict[tuple[str, str], float]


def threshold_percent(binary: str, name: str, thresholds: Thresholds | None = None) -> float:
    """How many percent more instructions than on the base a benchmark may cost."""
    table = THRESHOLD_PERCENT if thresholds is None else thresholds
    return table.get((binary, name), DEFAULT_THRESHOLD_PERCENT)


def evaluate_gate(data: dict[str, Any], *, labels: list[str],
                  thresholds: Thresholds | None = None) -> dict[str, Any]:
    """The gate's verdict on a comparison.

    Fails on a benchmark that costs more than its threshold more instructions
    than on the base, unless the pull request carries ACCEPT_LABEL; and, label
    or not, on a benchmark counted on the base but not on the pull request (it
    disappeared or no longer runs), on a binary that failed on the pull
    request's side, and when nothing was counted at all. A new benchmark, or one
    that does not build on the base, has nothing to be compared with and passes.
    """
    over: list[dict[str, Any]] = []
    missing: list[dict[str, str]] = []
    for entry in data["benchmarks"]:
        i = entry["metrics"]["instructions"]
        if i["before"] is not None and i["after"] is None:
            missing.append({"binary": entry["binary"], "name": entry["name"]})
            continue
        change = change_percent(i["before"], i["after"])
        threshold = threshold_percent(entry["binary"], entry["name"], thresholds)
        if change is not None and change > threshold:
            over.append({"binary": entry["binary"], "name": entry["name"], "threshold_percent": threshold})
    failed = list(data["after"]["failed_binaries"])
    counted = any(b["metrics"]["instructions"]["after"] is not None for b in data["benchmarks"])
    accepted = ACCEPT_LABEL in labels and bool(over)
    passed = counted and not missing and not failed and (not over or accepted)
    return {"schema_version": SCHEMA_VERSION, "pull_request": data["pull_request"], "head_sha": data["head_sha"],
            "passed": passed, "accepted": accepted, "over_threshold": over, "missing": missing,
            "failed_binaries": failed}


def unused_thresholds(data: dict[str, Any], thresholds: Thresholds | None = None) -> list[tuple[str, str]]:
    """The threshold entries that name no benchmark of the comparison."""
    table = THRESHOLD_PERCENT if thresholds is None else thresholds
    names = {(b["binary"], b["name"]) for b in data["benchmarks"]}
    return [key for key in table if key not in names]


# ---------------------------------------------------------------------------
# Validating the artifact
# ---------------------------------------------------------------------------

def _require(condition: bool, what: str) -> None:
    if not condition:
        raise ValueError(f"the comparison does not match the schema: {what}")


def _is_count(value: Any) -> bool:
    return value is None or (type(value) is int and 0 <= value < 10**15)


def validate(data: Any) -> dict[str, Any]:
    """The comparison, if it matches the schema exactly; ValueError otherwise.

    Returns only the fields the schema has, so nothing else from the artifact
    goes any further.
    """
    _require(isinstance(data, dict), "not an object")
    _require(data.get("schema_version") == SCHEMA_VERSION, "schema_version")
    pull_request = data.get("pull_request")
    _require(type(pull_request) is int and 0 < pull_request < 10**9, "pull_request")
    _require(isinstance(data.get("head_sha"), str) and SHA.fullmatch(data["head_sha"]) is not None,
             "head_sha")
    sides = {}
    for key in ("before", "after"):
        s = data.get(key)
        _require(isinstance(s, dict), key)
        _require(isinstance(s.get("sha"), str) and SHA.fullmatch(s["sha"]) is not None, f"{key}.sha")
        failed = s.get("failed_binaries")
        _require(isinstance(failed, list) and len(failed) <= MAX_BENCHMARKS
                 and all(isinstance(f, str) and BINARY.fullmatch(f) for f in failed),
                 f"{key}.failed_binaries")
        sides[key] = {"sha": s["sha"], "failed_binaries": list(failed)}
    raw = data.get("benchmarks")
    _require(isinstance(raw, list) and len(raw) <= MAX_BENCHMARKS, "benchmarks")
    benchmarks = []
    for entry in raw:
        _require(isinstance(entry, dict), "benchmark")
        _require(isinstance(entry.get("binary"), str) and BINARY.fullmatch(entry["binary"]) is not None,
                 "benchmark binary")
        _require(isinstance(entry.get("name"), str) and 0 < len(entry["name"]) <= MAX_NAME_LENGTH,
                 "benchmark name")
        metrics = entry.get("metrics")
        _require(isinstance(metrics, dict) and isinstance(metrics.get("instructions"), dict),
                 "benchmark metrics")
        instructions = metrics["instructions"]
        _require(_is_count(instructions.get("before")) and _is_count(instructions.get("after")),
                 "instruction counts")
        b, a = instructions.get("before"), instructions.get("after")
        benchmarks.append({"binary": entry["binary"], "name": entry["name"], "metrics": {
            "instructions": {"before": b, "after": a, "change_percent": change_percent(b, a)}}})
    return {"schema_version": SCHEMA_VERSION, "pull_request": pull_request, "head_sha": data["head_sha"],
            "before": sides["before"], "after": sides["after"], "benchmarks": benchmarks}


def validate_gate(gate: Any, comparison: dict[str, Any]) -> dict[str, Any]:
    """The gate's verdict, if it matches its schema and the validated comparison; ValueError otherwise.

    Every benchmark it names must be one of the comparison's, so the comment
    shows only counts that came through validate().
    """
    _require(isinstance(gate, dict), "gate: not an object")
    _require(gate.get("schema_version") == SCHEMA_VERSION, "gate: schema_version")
    _require(type(gate.get("pull_request")) is int and gate["pull_request"] == comparison["pull_request"],
             "gate: pull_request")
    _require(gate.get("head_sha") == comparison["head_sha"], "gate: head_sha")
    _require(type(gate.get("passed")) is bool and type(gate.get("accepted")) is bool, "gate: verdict")
    names = {(b["binary"], b["name"]) for b in comparison["benchmarks"]}

    def benchmarks(key: str, with_threshold: bool) -> list[dict[str, Any]]:
        raw = gate.get(key)
        _require(isinstance(raw, list) and len(raw) <= MAX_BENCHMARKS, f"gate: {key}")
        result = []
        for entry in raw:
            _require(isinstance(entry, dict) and (entry.get("binary"), entry.get("name")) in names,
                     f"gate: {key} names a benchmark the comparison does not have")
            clean = {"binary": entry["binary"], "name": entry["name"]}
            if with_threshold:
                threshold = entry.get("threshold_percent")
                _require(type(threshold) in (int, float) and 0 < threshold <= MAX_THRESHOLD_PERCENT,
                         f"gate: {key} threshold")
                clean["threshold_percent"] = float(threshold)
            result.append(clean)
        return result

    over = benchmarks("over_threshold", True)
    missing = benchmarks("missing", False)
    failed = gate.get("failed_binaries")
    _require(isinstance(failed, list) and len(failed) <= MAX_BENCHMARKS
             and all(isinstance(f, str) and BINARY.fullmatch(f) for f in failed), "gate: failed_binaries")
    return {"schema_version": SCHEMA_VERSION, "pull_request": gate["pull_request"], "head_sha": gate["head_sha"],
            "passed": gate["passed"], "accepted": gate["accepted"], "over_threshold": over, "missing": missing,
            "failed_binaries": list(failed)}


# ---------------------------------------------------------------------------
# The report
# ---------------------------------------------------------------------------

_MARKDOWN_SPECIAL = re.compile(r"([\\`*_\[\]|~#!()])")


def escape(text: str) -> str:
    """Plain text in a Markdown table cell: no markup, no HTML, no mention."""
    text = " ".join(text.split())
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = _MARKDOWN_SPECIAL.sub(r"\\\1", text)
    return text.replace("@", "@&#8203;")


def short_binary(binary: str) -> str:
    name = binary.removeprefix("logsquirl_")
    return name.removesuffix("_benchmark") or binary


def _count(value: int | None) -> str:
    return "" if value is None else f"{value:,}"


def _change(before: int | None, after: int | None) -> str:
    if before is None and after is not None:
        return "new"
    if after is None and before is not None:
        return "missing"
    change = change_percent(before, after)
    return "" if change is None else f"{change:+.2f} %"


def is_change(instructions: dict[str, Any]) -> bool:
    before, after = instructions["before"], instructions["after"]
    if (before is None) != (after is None):
        return True
    change = change_percent(before, after)
    return change is not None and abs(change) > NOISE_PERCENT


def _table(benchmarks: list[dict[str, Any]]) -> list[str]:
    lines = ["| Benchmark | Before | After | Change |", "|---|---:|---:|---:|"]
    for entry in benchmarks:
        i = entry["metrics"]["instructions"]
        name = escape(f"{short_binary(entry['binary'])}: {entry['name']}")
        lines.append(f"| {name} | {_count(i['before'])} | {_count(i['after'])} | "
                     f"{_change(i['before'], i['after'])} |")
    return lines


def _percent(value: float) -> str:
    return f"+{value:.1f} %"


def render_gate(data: dict[str, Any], gate: dict[str, Any]) -> list[str]:
    """The gate's verdict as Markdown lines: what it failed on, or what was accepted."""
    entries = {(b["binary"], b["name"]): b for b in data["benchmarks"]}
    if gate["passed"]:
        headline = f"**Gate: accepted with `{ACCEPT_LABEL}`.**" if gate["accepted"] else "**Gate: passed.**"
    else:
        headline = "**Gate: failed.**"
    lines = [headline]
    if not gate["over_threshold"] and gate["passed"]:
        lines[0] += (f" No benchmark costs more instructions than its threshold above the base: "
                     f"{_percent(DEFAULT_THRESHOLD_PERCENT)}, or more for the few that vary more "
                     "(`THRESHOLD_PERCENT` in `.github/scripts/instruction-counts.py`).")
    lines.append("")
    over = gate["over_threshold"]
    if over:
        count = f"{len(over)} benchmark{' costs' if len(over) == 1 else 's cost'}"
        if gate["accepted"]:
            lines += [f"{count} more instructions than {'its' if len(over) == 1 else 'their'} threshold above "
                      f"the base, accepted with the label `{ACCEPT_LABEL}`; the pull request description "
                      "says why:", ""]
        else:
            lines += [f"{count} more instructions than {'its' if len(over) == 1 else 'their'} threshold above "
                      f"the base. If the cost is intended, a maintainer adds the label `{ACCEPT_LABEL}`, the "
                      "pull request description says why, and the gate runs again (CONTRIBUTING.md, "
                      "*Instruction count gate*):", ""]
        lines += ["| Benchmark | Before | After | Change | Threshold |", "|---|---:|---:|---:|---:|"]
        for o in over:
            i = entries[(o["binary"], o["name"])]["metrics"]["instructions"]
            name = escape(f"{short_binary(o['binary'])}: {o['name']}")
            lines.append(f"| {name} | {_count(i['before'])} | {_count(i['after'])} | "
                         f"{_change(i['before'], i['after'])} | {_percent(o['threshold_percent'])} |")
        lines.append("")
    if gate["missing"]:
        lines += ["Counted on the base but not on this pull request: the benchmark was removed or no longer "
                  f"runs, which `{ACCEPT_LABEL}` does not accept:", ""]
        lines += [f"- {escape(short_binary(m['binary']) + ': ' + m['name'])}" for m in gate["missing"]]
        lines.append("")
    if gate["failed_binaries"]:
        lines += ["A benchmark binary failed on the after side (named below), so its benchmarks are not "
                  "counted.", ""]
    if not gate["passed"] and not over and not gate["missing"] and not gate["failed_binaries"]:
        lines += ["Nothing was counted on the after side.", ""]
    return lines


def render_markdown(data: dict[str, Any], *, comment: bool = False, gate: dict[str, Any] | None = None) -> str:
    benchmarks = data["benchmarks"]
    changed = [b for b in benchmarks if is_change(b["metrics"]["instructions"])]
    total = len(benchmarks)
    lines = [MARKER] if comment else []
    lines += [
        "### Instruction counts",
        "",
        "Instructions of one run of each Catch2 benchmark (`tests/benchmarks`), counted by Callgrind: "
        f"before is the base, {data['before']['sha'][:12]}, after is this pull request merged onto it, "
        f"{data['after']['sha'][:12]}. Two runs of the same commit differ by less than {NOISE_PERCENT} % "
        "for most benchmarks, so a smaller change is noise; a few that wait for other threads vary by "
        "up to about 3 %. BUILD.md, *Instruction counts*, shows how to count one locally.",
        "",
    ]
    if gate is not None:
        lines += render_gate(data, gate)
    for key in ("before", "after"):
        failed = data[key]["failed_binaries"]
        if failed:
            names = ", ".join(escape(f) for f in failed)
            lines += [f"**Not counted on the {key} side**, the binary failed: {names}", ""]
    summary = f"{total} benchmark{'' if total == 1 else 's'}, "
    summary += (f"{len(changed)} changed by more than {NOISE_PERCENT} %" if changed
                else f"none changed by more than {NOISE_PERCENT} %")
    lines += [f"**{summary}**", ""]
    if changed:
        lines += _table(changed) + [""]
    lines += ["<details><summary>All benchmarks</summary>", ""]
    lines += _table(benchmarks)
    lines += ["", "</details>", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Posting the comment
# ---------------------------------------------------------------------------

Api = Callable[[str, str, "dict[str, Any] | None"], Any]


def gh_api(method: str, path: str, body: dict[str, Any] | None = None) -> Any:
    """`gh api`; GET requests are paginated into one list."""
    args = ["gh", "api", "--method", method, path]
    if method == "GET":
        args += ["--paginate", "--slurp"]
    if body is not None:
        args += ["--input", "-"]
    result = subprocess.run(args, input=json.dumps(body) if body is not None else None,
                            check=True, capture_output=True, text=True)
    data = json.loads(result.stdout) if result.stdout.strip() else None
    if method == "GET" and isinstance(data, list) and data and all(isinstance(p, list) for p in data):
        return [item for page in data for item in page]
    if method == "GET" and isinstance(data, list) and len(data) == 1 and isinstance(data[0], dict):
        return data[0]
    return data


def post_comment(data: dict[str, Any], *, repo: str, head_sha: str, api: Api | None = None,
                 gate: dict[str, Any] | None = None) -> str:
    """Creates or updates the report comment; returns what it did."""
    api = api or gh_api
    number = data["pull_request"]
    pull = api("GET", f"repos/{repo}/pulls/{number}", None)
    if data["head_sha"] != head_sha or pull.get("head", {}).get("sha") != head_sha:
        print(f"::notice::Pull request #{number} is not at {head_sha[:12]} any more (or never was); "
              "not commenting")
        return "skipped"
    body = {"body": render_markdown(data, comment=True, gate=gate)}
    for existing in api("GET", f"repos/{repo}/issues/{number}/comments", None) or []:
        if (existing.get("user", {}).get("login") == COMMENT_AUTHOR
                and str(existing.get("body", "")).startswith(MARKER)):
            api("PATCH", f"repos/{repo}/issues/comments/{int(existing['id'])}", body)
            return "updated"
    api("POST", f"repos/{repo}/issues/{number}/comments", body)
    return "created"


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("collect")
    p.add_argument("dumps", type=Path)
    p.add_argument("--json", type=Path, required=True)

    p = sub.add_parser("compare")
    p.add_argument("--before", type=Path, required=True)
    p.add_argument("--after", type=Path, required=True)
    p.add_argument("--before-sha", required=True)
    p.add_argument("--after-sha", required=True)
    p.add_argument("--head-sha", required=True)
    p.add_argument("--pull-request", type=int, required=True)
    p.add_argument("--json", type=Path)
    p.add_argument("--markdown", type=Path)

    p = sub.add_parser("gate")
    p.add_argument("--comparison", type=Path, required=True)
    p.add_argument("--labels", type=Path, required=True)
    p.add_argument("--json", type=Path)
    p.add_argument("--markdown", type=Path)

    p = sub.add_parser("comment")
    p.add_argument("--comparison", type=Path, required=True)
    p.add_argument("--gate", type=Path)
    p.add_argument("--repo", required=True)
    p.add_argument("--head-sha", required=True)

    args = parser.parse_args(argv)

    if args.command == "collect":
        result = collect(args.dumps)
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        for binary in result["failed_binaries"]:
            print(f"::error::{binary} failed; its benchmarks are not counted")
        return 0

    if args.command == "compare":
        def load(path: Path) -> dict[str, Any]:
            if not path.is_file():
                return {"schema_version": SCHEMA_VERSION, "benchmarks": [], "failed_binaries": []}
            return json.loads(path.read_text(encoding="utf-8"))

        result = compare(load(args.before), load(args.after), before_sha=args.before_sha,
                         after_sha=args.after_sha, head_sha=args.head_sha,
                         pull_request=args.pull_request)
        validate(result)
        if args.json:
            args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        markdown = render_markdown(result)
        if args.markdown:
            args.markdown.write_text(markdown, encoding="utf-8")
        else:
            sys.stdout.write(markdown)
        return 0

    if args.command == "gate":
        return run_gate(args)

    if not SHA.fullmatch(args.head_sha):
        print("::error::--head-sha is not a commit SHA", file=sys.stderr)
        return 1
    try:
        data = validate(json.loads(args.comparison.read_text(encoding="utf-8")))
    except (ValueError, OSError) as error:  # json.JSONDecodeError is a ValueError
        print(f"::error::The instruction counts artifact is not usable: {error}", file=sys.stderr)
        return 1
    gate = None
    if args.gate:
        try:
            gate = validate_gate(json.loads(args.gate.read_text(encoding="utf-8")), data)
        except (ValueError, OSError) as error:
            # The counts are still worth showing; the check shows the verdict.
            print(f"::warning::The gate artifact is not usable, the comment leaves it out: {error}")
    result = post_comment(data, repo=args.repo, head_sha=args.head_sha, gate=gate)
    print(f"Comment on #{data['pull_request']}: {result}")
    return 0


def run_gate(args: argparse.Namespace) -> int:
    """The gate subcommand: 0 when the pull request passes, 1 when it fails."""
    try:
        data = validate(json.loads(args.comparison.read_text(encoding="utf-8")))
    except (ValueError, OSError) as error:
        print(f"::error::No usable instruction counts to judge: {error}")
        return 1
    try:
        labels = json.loads(args.labels.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        print(f"::error::The pull request's labels are not readable: {error}")
        return 1
    if not isinstance(labels, list) or not all(isinstance(label, str) for label in labels):
        print("::error::The pull request's labels are not a list of names")
        return 1
    for binary, name in unused_thresholds(data):
        print(f"::warning::THRESHOLD_PERCENT names {binary}: {name}, which was not counted; "
              "rename or remove its entry")
    gate = evaluate_gate(data, labels=labels)
    if args.json:
        args.json.write_text(json.dumps(gate, indent=2) + "\n", encoding="utf-8")
    markdown = "\n".join(["### Instruction counts gate", ""] + render_gate(data, gate))
    if args.markdown:
        args.markdown.write_text(markdown, encoding="utf-8")
    else:
        sys.stdout.write(markdown)
    counts = {(b["binary"], b["name"]): b["metrics"]["instructions"] for b in data["benchmarks"]}
    for o in gate["over_threshold"]:
        level = "notice" if gate["accepted"] else "error"
        i = counts[(o["binary"], o["name"])]
        print(f"::{level}::{o['binary']}: {o['name']} costs {change_percent(i['before'], i['after']):+.2f} % "
              "instructions, "
              f"threshold {_percent(o['threshold_percent'])}"
              + (f" (accepted with {ACCEPT_LABEL})" if gate["accepted"] else ""))
    for m in gate["missing"]:
        print(f"::error::{m['binary']}: {m['name']} was counted on the base but not on this pull request")
    for binary in gate["failed_binaries"]:
        print(f"::error::{binary} failed on the after side")
    print("The gate " + ("passed" if gate["passed"] else "failed"))
    return 0 if gate["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
