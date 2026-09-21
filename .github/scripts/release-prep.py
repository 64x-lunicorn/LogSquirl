#!/usr/bin/env python3
"""Checks that a pull request keeps the CHANGELOG and a release preparation
complete (#312, #314).

  changelog-entry --base B --head H --author A --labels JSON
      The pull request adds to the `# Unreleased` section of the CHANGELOG
      (base B, head H), or starts a release section. The `no-changelog` label
      and the dependency bots need no entry, nor does a release's update feed
      pull request from a feed/ branch (--branch).
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
from collections.abc import Callable
from pathlib import Path

from typing import NamedTuple

from releases import (feed_changelog_versions, is_release_of, release_heading, report,
                      sections)

UNRELEASED = "# Unreleased"
NO_CHANGELOG_LABEL = "no-changelog"
DEPENDENCY_BOTS = ("dependabot[bot]", "renovate[bot]")
# CI Release pushes a release's update feed change to feed/<tag> for the
# maintainer to open as a pull request; the release's entry is already in
# the CHANGELOG.
FEED_BRANCH_PREFIX = "feed/"


def changelog_entry_problem(*, base: str, head: str, labels: list[str], author: str,
                            branch: str = "") -> str | None:
    """Why a pull request lacks its CHANGELOG entry, or None when it has one or needs none."""
    if (NO_CHANGELOG_LABEL in labels or author in DEPENDENCY_BOTS
            or branch.startswith(FEED_BRANCH_PREFIX)):
        return None
    base_sections = dict(sections(base))
    head_sections = sections(head)
    unreleased = dict(head_sections).get(UNRELEASED, "")
    if unreleased and unreleased != base_sections.get(UNRELEASED, ""):
        return None
    # A release preparation turns the Unreleased section into a release section.
    if (head_sections and release_heading(head_sections[0][0])
            and head_sections[0][0] not in base_sections):
        return None
    # Between a release preparation and the release the top section is that
    # release, and there is no Unreleased section to add to: this pull request
    # starts a new one above it (#338).
    top = head_sections[0][0] if head_sections else ""
    if release_heading(top):
        return (f"CHANGELOG.md: the top section is the prepared release '{top}', so this change "
                f"goes under a new '{UNRELEASED}' section above it, "
                f"or add the '{NO_CHANGELOG_LABEL}' label if it needs none.")
    return (f"CHANGELOG.md: add an entry under '{UNRELEASED}' for this change, "
            f"or add the '{NO_CHANGELOG_LABEL}' label if it needs none.")


_PROJECT_VERSION = re.compile(r"^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", re.MULTILINE)
_PAGE_VERSION = re.compile(r"^release:\n  version: (\S+)$", re.MULTILINE)


def _project_version(cmake: str) -> str | None:
    match = _PROJECT_VERSION.search(cmake)
    return match.group(1) if match else None


class _Expected(NamedTuple):
    """The release a preparation must name everywhere."""
    name: str          # in messages
    or_prerelease: str  # " or a pre-release of it" while the CHANGELOG names none
    matches: Callable[[str], bool]


def _expected(version: str, release: str | None) -> _Expected:
    if release:
        return _Expected(release, "", lambda name: name == release)
    return _Expected(version, " or a pre-release of it", is_release_of(version))


def _version_order(version: str) -> tuple[int, ...]:
    """A project version as numbers, so 26.7.0 sorts below 26.10.0."""
    return tuple(int(part) for part in version.split("."))


def release_preparation_problems(*, base_cmake: str, head_cmake: str, changelog: str, feed,
                                 news_dir: Path) -> list[str]:
    """What a release preparation lacks; empty when complete, when the pull
    request does not change the project version, or when it lowers it."""
    version = _project_version(head_cmake)
    base_version = _project_version(base_cmake)
    if version is None or version == base_version:
        return []
    # A preparation raises the version. A pull request that lowers it takes a
    # preparation back -- the release it named was never published -- so there
    # is no release to find named anywhere, and the CHANGELOG section that
    # named it is gone on purpose. Without this, the two halves of the check
    # contradict each other: the version has to go back for the repository's
    # own consistency test to pass, and going back is what this check would
    # read as an incomplete preparation (#338).
    if base_version is not None and _version_order(version) < _version_order(base_version):
        return []
    found = []
    all_sections = sections(changelog)
    top = all_sections[0][0] if all_sections else "(nothing)"
    named = release_heading(top)
    release = named if named and is_release_of(version)(named) else None
    if not release:
        found.append(f"CHANGELOG.md: the version is now {version}, but the top section is '{top}'; "
                     f"turn it into '# v{version} (YYYY-MM-DD)' or a pre-release such as "
                     f"'# v{version}-beta1 (YYYY-MM-DD)'")
    expected = _expected(version, release)
    if not any(expected.matches(name) for name in feed_changelog_versions(feed)):
        found.append(f"latest.json: no changelog entry for {expected.name}{expected.or_prerelease}")

    pages = [m.group(1) for page in sorted(news_dir.glob("release-*.md"))
             if (m := _PAGE_VERSION.search(page.read_text(encoding="utf-8")))]
    if not any(expected.matches(name) for name in pages):
        found.append(f"website: no release page with 'version: {expected.name}'{expected.or_prerelease}")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    entry = sub.add_parser("changelog-entry")
    entry.add_argument("--base", type=Path, required=True)
    entry.add_argument("--head", type=Path, required=True)
    entry.add_argument("--author", required=True)
    entry.add_argument("--labels", default="[]", help="JSON list of the pull request's label names")
    entry.add_argument("--branch", default="", help="the pull request's head branch")
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
                labels=json.loads(args.labels), author=args.author, branch=args.branch)
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
    return report(found)


if __name__ == "__main__":
    sys.exit(main())
