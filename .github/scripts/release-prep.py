#!/usr/bin/env python3
"""Checks that a pull request keeps the CHANGELOG and a release preparation
complete (#312, #314).

  changelog-entry --base B --head H --author A --labels JSON
      The pull request adds to the `# Unreleased` section of the CHANGELOG
      (base B, head H), or starts a release section. The `no-changelog` label
      and the dependency bots need no entry.
  release-preparation --base-cmakelists B [--cmakelists C] [--changelog L]
                      [--feed F] [--news-dir D]
      When the project version in C differs from base B, the preparation
      names that release everywhere a release needs it: the top CHANGELOG
      section, a changelog entry in the update feed, and a website release
      page.

A failed check exits 1 with an ::error:: annotation per problem.
"""

from __future__ import annotations

import argparse
import json
import re
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


_PROJECT_VERSION = re.compile(r"^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", re.MULTILINE)
_PAGE_VERSION = re.compile(r"^release:\n  version: (\S+)$", re.MULTILINE)


def _project_version(cmake: str) -> str | None:
    match = _PROJECT_VERSION.search(cmake)
    return match.group(1) if match else None


def _expected(version: str, release: str | None) -> tuple[str, str, callable]:
    """The expected release name, the words saying a pre-release matches too,
    and which names match."""
    if release:
        return release, "", lambda name: name == release
    prerelease = re.compile(rf"{re.escape(version)}(-(alpha|beta|rc)\.?[0-9]+)?")
    return version, " or a pre-release of it", lambda name: bool(prerelease.fullmatch(str(name)))


def release_preparation_problems(*, base_cmake: str, head_cmake: str, changelog: str, feed,
                                 news_dir: Path) -> list[str]:
    """What a release preparation lacks; empty when complete or when the
    pull request does not change the project version."""
    version = _project_version(head_cmake)
    if version is None or version == _project_version(base_cmake):
        return []
    found = []
    all_sections = sections(changelog)
    top = all_sections[0][0] if all_sections else "(nothing)"
    heading = re.fullmatch(rf"# v({re.escape(version)}(-(alpha|beta|rc)\.?[0-9]+)?)( \(.*\))?", top)
    release = heading.group(1) if heading else None
    if not release:
        found.append(f"CHANGELOG.md: the version is now {version}, but the top section is '{top}'; "
                     f"turn it into '# v{version} (YYYY-MM-DD)' or a pre-release such as "
                     f"'# v{version}-beta1 (YYYY-MM-DD)'")
    expected, or_prerelease, matches = _expected(version, release)

    entries = feed.get("changelog", []) if isinstance(feed, dict) else []
    if not any(isinstance(e, dict) and matches(e.get("version")) for e in entries):
        found.append(f"latest.json: no changelog entry for {expected}{or_prerelease}")

    pages = [m.group(1) for page in sorted(news_dir.glob("release-*.md"))
             if (m := _PAGE_VERSION.search(page.read_text(encoding="utf-8")))]
    if not any(matches(name) for name in pages):
        found.append(f"website: no release page with 'version: {expected}'{or_prerelease}")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    entry = sub.add_parser("changelog-entry")
    entry.add_argument("--base", type=Path, required=True)
    entry.add_argument("--head", type=Path, required=True)
    entry.add_argument("--author", required=True)
    entry.add_argument("--labels", default="[]", help="JSON list of the pull request's label names")
    prep = sub.add_parser("release-preparation")
    prep.add_argument("--base-cmakelists", type=Path, required=True)
    prep.add_argument("--cmakelists", type=Path, default=Path("CMakeLists.txt"))
    prep.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    prep.add_argument("--feed", type=Path, default=Path("latest.json"))
    prep.add_argument("--news-dir", type=Path, default=Path("website/src/content/docs/news"))
    args = parser.parse_args(argv)

    try:
        if args.command == "changelog-entry":
            problem = changelog_entry_problem(
                base=args.base.read_text(encoding="utf-8") if args.base.exists() else "",
                head=args.head.read_text(encoding="utf-8"),
                labels=json.loads(args.labels), author=args.author)
            found = [problem] if problem else []
        else:
            found = release_preparation_problems(
                base_cmake=args.base_cmakelists.read_text(encoding="utf-8"),
                head_cmake=args.cmakelists.read_text(encoding="utf-8"),
                changelog=args.changelog.read_text(encoding="utf-8"),
                feed=json.loads(args.feed.read_text(encoding="utf-8")),
                news_dir=args.news_dir)
    except (OSError, ValueError) as err:
        found = [str(err)]
    for message in found:
        print(f"::error::{' '.join(message.split())}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
