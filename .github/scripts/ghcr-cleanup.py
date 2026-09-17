#!/usr/bin/env python3
"""Deletes the build image versions on GHCR that no CI run uses any more (#230).

The Docker Images workflow pushes every build image under its inputs hash
(docker/image-hash.sh) plus :latest, and a hash tag is never overwritten
(#215), so each change under docker/ leaves the previous image behind. Before
#215 images were tagged by commit SHA. Every pushed image is an index with a
platform manifest and an attestation manifest (#216), and its cosign signature
is an index tagged sha256-<image digest> with a bundle manifest below it; on
GHCR each of those manifests is a package version of its own.

A version is kept when it

  - carries a protected tag: `latest`, or the inputs hash of the image
    directory on the default branch (what CI Build on master pulls);
  - carries an inputs hash tag (64 hex digits) pushed within the retention
    period, so a pull request based on a recent master still pulls its image
    instead of building it;
  - is younger than the minimum age, so a push in progress, whose manifests
    exist before the index is tagged, is never torn apart;
  - is a manifest listed by a kept index, or the signature (sha256-<digest>
    tag) of a kept image, or a manifest listed by a kept signature.

Everything else is deleted: commit SHA tags, older inputs hashes, the
signatures of deleted images and untagged manifests nothing kept lists. A
deleted image a CI run still asks for is simply built locally by
docker-pull-or-build, as for any new inputs hash.

Usage (from the repository root, on the default branch):
  ghcr-cleanup.py --owner OWNER --image PACKAGE=DIR [--image ...]
                  [--retention-days N] [--min-age-days N] [--dry-run]
Needs `gh` authenticated with packages: write (read for --dry-run) and the
same token in GH_TOKEN for the registry.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone

HASH_TAG = re.compile(r"^[0-9a-f]{64}$")
SIGNATURE_TAG = re.compile(r"^sha256-(?P<hex>[0-9a-f]{64})$")
MANIFEST_TYPES = ", ".join((
    "application/vnd.oci.image.index.v1+json",
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.docker.distribution.manifest.v2+json",
))


class CleanupError(Exception):
    pass


@dataclass
class Plan:
    keep: list[dict] = field(default_factory=list)
    delete: list[tuple[dict, str]] = field(default_factory=list)


def tags_of(version: dict) -> list[str]:
    return list(version.get("metadata", {}).get("container", {}).get("tags") or [])


def age_of(version: dict, now: datetime) -> timedelta:
    stamps = [version[k] for k in ("created_at", "updated_at") if version.get(k)]
    newest = max(datetime.fromisoformat(s.replace("Z", "+00:00")) for s in stamps)
    return now - newest


def plan(versions: list[dict], *, children: Callable[[str], list[str]], protected_tags: set[str],
         retention: timedelta, min_age: timedelta, now: datetime) -> Plan:
    """Splits one package's versions into kept and deleted ones. children(digest)
    lists the manifests an index names; it is only asked about kept versions,
    and an error from it aborts the plan instead of deleting their children."""
    by_digest = {v["name"]: v for v in versions}
    if not any(protected_tags & set(tags_of(v)) for v in versions):
        raise CleanupError(f"no version tagged {' or '.join(sorted(protected_tags))}; refusing to clean up")

    kept: set[str] = set()
    pending: list[str] = []

    def keep(d: str) -> None:
        if d in by_digest and d not in kept:
            kept.add(d)
            pending.append(d)

    for v in versions:
        tags = set(tags_of(v))
        young = age_of(v, now) < min_age
        recent_hash = age_of(v, now) < retention and any(HASH_TAG.match(t) for t in tags)
        if tags & protected_tags or young or recent_hash:
            keep(v["name"])

    signatures = {f"sha256:{m['hex']}": v["name"]
                  for v in versions for t in tags_of(v) if (m := SIGNATURE_TAG.match(t))}
    while pending:
        d = pending.pop()
        if d in signatures:
            keep(signatures[d])
        for child in children(d):
            keep(child)

    result = Plan()
    for v in versions:
        if v["name"] in kept:
            result.keep.append(v)
            continue
        tags = tags_of(v)
        if not tags:
            reason = "untagged, not part of a kept image"
        elif all(SIGNATURE_TAG.match(t) for t in tags):
            reason = "signature of an image that is not kept"
        else:
            reason = f"superseded image ({', '.join(tags)})"
        result.delete.append((v, reason))
    return result


def parse_image(arg: str) -> tuple[str, str]:
    package, sep, directory = arg.partition("=")
    if not sep or not package or not directory:
        raise ValueError(f"--image wants PACKAGE=DIR, got {arg!r}")
    return package, directory


def gh_api(*args: str) -> str:
    return subprocess.run(["gh", "api", *args], check=True, capture_output=True, text=True).stdout


def list_versions(owner: str, package: str) -> list[dict]:
    out = gh_api("--paginate", "--slurp", f"/users/{owner}/packages/container/{package}/versions?per_page=100")
    return [v for page in json.loads(out) for v in page]


def registry_children(owner: str, package: str, token: str) -> Callable[[str], list[str]]:
    repository = f"{owner.lower()}/{package}"
    bearer: list[str] = []

    def auth() -> str:
        if not bearer:
            query = urllib.parse.urlencode({"service": "ghcr.io", "scope": f"repository:{repository}:pull"})
            request = urllib.request.Request(f"https://ghcr.io/token?{query}")
            if token:  # without one, anonymous: enough for public packages
                basic = base64.b64encode(f"token:{token}".encode()).decode()
                request.add_header("Authorization", f"Basic {basic}")
            with urllib.request.urlopen(request, timeout=60) as response:
                bearer.append(json.load(response)["token"])
        return bearer[0]

    def children(d: str) -> list[str]:
        request = urllib.request.Request(f"https://ghcr.io/v2/{repository}/manifests/{d}")
        request.add_header("Authorization", f"Bearer {auth()}")
        request.add_header("Accept", MANIFEST_TYPES)
        with urllib.request.urlopen(request, timeout=60) as response:
            manifest = json.load(response)
        return [m["digest"] for m in manifest.get("manifests", [])]

    return children


def inputs_hash(directory: str) -> str:
    out = subprocess.run(["sh", "docker/image-hash.sh", directory], check=True, capture_output=True, text=True)
    value = out.stdout.strip()
    if not HASH_TAG.match(value):
        raise CleanupError(f"docker/image-hash.sh {directory} printed {value!r}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--owner", required=True)
    parser.add_argument("--image", action="append", required=True, type=parse_image, metavar="PACKAGE=DIR")
    parser.add_argument("--retention-days", type=float, default=30)
    parser.add_argument("--min-age-days", type=float, default=2)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    token = os.environ.get("GH_TOKEN", "")
    now = datetime.now(timezone.utc)

    status = 0
    for package, directory in args.image:
        protected = {"latest", inputs_hash(directory)}
        print(f"::group::{package} (keeps {' and '.join(sorted(protected))})")
        try:
            versions = list_versions(args.owner, package)
            result = plan(versions, children=registry_children(args.owner, package, token),
                          protected_tags=protected, retention=timedelta(days=args.retention_days),
                          min_age=timedelta(days=args.min_age_days), now=now)
        except (CleanupError, OSError, subprocess.CalledProcessError) as e:
            print("::endgroup::")
            print(f"::error::{package}: {e}")
            status = 1
            continue
        for v in result.keep:
            print(f"keep   {v['name']} {' '.join(tags_of(v))}")
        failed = 0
        for v, reason in result.delete:
            verb = "would delete" if args.dry_run else "delete"
            print(f"{verb} {v['name']}: {reason}")
            if not args.dry_run:
                try:
                    gh_api("--method", "DELETE", f"/users/{args.owner}/packages/container/{package}/versions/{v['id']}")
                except (OSError, subprocess.CalledProcessError) as e:
                    print(f"::error::{package}: deleting {v['name']} failed: {e}")
                    failed += 1
                    status = 1
        print("::endgroup::")
        action = "would delete" if args.dry_run else "deleted"
        print(f"{package}: kept {len(result.keep)}, {action} {len(result.delete) - failed} of {len(versions)} versions")
    return status


if __name__ == "__main__":
    sys.exit(main())
