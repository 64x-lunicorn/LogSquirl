#!/usr/bin/env python3
"""Fails when a workflow would run an action the repository allowlist rejects (#201).

The repository allows GitHub-owned actions plus the patterns in
repo-settings.sh ALLOWED_ACTIONS. That check applies to every action a job
runs, including the ones a composite action calls internally, and a pattern
like `owner/repo@*` does not cover `owner/repo/sub@...`. Both have broken CI
after the allowlist was applied (install-qt-action runs "$/action",
trivy-action runs aquasecurity/setup-trivy), and neither is visible in our own
workflow files. So this walks every non-local `uses:` in the workflows and
composite actions, fetches the action.yml of each referenced action at its
pinned ref, follows the actions it uses in turn, and matches each against the
allowlist the way GitHub does.

Usage: .github/scripts/check-action-allowlist.py   (from the repository root)
Needs `gh` authenticated (GH_TOKEN in CI; contents: read suffices).
"""

from __future__ import annotations

import fnmatch
import glob
import re
import subprocess
import sys

MAX_DEPTH = 5
GITHUB_OWNED = ("actions", "github")

USES = re.compile(r"""^\s*(?:-\s+)?uses:\s*['"]?([^\s'"#]+)""")


def uses_in(text: str) -> list[str]:
    return [m.group(1) for line in text.splitlines() if (m := USES.match(line))]


def allowed_patterns() -> list[str]:
    # repo-settings.sh is the single source of the allowlist.
    out = subprocess.run(
        [".github/scripts/repo-settings.sh", "list-allowed"],
        check=True, capture_output=True, text=True,
    ).stdout
    return [p for p in out.splitlines() if p]


def fetch(owner: str, repo: str, path: str, ref: str) -> str | None:
    """Returns the file content, or None if the file does not exist."""
    result = subprocess.run(
        ["gh", "api", "-H", "Accept: application/vnd.github.raw+json",
         f"repos/{owner}/{repo}/contents/{path}?ref={ref}"],
        capture_output=True, text=True,
    )
    if result.returncode == 0:
        return result.stdout
    if "HTTP 404" in result.stderr:
        return None
    sys.exit(f"::error::gh api failed for {owner}/{repo}/{path}@{ref}: {result.stderr.strip()}")


def definition(owner: str, repo: str, sub: str, ref: str) -> str | None:
    """The action.yml (or reusable workflow) behind a reference, if any.

    Docker and JavaScript actions without an action.yml at that path cannot
    call further actions, so a miss is not an error.
    """
    if sub.endswith((".yml", ".yaml")):  # reusable workflow
        return fetch(owner, repo, sub, ref)
    base = f"{sub}/" if sub else ""
    for name in ("action.yml", "action.yaml"):
        text = fetch(owner, repo, base + name, ref)
        if text is not None:
            return text
    return None


def resolve(uses: str, parent: tuple[str, str, str] | None) -> tuple[str, str, str, str] | None:
    """Splits a reference into (owner, repo, sub, ref); None for what the
    allowlist does not govern (docker:// images, the caller's own actions)."""
    if uses.startswith("docker://"):
        return None
    if uses.startswith(("./", "$/")):
        if parent is None:
            return None  # a local action of this repository
        # Inside a remote action, "$/sub" (install-qt-action) and "./sub"
        # name a directory of that same action repository at the same ref.
        owner, repo, ref = parent
        return owner, repo, uses[2:].strip("/"), ref
    if "@" not in uses:
        sys.exit(f"::error::cannot parse action reference '{uses}'")
    name, ref = uses.rsplit("@", 1)
    parts = name.split("/")
    if len(parts) < 2:
        sys.exit(f"::error::cannot parse action reference '{uses}'")
    return parts[0], parts[1], "/".join(parts[2:]), ref


def is_allowed(owner: str, repo: str, sub: str, ref: str, patterns: list[str]) -> bool:
    if owner.lower() in GITHUB_OWNED:
        return True
    # GitHub matches a pattern against the whole "owner/repo[/sub]@ref"; `*`
    # spans any characters, so `owner/*` covers subdirectories but
    # `owner/repo@*` does not. Owner and repository names are case-insensitive.
    target = f"{owner}/{repo}{'/' + sub if sub else ''}@{ref}".lower()
    return any(fnmatch.fnmatchcase(target, p.lower()) for p in patterns)


def main() -> int:
    patterns = allowed_patterns()
    queue: list[tuple[str, tuple[str, str, str] | None, list[str], int]] = []
    for path in sorted(glob.glob(".github/workflows/*.y*ml") + glob.glob(".github/actions/*/action.y*ml")):
        with open(path, encoding="utf-8") as f:
            for uses in uses_in(f.read()):
                queue.append((uses, None, [path], 0))

    seen: set[tuple[str, str, str, str]] = set()
    missing: dict[str, list[str]] = {}
    while queue:
        uses, parent, chain, depth = queue.pop(0)
        resolved = resolve(uses, parent)
        if resolved is None or resolved in seen:
            continue
        seen.add(resolved)
        owner, repo, sub, ref = resolved
        label = f"{owner}/{repo}{'/' + sub if sub else ''}@{ref}"
        here = chain + [label]
        if not is_allowed(owner, repo, sub, ref, patterns):
            suggestion = f"{owner}/{repo}{'/' + sub if sub else ''}@*"
            missing.setdefault(suggestion, []).append(" -> ".join(here))
        if depth >= MAX_DEPTH:
            print(f"::warning::not following {label}: nesting deeper than {MAX_DEPTH}")
            continue
        text = definition(owner, repo, sub, ref)
        if text is not None:
            queue.extend((u, (owner, repo, ref), here, depth + 1) for u in uses_in(text))

    for suggestion, chains in sorted(missing.items()):
        print(f"::error::'{suggestion}' is missing from ALLOWED_ACTIONS in "
              f".github/scripts/repo-settings.sh; used via {chains[0]}")
    if missing:
        print("Add the patterns, then run `.github/scripts/repo-settings.sh apply` "
              "before merging; otherwise the jobs fail with \"action is not allowed\".")
        return 1
    print(f"All {len(seen)} actions, including nested ones, are allowed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
