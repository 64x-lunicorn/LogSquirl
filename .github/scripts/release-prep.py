#!/usr/bin/env python3
"""Checks that a pull request keeps the CHANGELOG and a release preparation
complete (#312, #314).

  changelog-entry --base B --head H --author A --labels JSON
      The pull request adds to the `# Unreleased` section of the CHANGELOG
      (base B, head H), or starts a release section. The `no-changelog` label
      and the dependency bots need no entry.

A failed check exits 1 with an ::error:: annotation per problem.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from releases import sections

UNRELEASED = "# Unreleased"
NO_CHANGELOG_LABEL = "no-changelog"
DEPENDENCY_BOTS = ("dependabot[bot]", "renovate[bot]")


def changelog_entry_problem(*, base: str, head: str, labels: list[str], author: str) -> str | None:
    """Why a pull request lacks its CHANGELOG entry, or None when it has one or needs none."""
    if NO_CHANGELOG_LABEL in labels or author in DEPENDENCY_BOTS:
        return None
    base_sections = dict(sections(base))
    head_sections = sections(head)
    unreleased = dict(head_sections).get(UNRELEASED, "")
    if unreleased and unreleased != base_sections.get(UNRELEASED, ""):
        return None
    # A release preparation turns the Unreleased section into a release section.
    if head_sections and head_sections[0][0] != UNRELEASED and head_sections[0][0] not in base_sections:
        return None
    return (f"CHANGELOG.md: add an entry under '{UNRELEASED}' for this change, "
            f"or add the '{NO_CHANGELOG_LABEL}' label if it needs none.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    entry = sub.add_parser("changelog-entry")
    entry.add_argument("--base", type=Path, required=True)
    entry.add_argument("--head", type=Path, required=True)
    entry.add_argument("--author", required=True)
    entry.add_argument("--labels", default="[]", help="JSON list of the pull request's label names")
    args = parser.parse_args(argv)

    try:
        problem = changelog_entry_problem(
            base=args.base.read_text(encoding="utf-8") if args.base.exists() else "",
            head=args.head.read_text(encoding="utf-8"),
            labels=json.loads(args.labels), author=args.author)
        found = [problem] if problem else []
    except (OSError, ValueError) as err:
        found = [str(err)]
    for message in found:
        print(f"::error::{' '.join(message.split())}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
