"""What a release is named and what the CHANGELOG says about it (#221, #307).

Shared by the release scripts in this directory; they run with this directory
on sys.path (as a script, or under pytest).
"""

from __future__ import annotations

import re
from collections.abc import Callable, Iterable

# The suffix of a pre-release name; accepts the historical formats
# 26.05.0-beta1 and 26.03.1-beta.2.
PRERELEASE = r"-(?:alpha|beta|rc)\.?[0-9]+"
_TAG = re.compile(rf"v([0-9]+\.[0-9]+\.[0-9]+)({PRERELEASE})?")
# A release section's heading: `# v26.10.0-beta1 (2026-09-17)`, or the older
# `# 26.04.2 (2026-04-19):`.
_RELEASE_HEADING = re.compile(rf"# v?([0-9]+\.[0-9]+\.[0-9]+(?:{PRERELEASE})?)(?:[\s:(].*)?")


class ReleaseError(Exception):
    pass


_VERSION = re.compile(r"[0-9]+(?:\.[0-9]+){2,3}")


def version_key(version: str) -> tuple[int, ...]:
    """A version X.Y.Z or build X.Y.Z.BUILD as numbers, so 26.7.0 sorts below
    26.10.0 (#385)."""
    if not _VERSION.fullmatch(version):
        raise ReleaseError(f"{version!r} is not a version X.Y.Z or a build X.Y.Z.BUILD.")
    return tuple(int(part) for part in version.split("."))


def parse_tag(tag: str) -> tuple[str, bool]:
    """Returns (base version, is prerelease) of a release tag."""
    m = _TAG.fullmatch(tag)
    if not m:
        raise ReleaseError(f"Tag {tag!r} is not a release tag "
                           "(expected vX.Y.Z or vX.Y.Z-alphaN/betaN/rcN).")
    return m.group(1), m.group(2) is not None


def release_notes(changelog: str, *, tag: str) -> str:
    """The CHANGELOG section of a release tag, without its heading (#307).

    The section starts at the level-one heading naming the tag's version
    (`# v26.10.0-beta1 (2026-09-17)`, or the older `# 26.04.2 (2026-04-19):`)
    and ends at the next level-one heading; the `---` separator before that
    heading is not part of it.
    """
    parse_tag(tag)
    heading = re.compile(rf"# v?{re.escape(tag[1:])}(?=[\s:(]|$).*")
    for line, body in sections(changelog):
        if heading.fullmatch(line):
            return body + "\n"
    raise ReleaseError(f"CHANGELOG.md has no section for {tag}: add a heading "
                       f"'# {tag} (YYYY-MM-DD)' before tagging.")


def sections(changelog: str) -> list[tuple[str, str]]:
    """The level-one sections of a CHANGELOG as (heading line, body), in order;
    a body keeps its lines without the `---` separator before the next heading."""
    found: list[tuple[str, list[str]]] = []
    for line in changelog.splitlines():
        if line.startswith("# "):
            found.append((line, []))
        elif found:
            found[-1][1].append(line)
    result = []
    for heading, body in found:
        while body and body[-1].strip() in ("", "---"):
            body.pop()
        while body and not body[0].strip():
            body.pop(0)
        result.append((heading, "\n".join(body)))
    return result


def release_heading(line: str) -> str | None:
    """The release name a CHANGELOG heading line names, or None when it names none."""
    match = _RELEASE_HEADING.fullmatch(line)
    return match.group(1) if match else None


def is_release_of(version: str) -> Callable[[str], bool]:
    """Whether a release name is version X.Y.Z itself or a pre-release of it."""
    pattern = re.compile(rf"{re.escape(version)}(?:{PRERELEASE})?")
    return lambda name: isinstance(name, str) and pattern.fullmatch(name) is not None


def feed_changelog_versions(feed) -> set[str]:
    """The release names the update feed's changelog describes."""
    entries = feed.get("changelog", []) if isinstance(feed, dict) else []
    entries = entries if isinstance(entries, list) else []
    return {e["version"] for e in entries
            if isinstance(e, dict) and isinstance(e.get("version"), str) and e["version"]}


def report(problems: Iterable[str]) -> int:
    """Prints one ::error:: annotation per problem; the exit code of a check."""
    found = False
    for problem in problems:
        # One line each: an annotation ends at the first newline.
        print(f"::error::{' '.join(problem.split())}")
        found = True
    return 1 if found else 0
