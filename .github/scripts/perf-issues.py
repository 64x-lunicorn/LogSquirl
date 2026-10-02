#!/usr/bin/env python3
"""Plans the issues the nightly Performance run files for its findings (#677).

perf-history.py writes the findings of a run of master: an instruction count
that moved up at a change point, a Budget it broke, a benchmark that went
missing, each with the scenario it belongs to and the commit range from the
last good run to the first bad one. This script decides, per scenario, what
happens on the issue tracker, and writes the issue texts; the workflow then
only calls `gh issue create/edit/comment` with them, in a job that may write
issues and nothing else.

One issue per scenario, found again by the marker SCENARIO_MARKER in its body
(the title may be edited freely):

- no issue of the scenario is open: one is opened, labelled needs-triage and
  performance;
- one is open: its body is replaced with this run's findings (the numbers of
  the night, the run that saw them), and when the findings are not the ones
  it already holds (another benchmark, another change point) a comment says
  what is new, so the change notifies;
- none is open, but one was closed holding exactly these findings (the
  findings marker: kind, benchmark and first bad commit of each):
  nothing happens. A maintainer closed it for these findings; the run reports
  them in its summary all the same.

A run of any branch other than the default one (dest "trial") files nothing:
plan refuses it.

Usage:
  perf-issues.py plan --comparison comparison.json --issues issues.json \\
      --server-url URL --repo OWNER/REPO --out DIR

issues.json is `gh issue list --label performance --state all --json
number,state,title,body`. DIR gets plan.json, a list of actions
({"action": "create"|"edit"|"comment"|"skip", "scenario", "number"?,
"title"?, "labels"?, "body_file"?, "reason"?}), and the bodies they name.

Exit status: 0 with a plan (possibly empty), 2 on unusable input or a run
that may not file.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path
from types import ModuleType

MARKER = "logsquirl-nightly-performance"
LABELS = ["needs-triage", "performance"]


def _load(name: str, filename: str) -> ModuleType:
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


pb = _load("perf_budgets", "perf-budgets.py")


def scenario_marker(scenario: str) -> str:
    return f"<!-- {MARKER} scenario={scenario} -->"


def finding_id(f: dict) -> str:
    """What a finding is, without the numbers of the night: its kind, its
    benchmark and its first bad commit."""
    first_bad = (f.get("first_bad") or {}).get("commit", "")
    text = json.dumps([f["kind"], f.get("budget_key") or f["benchmark"], first_bad])
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]


def fingerprint(found: list[dict]) -> str:
    """What the findings are, together."""
    text = ",".join(sorted(finding_id(f) for f in found))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def findings_marker(found: list[dict]) -> str:
    ids = ",".join(sorted(finding_id(f) for f in found))
    return f"<!-- {MARKER} findings={fingerprint(found)} ids={ids} -->"


_FINDINGS = re.compile(rf"<!-- {MARKER} findings=([0-9a-f]+) ids=([0-9a-f,]*) -->")


def _held(issue: dict) -> tuple[str | None, set[str]]:
    """The fingerprint and the finding ids an issue's body holds."""
    match = _FINDINGS.search(issue.get("body") or "")
    if match is None:
        return None, set()
    return match.group(1), set(filter(None, match.group(2).split(",")))


def title(scenario: str) -> str:
    return f"The nightly performance run found a regression in {scenario}"


# ---------------------------------------------------------------------------
# Texts
# ---------------------------------------------------------------------------

def escape(text: str) -> str:
    """A name from the run as table text: no column break, no mention, no code span."""
    text = re.sub(r"\s+", " ", str(text))
    return text.replace("\\", "\\\\").replace("|", "\\|").replace("`", "'").replace("@", "&#64;")


def run_url(server_url: str, repo: str, run_id: str) -> str:
    run, _, attempt = str(run_id).partition("-")
    url = f"{server_url}/{repo}/actions/runs/{run}"
    return f"{url}/attempts/{attempt}" if attempt else url


def _range_link(f: dict, server_url: str, repo: str) -> str:
    bad = (f.get("first_bad") or {}).get("commit")
    good = (f.get("last_good") or {}).get("commit")
    if not bad:
        return "–"
    if not good:
        return f"[{bad[:12]}]({server_url}/{repo}/commit/{bad}) (no good run before it)"
    return f"[{good[:12]}..{bad[:12]}]({server_url}/{repo}/compare/{good}...{bad})"


def _value(value: float | None, f: dict) -> str:
    if value is None:
        return "–"
    if f["metric"] == "instructions":
        return f"{value:,.0f} instructions"
    if f["metric"] == "budget":
        return pb.format_value(value, f["unit"])
    if abs(value) < 0.001:
        return f"{value * 1e6:.1f} µs"
    return f"{value * 1000:.1f} ms"


def _what(f: dict) -> str:
    if f["kind"] == "regression":
        return f"{f['metric']}: change point, {f['streak']} run(s) since"
    if f["kind"] == "missing":
        return f"{f['metric']}: no longer measured"
    if f.get("status") == "missing":
        return "Budget: not measured"
    return "Budget broken"


def _row(f: dict, server_url: str, repo: str) -> str:
    if f["kind"] == "budget":
        before = f"Budget {pb.format_value(f['budget'], f['unit'])}"
        change = ("–" if f["measured"] is None
                  else f"{(f['measured'] - f['budget']) / f['budget'] * 100:+.1f} %")
    else:
        before = _value(f.get("reference"), f)
        change = "–" if f.get("delta_percent") is None else f"{f['delta_percent']:+.1f} %"
    return (f"| {escape(f['benchmark'])} | {_what(f)} | {_value(f.get('measured'), f)} "
            f"| {before} | {change} | {_range_link(f, server_url, repo)} |")


def _table(found: list[dict], server_url: str, repo: str) -> list[str]:
    return ["| Benchmark | Finding | This run | Before | Change | Commits |",
            "|---|---|---:|---:|---:|---|",
            *[_row(f, server_url, repo) for f in found]]


def _git_logs(found: list[dict]) -> list[str]:
    ranges = []
    for f in found:
        good = (f.get("last_good") or {}).get("commit")
        bad = (f.get("first_bad") or {}).get("commit")
        if good and bad and (good, bad) not in ranges:
            ranges.append((good, bad))
    return [f"git log --oneline {good}..{bad}" for good, bad in ranges]


def render_body(scenario: str, found: list[dict], run: dict, *, server_url: str,
                repo: str) -> str:
    commit = run.get("commit", "")
    logs = _git_logs(found)
    lines = [
        scenario_marker(scenario),
        findings_marker(found),
        "## What happens",
        "",
        f"The nightly Performance run [{escape(run.get('run_id', ''))}]"
        f"({run_url(server_url, repo, run.get('run_id', ''))}) of master at "
        f"[{commit[:12]}]({server_url}/{repo}/commit/{commit}) found in **{escape(scenario)}**:",
        "",
        *_table(found, server_url, repo),
        "",
        "*Commits* runs from the last run without the finding to the first run with it "
        "(perf-data branch, `history/`); the change is in that range"
        + (":" if logs else "."),
        "",
    ]
    if logs:
        lines += ["```bash", *logs, "```", ""]
    lines += [
        "## What to do",
        "",
        "- An instruction count: the pull requests in the range carry an Instruction Counts "
        "comment, which shows the one that added them; BUILD.md, *Instruction counts*, "
        "reproduces a count.",
        "- A Budget: ADR 0018 and `tests/e2e/budgets.json` hold it; changing a Budget means "
        "changing the ADR.",
        "- An intended cost: dispatch the Performance workflow from master with "
        "`accept_new_level` (BUILD.md, *Nightly performance*), then close this issue.",
        "",
        "Each nightly run updates this issue while the finding stands and comments when a new "
        "one joins it. Closed, it stays closed for these findings.",
        "",
        "> *Filed by the Performance workflow (#677).*",
        "",
    ]
    return "\n".join(lines)


def render_comment(found: list[dict], run: dict, *, server_url: str, repo: str) -> str:
    return "\n".join([
        f"The nightly Performance run [{escape(run.get('run_id', ''))}]"
        f"({run_url(server_url, repo, run.get('run_id', ''))}) has findings this issue did "
        "not hold yet; the description now lists them all:",
        "",
        *_table(found, server_url, repo),
        "",
        "> *Filed by the Performance workflow (#677).*",
        "",
    ])


# ---------------------------------------------------------------------------
# The plan
# ---------------------------------------------------------------------------

def _issues_of(scenario: str, issues: list[dict]) -> list[dict]:
    marker = scenario_marker(scenario)
    return sorted((i for i in issues if marker in (i.get("body") or "")),
                  key=lambda i: i["number"], reverse=True)


def plan(comparison: dict, issues: list[dict], *, server_url: str, repo: str) -> list[dict]:
    """The actions on the issue tracker for a run's findings, one scenario at a time."""
    if comparison.get("dest") != "history":
        raise ValueError(f"a run recorded in {comparison.get('dest')!r} (trial) files no issue")
    run = comparison.get("run", {})
    by_scenario: dict[str, list[dict]] = {}
    for f in comparison.get("findings", []):
        by_scenario.setdefault(f["scenario"], []).append(f)

    actions = []
    for scenario, found in sorted(by_scenario.items()):
        fp = fingerprint(found)
        body = render_body(scenario, found, run, server_url=server_url, repo=repo)
        known = _issues_of(scenario, issues)
        open_issue = next((i for i in known if i.get("state", "").upper() == "OPEN"), None)
        if open_issue is not None:
            actions.append({"action": "edit", "scenario": scenario,
                            "number": open_issue["number"], "body": body})
            held_fp, held_ids = _held(open_issue)
            if held_fp != fp:
                new = [f for f in found if finding_id(f) not in held_ids] or found
                actions.append({"action": "comment", "scenario": scenario,
                                "number": open_issue["number"],
                                "body": render_comment(new, run, server_url=server_url,
                                                       repo=repo)})
            continue
        closed = next((i for i in known if _held(i)[0] == fp), None)
        if closed is not None:
            actions.append({"action": "skip", "scenario": scenario, "number": closed["number"],
                            "reason": "closed for these findings"})
            continue
        actions.append({"action": "create", "scenario": scenario, "title": title(scenario),
                        "labels": list(LABELS), "body": body})
    return actions


# ---------------------------------------------------------------------------
# The command
# ---------------------------------------------------------------------------

def run_plan(args: argparse.Namespace) -> int:
    try:
        comparison = json.loads(Path(args.comparison).read_text(encoding="utf-8"))
        issues = json.loads(Path(args.issues).read_text(encoding="utf-8"))
        if not isinstance(issues, list):
            raise ValueError("the issues are not a list")
        actions = plan(comparison, issues, server_url=args.server_url, repo=args.repo)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"::error::{error}")
        return 2
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for index, action in enumerate(actions):
        body = action.pop("body", None)
        if body is not None:
            path = out / f"{index}-{action['action']}.md"
            path.write_text(body, encoding="utf-8")
            action["body_file"] = str(path)
    (out / "plan.json").write_text(json.dumps(actions, indent=2) + "\n", encoding="utf-8")
    for action in actions:
        print(f"{action['action']}: {action['scenario']} {action.get('number', '')}".rstrip())
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("plan", help="decide the issues for a run's findings")
    p.add_argument("--comparison", required=True, help="perf-history.py's --json")
    p.add_argument("--issues", required=True, help="gh issue list ... --json number,state,title,body")
    p.add_argument("--server-url", required=True)
    p.add_argument("--repo", required=True)
    p.add_argument("--out", required=True, help="directory for plan.json and the bodies")
    return run_plan(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
