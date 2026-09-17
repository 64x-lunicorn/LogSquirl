#!/usr/bin/env python3
"""Checks the update feed and records a published release in it (#306, #310).

latest.json on master is the update feed LogSquirl reads at start-up, so a
merge reaches every installation at once. BUILD.md ("Release Process")
documents its fields.

  check     --feed F
      F is a valid feed.
  check-tag --feed F --tag T
      F has a changelog entry for T's release name; CI Release checks this
      before anything is signed, since record needs it.
  record    --feed F --tag T --build B
      Records release T, published from build B (YY.MM.PATCH.BUILD), in F's
      stable or beta fields and its release list, unless F already announces
      a newer build on that channel. The result must be a valid feed.

A rejected feed exits 1 with an ::error:: annotation per problem.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from releases import ReleaseError, parse_tag

RELEASE_PAGE = "https://github.com/64x-lunicorn/LogSquirl/releases/tag/v"
_BUILD = re.compile(r"([0-9]+\.[0-9]+\.[0-9]+)\.([0-9]+)")


def _base(release_name: str) -> str:
    return release_name.split("-", 1)[0]


def _build_key(build: str) -> tuple[int, ...]:
    return tuple(int(part) for part in build.split("."))


def _is_text(value) -> bool:
    return isinstance(value, str) and value != ""


def problems(feed) -> list[str]:
    """Everything that makes feed invalid, in field order; empty when it is valid."""
    if not isinstance(feed, dict):
        return ["the feed is not a JSON object"]
    found = []
    for field in ("stable", "stable_url", "stable_build", "beta", "beta_url", "beta_build",
                  "ci", "ci_url"):
        if field not in feed:
            found.append(f"'{field}' is missing")
        elif not _is_text(feed[field]):
            found.append(f"'{field}' is not a text")
    for field in ("releases", "changelog"):
        if field not in feed:
            found.append(f"'{field}' is missing")

    releases = feed.get("releases", [])
    if not isinstance(releases, list) or not all(_is_text(r) for r in releases):
        found.append("'releases' is not a list of texts")
        releases = []
    changelog = feed.get("changelog", [])
    if not isinstance(changelog, list):
        found.append("'changelog' is not a list")
        changelog = []
    described = set()
    for number, entry in enumerate(changelog, start=1):
        entry = entry if isinstance(entry, dict) else {}
        if not _is_text(entry.get("version")):
            found.append(f"changelog entry {number} has no version")
        else:
            described.add(entry["version"])
        if not _is_text(entry.get("description")):
            found.append(f"changelog entry {number} has no description")

    if _is_text(feed.get("ci_url")) and not feed["ci_url"].endswith("#"):
        found.append("'ci_url' must end in '#'")
    for channel in ("stable", "beta"):
        name = feed.get(channel)
        if not _is_text(name):
            continue
        url, build = feed.get(f"{channel}_url"), feed.get(f"{channel}_build")
        if _is_text(url) and url != RELEASE_PAGE + name:
            found.append(f"'{channel}_url' should be {RELEASE_PAGE}{name}")
        if _is_text(build):
            match = _BUILD.fullmatch(build)
            if not match:
                found.append(f"'{channel}_build' {build} is not YY.MM.PATCH.BUILD")
            elif match.group(1) != _base(name):
                found.append(f"'{channel}_build' {build} is not a build of {name}")
        if name not in releases:
            found.append(f"'{channel}' {name} is not in 'releases'")
        if name not in described:
            found.append(f"'{channel}' {name} has no changelog entry")
    return found


def record(feed: dict, *, tag: str, build: str) -> dict:
    """feed with release tag, published from build, recorded; unchanged when
    the channel already announces a newer build."""
    _, prerelease = parse_tag(tag)
    name = tag[1:]
    channel = "beta" if prerelease else "stable"
    announced = feed.get(f"{channel}_build")
    if (_is_text(announced) and _BUILD.fullmatch(announced) and _BUILD.fullmatch(build)
            and _build_key(announced) > _build_key(build)):
        return feed

    feed[channel] = name
    feed[f"{channel}_url"] = RELEASE_PAGE + name
    feed[f"{channel}_version"] = name
    feed[f"{channel}_build"] = build
    if name not in feed.setdefault("releases", []):
        feed["releases"].append(name)
    if channel == "stable":
        # LogSquirl 26.07.0 and older read only `ci` (#306).
        feed["ci"] = name
    found = problems(feed)
    if found:
        raise ReleaseError("; ".join(found))
    return feed


def _load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as err:
        raise ReleaseError(f"{path.name} is not valid JSON: {err}") from err


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("--feed", type=Path, default=Path("latest.json"))
    check_tag = sub.add_parser("check-tag")
    check_tag.add_argument("--feed", type=Path, default=Path("latest.json"))
    check_tag.add_argument("--tag", required=True)
    rec = sub.add_parser("record")
    rec.add_argument("--feed", type=Path, default=Path("latest.json"))
    rec.add_argument("--tag", required=True)
    rec.add_argument("--build", required=True)
    args = parser.parse_args(argv)

    try:
        feed = _load(args.feed)
        if args.command == "check":
            found = problems(feed)
        elif args.command == "check-tag":
            parse_tag(args.tag)
            entries = feed.get("changelog", []) if isinstance(feed, dict) else []
            names = {e.get("version") for e in entries if isinstance(e, dict)}
            found = [] if args.tag[1:] in names else [
                f"{args.feed.name} has no changelog entry for {args.tag[1:]}: add one "
                "with the release preparation before tagging."]
        else:
            feed = record(feed, tag=args.tag, build=args.build)
            args.feed.write_text(json.dumps(feed, indent=2, ensure_ascii=False) + "\n",
                                 encoding="utf-8")
            found = []
    except (ReleaseError, OSError) as err:
        found = [str(err)]
    for problem in found:
        # One line each: an annotation ends at the first newline.
        print(f"::error::{' '.join(problem.split())}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
