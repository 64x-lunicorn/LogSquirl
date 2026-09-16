#!/usr/bin/env python3
"""Keeps every pinned download's SHA-256 in step with its version (#211).

Renovate bumps the version under a `# renovate:` comment, but its hosted app
cannot download the new artifact and hash it. Every such pin in this
repository is written as a block the script can read:

    # renovate: datasource=... depName=<dep> [...]
    <NAME>_VERSION: <version>          (YAML, NAME=..., NAME="...", ENV NAME=...)
    <NAME>_SHA256[_<SUFFIX>]: <hash>   (one or more lines, directly below)

URLS below maps each depName and hash variable to the download URL the file
itself uses, so a new pair needs a rule here too; `--list` fails without it.

Usage (from the repository root):
  update-checksums.py           download every pair and rewrite stale hashes
  update-checksums.py --check   download and verify, rewrite nothing (exit 1 on mismatch)
  update-checksums.py --list    parse only, no network: every pair has a URL rule and
                                every SHA-256 assignment belongs to a pair
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import time
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

# The files Renovate's `# renovate:` manager reads (renovate.json5); keep both
# lists in step.
SCAN_GLOBS = (
    ".github/workflows/*.yml",
    ".github/actions/*/action.yml",
    "docker/*/Dockerfile",
    "docker/shared/*.sh",
    "packaging/**/*.sh",
)

_GH = "https://github.com"
_NINJA = _GH + "/ninja-build/ninja/releases/download/v{version}"
# depName -> hash variable -> download URL, mirroring the URL the file itself
# downloads. {version} is the pinned version as written; {version_underscore}
# replaces its dots (Boost's file names).
URLS: dict[str, dict[str, str]] = {
    "openssl/openssl": {
        "OPENSSL_SHA256": "https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-{version}.zip",
    },
    "ninja-build/ninja": {
        "NINJA_SHA256_WINDOWS": _NINJA + "/ninja-win.zip",
        "NINJA_SHA256_MACOS": _NINJA + "/ninja-mac.zip",
        "NINJA_SHA256_LINUX": _NINJA + "/ninja-linux.zip",
        "NINJA_SHA256": _NINJA + "/ninja-linux.zip",
    },
    "boostorg/boost": {
        "BOOST_SHA256": "https://archives.boost.io/release/{version}/source/boost_{version_underscore}.tar.bz2",
    },
    "nsis": {
        "NSIS_SHA256": "https://downloads.sourceforge.net/project/nsis/NSIS%203/{version}/nsis-{version}.zip",
    },
    "create-dmg/create-dmg": {
        "CREATE_DMG_SHA256": _GH + "/create-dmg/create-dmg/archive/refs/tags/v{version}.tar.gz",
    },
    "anchore/grype": {
        "GRYPE_SHA256": _GH + "/anchore/grype/releases/download/v{version}/grype_{version}_linux_amd64.tar.gz",
    },
    "getsentry/sentry-cli": {
        "SENTRY_CLI_SHA256": _GH + "/getsentry/sentry-cli/releases/download/{version}/sentry-cli-Linux-x86_64",
    },
    "linuxdeploy/linuxdeploy": {
        "LINUXDEPLOY_SHA256": _GH + "/linuxdeploy/linuxdeploy/releases/download/{version}/linuxdeploy-x86_64.AppImage",
    },
    "linuxdeploy/linuxdeploy-plugin-qt": {
        "LINUXDEPLOY_PLUGIN_QT_SHA256":
            _GH + "/linuxdeploy/linuxdeploy-plugin-qt/releases/download/{version}/linuxdeploy-plugin-qt-x86_64.AppImage",
    },
    "mozilla/sccache": {
        "SCCACHE_SHA256":
            _GH + "/mozilla/sccache/releases/download/v{version}/sccache-v{version}-x86_64-unknown-linux-musl.tar.gz",
    },
    "Kitware/CMake": {
        "CMAKE_SHA256": _GH + "/Kitware/CMake/releases/download/v{version}/cmake-{version}-linux-x86_64.sh",
    },
    "adrian-thurston/ragel": {
        "RAGEL_SHA256": "https://www.colm.net/files/ragel/ragel-{version}.tar.gz",
    },
}

_COMMENT = re.compile(r"^\s*#\s*renovate:\s*(?P<fields>.*?)\s*$")
_ASSIGNMENT = re.compile(
    r"""^\s*(?:ENV\s+)?(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*|=)(?P<q>["']?)(?P<value>[^"'\s]+)(?P=q)\s*$""")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class HashLine:
    name: str
    value: str
    line: int  # 0-based line index in the file


@dataclass(frozen=True)
class Pair:
    path: str
    dep_name: str
    version: str
    line: int  # 0-based line index of the version
    hashes: tuple[HashLine, ...]


class UnknownPair(Exception):
    pass


def _hash_assignment(line: str) -> re.Match[str] | None:
    m = _ASSIGNMENT.match(line)
    return m if m and "SHA256" in m["name"] and _SHA256.match(m["value"]) else None


def parse_pairs(text: str, path: str) -> list[Pair]:
    """The version/hash pairs in one file; a comment without hash lines is none."""
    lines = text.splitlines()
    pairs = []
    for i, line in enumerate(lines):
        comment = _COMMENT.match(line)
        if not comment or i + 1 >= len(lines):
            continue
        fields = dict(f.split("=", 1) for f in comment["fields"].split() if "=" in f)
        version = _ASSIGNMENT.match(lines[i + 1])
        if not version or "depName" not in fields:
            continue
        hashes = []
        for j in range(i + 2, len(lines)):
            m = _hash_assignment(lines[j])
            if not m:
                break
            hashes.append(HashLine(m["name"], m["value"], j))
        if hashes:
            pairs.append(Pair(path, fields["depName"], version["value"], i + 1, tuple(hashes)))
    return pairs


def download_urls(pair: Pair) -> dict[str, str]:
    rules = URLS.get(pair.dep_name, {})
    urls = {}
    for h in pair.hashes:
        if h.name not in rules:
            raise UnknownPair(f"{pair.path}:{h.line + 1}: no download URL for {pair.dep_name} {h.name}")
        urls[h.name] = rules[h.name].format(
            version=pair.version, version_underscore=pair.version.replace(".", "_"))
    return urls


def fetch_url(url: str) -> bytes:
    # SourceForge answers urllib's default agent with an HTML page instead of
    # the mirror redirect curl gets.
    request = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                return response.read()
        except OSError:
            if attempt == 3:
                raise
            time.sleep(5 * (attempt + 1))
    raise AssertionError("unreachable")


def _scan(root: Path) -> tuple[list[Pair], list[str]]:
    """All pairs under root, and an error for each SHA-256 outside a pair."""
    pairs: list[Pair] = []
    errors: list[str] = []
    files = sorted({p for pattern in SCAN_GLOBS for p in root.glob(pattern) if p.is_file()})
    for file in files:
        rel = file.relative_to(root).as_posix()
        text = file.read_text()
        found = parse_pairs(text, rel)
        pairs.extend(found)
        paired = {h.line for p in found for h in p.hashes}
        for i, line in enumerate(text.splitlines()):
            m = _hash_assignment(line)
            if m and i not in paired:
                errors.append(f"{rel}:{i + 1}: {m['name']} has no # renovate: comment directly above its version")
    return pairs, errors


# The Renovate Checksums workflow pushes the refreshed hashes with GITHUB_TOKEN,
# and GitHub refuses any push from it that touches .github/workflows/ (that
# needs the `workflows` permission, which GITHUB_TOKEN never has). A pair there
# would fail every Renovate PR for it, so pins live in composite actions (#211).
WORKFLOWS_DIR = ".github/workflows/"


def workflow_pair_errors(pairs: list[Pair]) -> list[str]:
    return [f"{p.path}:{p.line + 1}: {p.dep_name} pins a version and SHA-256 in a workflow file, where the "
            "Renovate Checksums workflow cannot push (GITHUB_TOKEN may not change .github/workflows/); "
            "move the download into a composite action under .github/actions/"
            for p in pairs if p.path.startswith(WORKFLOWS_DIR)]


def main(argv: list[str] | None = None, fetch: Callable[[str], bytes] = fetch_url) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--list", action="store_true", help="parse only, no network")
    mode.add_argument("--check", action="store_true", help="verify every hash, rewrite nothing")
    parser.add_argument("--repo-root", default=".", type=Path)
    args = parser.parse_args(argv)

    pairs, errors = _scan(args.repo_root)
    errors += workflow_pair_errors(pairs)
    urls: dict[tuple[str, int], str] = {}
    for pair in pairs:
        try:
            for name, url in download_urls(pair).items():
                line = next(h.line for h in pair.hashes if h.name == name)
                urls[(pair.path, line)] = url
        except UnknownPair as e:
            errors.append(str(e))
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    if args.list:
        for pair in pairs:
            for h in pair.hashes:
                print(f"{pair.path}:{pair.line + 1} {pair.dep_name} {pair.version} {h.name} {urls[(pair.path, h.line)]}")
        return 0

    status = 0
    digests: dict[str, str] = {}  # one download per URL (CMake is in three images)
    for pair in pairs:
        file = args.repo_root / pair.path
        lines = file.read_text().splitlines(keepends=True)
        changed = False
        for h in pair.hashes:
            url = urls[(pair.path, h.line)]
            if url not in digests:
                digests[url] = hashlib.sha256(fetch(url)).hexdigest()
            actual = digests[url]
            if actual == h.value:
                print(f"ok {pair.path}:{h.line + 1} {h.name}")
            elif args.check:
                print(f"error: {pair.path}:{h.line + 1}: {h.name} mismatch: file has {h.value}, "
                      f"{url} is {actual}", file=sys.stderr)
                status = 1
            else:
                lines[h.line] = lines[h.line].replace(h.value, actual)
                changed = True
                print(f"updated {pair.path}:{h.line + 1} {h.name} {h.value} -> {actual}")
        if changed:
            file.write_text("".join(lines))
    return status


if __name__ == "__main__":
    sys.exit(main())
