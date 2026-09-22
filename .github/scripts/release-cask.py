#!/usr/bin/env python3
"""Sets the Homebrew cask to a published stable release (#378).

The cask is Casks/logsquirl.rb in 64x-lunicorn/homebrew-tap. The app does not
update itself, so `brew upgrade` is how cask users get a release, and a cask
left behind leaves their installs behind. BUILD.md ("Homebrew tap") documents
the tap.

  update --cask F --tag T --checksums C
      Sets version and sha256 in cask F to stable release T and the
      logsquirl-mac-arm64.dmg line of T's checksum file C. Leaves F untouched
      when it already has that version and hash, or a newer version: a re-run
      of an older release never downgrades the cask.

A refused update exits 1 with an ::error:: annotation.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from releases import ReleaseError, parse_tag, report, version_key

DMG = "logsquirl-mac-arm64.dmg"
# A `sha256sum` line: the hash, then " *" (binary mode) or "  " and the name.
_CHECKSUM_LINE = re.compile(r"(\S+) [ *](.+)")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_VERSION_LINE = re.compile(r'^(\s*version ")([^"]*)(")$', re.MULTILINE)
_SHA256_LINE = re.compile(r'^(\s*sha256 ")([^"]*)(")$', re.MULTILINE)


def dmg_hash(checksums: str) -> str:
    """The SHA-256 the checksum file lists for the macOS DMG."""
    found = []
    for line in checksums.splitlines():
        match = _CHECKSUM_LINE.fullmatch(line.strip())
        if match and match.group(2) == DMG:
            found.append(match.group(1))
    if not found:
        raise ReleaseError(f"The checksum file has no {DMG} line.")
    if len(found) > 1:
        raise ReleaseError(f"The checksum file lists {DMG} more than once.")
    if not _SHA256.fullmatch(found[0]):
        raise ReleaseError(f"The checksum file's {DMG} entry {found[0]!r} is not a SHA-256.")
    return found[0]


def _only(pattern: re.Pattern, cask: str, what: str) -> re.Match:
    matches = list(pattern.finditer(cask))
    if len(matches) != 1:
        raise ReleaseError(f"The cask must have exactly one {what} line, it has {len(matches)}.")
    return matches[0]


def update(cask: str, *, tag: str, sha256: str) -> str:
    """cask set to stable release tag and its DMG hash; unchanged when it
    already has that release and hash, or a newer release."""
    version, prerelease = parse_tag(tag)
    if prerelease:
        raise ReleaseError(f"{tag} is a pre-release: the cask only follows stable releases.")
    current = _only(_VERSION_LINE, cask, "version")
    _only(_SHA256_LINE, cask, "sha256")
    if version_key(current.group(2)) > version_key(version):
        return cask
    cask = _VERSION_LINE.sub(lambda m: m.group(1) + version + m.group(3), cask)
    return _SHA256_LINE.sub(lambda m: m.group(1) + sha256 + m.group(3), cask)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    upd = sub.add_parser("update")
    upd.add_argument("--cask", type=Path, required=True)
    upd.add_argument("--tag", required=True)
    upd.add_argument("--checksums", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        cask = args.cask.read_text(encoding="utf-8")
        sha256 = dmg_hash(args.checksums.read_text(encoding="utf-8"))
        updated = update(cask, tag=args.tag, sha256=sha256)
        if updated == cask:
            print(f"The cask already has {args.tag[1:]} or a newer release; nothing to change.")
        else:
            args.cask.write_text(updated, encoding="utf-8")
            print(f"Set the cask to {args.tag[1:]} ({DMG} {sha256}).")
        found = []
    except (ReleaseError, OSError) as err:
        found = [str(err)]
    return report(found)


if __name__ == "__main__":
    sys.exit(main())
