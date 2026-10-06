#!/usr/bin/env python3
"""Writes the pull request comment of the Benchmarks workflow (#674).

The Benchmarks workflow (.github/workflows/benchmarks.yml) runs for a pull
request that changes a hot path or carries the `performance` label, and leaves
the pull request number and its comparison (benchmark-compare.py's JSON) as
artifacts. The Benchmarks Comment workflow (benchmarks-comment.yml) runs this
script from the default branch to turn them into the one comment it keeps on
the pull request.

Both files were written by a run of the pull request's own code, so they are
untrusted: the number must be plain digits, the comparison must have the shape
benchmark-compare.py writes (anything else is reported as missing), every
number must be finite, and every piece of text is escaped so it can neither
break the table, nor mention anyone, nor carry HTML or links of its own.

Usage:
  benchmark-pr-comment.py marker
      Prints the hidden marker the comment starts with, which finds it again.
  benchmark-pr-comment.py comment-id < COMMENTS
      Prints the id of the earlier comment among the bot's comments on stdin,
      one {"id", "body"} JSON object per line, or nothing if there is none.
  benchmark-pr-comment.py number FILE
      Prints the pull request number in FILE, or fails.
  benchmark-pr-comment.py body --comparison FILE --run-url URL --conclusion TEXT --out FILE
      Writes the comment. A missing or unusable comparison gives a comment
      that says the run did not produce one.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import re
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

# Finds the comment again on a rerun. Its own marker, so the instruction count
# comment (#671) is never taken for this one.
MARKER = "<!-- logsquirl-benchmarks-wall-clock -->"

# GitHub refuses a comment above 65536 characters.
MAX_COMMENT_CHARS = 60000
MAX_TEXT_CHARS = 200

VERDICTS = ("faster", "slower", "no clear change", "only before", "only after")
CLEAR = ("faster", "slower")

_NUMBER = re.compile(r"[1-9][0-9]{0,9}")
_RUN_URL = re.compile(r"https://github\.com/[A-Za-z0-9-]+/[A-Za-z0-9._-]+/actions/runs/[0-9]+(/attempts/[0-9]+)?")
# Markdown punctuation that could start emphasis, code, a link or an image (both
# need a bracket) or end a table cell; escaped with a backslash, which renders as the character.
_MARKDOWN = re.compile(r"([\\`*_\[\]|~])")

# The times are formatted as in the job summary.
_SPEC = importlib.util.spec_from_file_location(
    "_benchmark_compare_for_comment", Path(__file__).with_name("benchmark-compare.py"))
_compare = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _compare
_SPEC.loader.exec_module(_compare)
format_ns = _compare.format_ns


@dataclass
class Row:
    suite: str
    key: str
    before_ns: float | None
    after_ns: float | None
    change_percent: float | None
    verdict: str


@dataclass
class Comparison:
    before_label: str
    after_label: str
    rows: list[Row]


def read_pull_request_number(path: Path) -> int:
    try:
        text = path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeDecodeError) as error:
        raise ValueError(f"no pull request number: {error}") from error
    if not _NUMBER.fullmatch(text):
        raise ValueError("the pull request number is not plain digits")
    return int(text)


def find_comment_id(lines: Iterable[str]) -> int | None:
    """The id of this workflow's earlier comment among the bot's comments, one
    `{"id": ..., "body": ...}` JSON object per line, as the workflow's
    `gh api --jq` lists them (#764); None if there is none yet."""
    for line in lines:
        if not line.strip():
            continue
        try:
            comment = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"a comment line is not JSON: {error}") from error
        if (not isinstance(comment, dict) or type(comment.get("id")) is not int
                or not isinstance(comment.get("body"), str)):
            raise ValueError("a comment line is not an object with an id and a body")
        if comment["body"].startswith(MARKER):
            return comment["id"]
    return None


def text(value: object) -> str:
    """Untrusted text as inert Markdown on one line."""
    if not isinstance(value, str):
        raise ValueError("expected text")
    value = "".join(" " if not c.isprintable() else c for c in value)
    if len(value) > MAX_TEXT_CHARS:
        value = value[:MAX_TEXT_CHARS] + "…"
    # HTML as entities, which Markdown and an HTML block both show as text.
    value = value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    value = _MARKDOWN.sub(r"\\\1", value)
    # A zero-width space after @ keeps GitHub from turning it into a mention.
    return value.replace("@", "@" + chr(0x200B))


def _number(value: object, allow_none: bool = True) -> float | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("expected a finite number")
    return float(value)


def _side(value: object) -> float | None:
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValueError("expected a measurement")
    result = _number(value.get("value_ns"), allow_none=False)
    if result < 0:
        raise ValueError("a negative time")
    return result


def _label(data: dict, side: str) -> str:
    entry = data.get(side)
    if isinstance(entry, dict) and isinstance(entry.get("label"), str):
        return text(entry["label"])
    return side


def load_comparison(path: Path) -> Comparison:
    """Reads benchmark-compare.py's JSON; anything else raises ValueError."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError, RecursionError) as error:
        raise ValueError(f"not a comparison: {error}") from error
    if not isinstance(data, dict) or not isinstance(data.get("benchmarks"), list):
        raise ValueError("not a comparison: no list of benchmarks")
    rows = []
    for entry in data["benchmarks"]:
        if not isinstance(entry, dict):
            raise ValueError("a benchmark that is not an object")
        verdict = entry.get("verdict")
        rows.append(Row(
            suite=text(entry.get("suite")),
            key=text(entry.get("key")),
            before_ns=_side(entry.get("before")),
            after_ns=_side(entry.get("after")),
            change_percent=_number(entry.get("change_percent")),
            verdict=verdict if verdict in VERDICTS else "?",
        ))
    return Comparison(_label(data, "before"), _label(data, "after"), rows)


def _table(rows: list[Row]) -> list[str]:
    lines = [
        "| Benchmark | Before | After | Change | Verdict |",
        "|-----------|-------:|------:|-------:|---------|",
    ]
    for row in rows:
        before = format_ns(row.before_ns) if row.before_ns is not None else "–"
        after = format_ns(row.after_ns) if row.after_ns is not None else "–"
        change = f"{row.change_percent:+.1f}%" if row.change_percent is not None else "–"
        lines.append(f"| {row.key} | {before} | {after} | {change} | {row.verdict} |")
    return lines


def _summary(rows: list[Row]) -> str:
    count = {verdict: sum(1 for r in rows if r.verdict == verdict) for verdict in VERDICTS}
    parts = [f"{count['faster']} faster", f"{count['slower']} slower",
             f"{count['no clear change']} without a clear change"]
    one_side = count["only before"] + count["only after"]
    if one_side:
        parts.append(f"{one_side} measured on one side only")
    return ", ".join(parts)


def _body(comparison: Comparison | None, run_url: str, conclusion: str,
          reason: str | None, limit: int | None) -> str:
    lines = [
        MARKER,
        "## Benchmarks: before and after",
        "",
        "Wall-clock times of an optimized build, both sides measured one after the other on the "
        "same shared runner. This is a report, not a gate: a shared runner varies by several "
        "percent from run to run, so rerun before trusting a small change.",
        "",
    ]
    if comparison is None:
        lines += [
            f"The [Benchmarks run]({run_url}) did not produce a comparison "
            f"(conclusion: {text(conclusion)}).",
        ]
        if reason:
            lines += ["", f"The comparison it left could not be read: {text(reason)}"]
        return "\n".join(lines) + "\n"

    lines += [
        f"- **Before:** {comparison.before_label}",
        f"- **After:** {comparison.after_label}",
        f"- **Run:** [Benchmarks]({run_url}); the raw reports are its `benchmark-results` artifact",
        "",
        f"**{_summary(comparison.rows)}.** Catch2 shows the mean, the e2e suite the median. A change "
        "is clear when the Catch2 95% confidence intervals do not overlap, or when Welch's t of "
        "the e2e runs exceeds 2.",
    ]
    if conclusion != "success":
        lines += ["", f"The run did not finish cleanly (conclusion: {text(conclusion)}): "
                      "benchmarks that failed or did not run are missing below."]
    if not comparison.rows:
        lines += ["", "No benchmark results were found on either side."]
        return "\n".join(lines) + "\n"

    left_out = 0

    def take(rows: list[Row]) -> list[Row]:
        nonlocal left_out
        if limit is None or len(rows) <= limit:
            return rows
        left_out += len(rows) - limit
        return rows[:limit]

    clear = [r for r in comparison.rows if r.verdict in CLEAR]
    if clear:
        lines += ["", "### Clear changes", ""] + _table(take(clear))
    suites = list(dict.fromkeys(r.suite for r in comparison.rows))
    for suite in suites:
        rows = [r for r in comparison.rows if r.suite == suite]
        lines += ["", "<details>", f"<summary>{suite}: every benchmark ({len(rows)})</summary>",
                  ""] + _table(take(rows)) + ["", "</details>"]
    if left_out:
        lines += ["", f"{left_out} rows left out to fit a comment; the run's summary has them all."]
    return "\n".join(lines) + "\n"


def render(comparison: Comparison | None, run_url: str, conclusion: str,
           reason: str | None = None) -> str:
    if not _RUN_URL.fullmatch(run_url):
        raise ValueError("the run URL is not a GitHub Actions run")
    body = _body(comparison, run_url, conclusion, reason, None)
    limit = max((len(comparison.rows) if comparison else 0), 1)
    while len(body) > MAX_COMMENT_CHARS and limit > 0:
        limit //= 2
        body = _body(comparison, run_url, conclusion, reason, limit)
    return body


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("marker", help="print the hidden marker the comment starts with")
    commands.add_parser("comment-id",
                        help="print the id of the earlier comment among the bot's comments on stdin")
    number =commands.add_parser("number", help="print the validated pull request number")
    number.add_argument("file", type=Path)
    body = commands.add_parser("body", help="write the comment")
    body.add_argument("--comparison", type=Path, required=True)
    body.add_argument("--run-url", required=True)
    body.add_argument("--conclusion", required=True)
    body.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    if args.command == "marker":
        print(MARKER)
        return 0
    if args.command == "comment-id":
        try:
            found = find_comment_id(sys.stdin)
        except ValueError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        if found is not None:
            print(found)
        return 0
    if args.command == "number":
        try:
            print(read_pull_request_number(args.file))
        except ValueError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        return 0

    comparison = None
    reason = None
    if args.comparison.is_file():
        try:
            comparison = load_comparison(args.comparison)
        except ValueError as error:
            reason = str(error)
    args.out.write_text(render(comparison, args.run_url, args.conclusion, reason), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
