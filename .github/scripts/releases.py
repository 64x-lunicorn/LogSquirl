"""What a release is named and what the CHANGELOG says about it (#221, #307).

Shared by the release scripts in this directory; they run with this directory
on sys.path (as a script, or under pytest).
"""

from __future__ import annotations

import re

# Accepts the historical formats: v26.04.2, v26.05.0-beta1, v26.03.1-beta.2
_TAG = re.compile(r"v([0-9]+\.[0-9]+\.[0-9]+)(-(?:alpha|beta|rc)\.?[0-9]+)?")


class ReleaseError(Exception):
    pass


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
    lines = changelog.splitlines()
    start = next((i for i, line in enumerate(lines) if heading.fullmatch(line)), None)
    if start is None:
        raise ReleaseError(f"CHANGELOG.md has no section for {tag}: add a heading "
                           f"'# {tag} (YYYY-MM-DD)' before tagging.")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("# ")), len(lines))
    section = lines[start + 1:end]
    while section and section[-1].strip() in ("", "---"):
        section.pop()
    while section and not section[0].strip():
        section.pop(0)
    return "\n".join(section) + "\n"
