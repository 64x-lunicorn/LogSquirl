#!/usr/bin/env python3
"""Builds the signed DNF repositories from the last stable releases (#381).

Two repositories are served next to the APT repository (#380) at
https://packages.lunicorn-lab.de: `dnf/fedora` (the Fedora 44 RPM) and
`dnf/el10` (the Oracle Linux 10 RPM, which is also what EL10 clones install).
Each holds the RPMs of the last three stable releases exactly as published on
GitHub Releases and is rebuilt completely on every deploy. Only the metadata
is signed (`repo_gpgcheck=1`, `gpgcheck=0`): signing an RPM rewrites the file,
and it would no longer be the release asset the attestations cover. The signed
repomd.xml carries every package's checksum, so dnf still verifies each
package. BUILD.md ("Package repository") documents it.

  select --flavor fedora|el10 --releases F --manifest M [--keep N]
      Picks the N (default 3) newest stable, published releases of the GitHub
      releases listing F that carry the flavor's RPM and a checksum file;
      writes them to manifest M and prints `tag<TAB>rpm<TAB>checksums` each.

  build --flavor FLAVOR --manifest M --downloads D --out O
      Checks each downloaded RPM against its release's checksum file and lays
      out O/dnf/FLAVOR (RPMs and repodata, from createrepo_c) and the
      O/logsquirl-FLAVOR.repo file users drop into /etc/yum.repos.d.

  sign --out O --fingerprint FPR
      Signs the repomd.xml of every repository in O as repomd.xml.asc with
      the secret key FPR of the current GnuPG home.

  verify --out O
      Checks every repomd.xml.asc with the public key the site serves alone,
      in an empty keyring, as dnf does.

A refused step exits 1 with an ::error:: annotation.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from releases import (CHECKSUMS, ReleaseError, asset_named, parse_tag, report, verify_asset,
                      version_key)

HOST = "https://packages.lunicorn-lab.de"
KEEP = 3
KEY_FILE = "logsquirl-packages.asc"
# flavor -> (release asset suffix, distribution the .repo file is for)
FLAVORS = {
    "fedora": ("fedora", "Fedora 44"),
    "el10": ("oracle", "Oracle Linux 10 and EL10 clones"),
}


def _rpm_pattern(flavor: str) -> re.Pattern:
    return re.compile(rf"logsquirl-[0-9]+(?:\.[0-9]+){{2,3}}-{FLAVORS[flavor][0]}\.rpm")


def select_releases(listing: list[dict], *, flavor: str, keep: int = KEEP) -> list[dict]:
    """The newest `keep` stable, published releases that have the flavor's RPM
    and a checksum file, newest first, as {tag, published_at, rpm, checksums}."""
    found = []
    for release in listing:
        tag = release.get("tag_name", "")
        if release.get("draft") or release.get("prerelease"):
            continue
        try:
            version, prerelease = parse_tag(tag)
        except ReleaseError:
            continue
        if prerelease:
            continue
        names = [asset["name"] for asset in release.get("assets", [])]
        rpm, checksums = asset_named(names, _rpm_pattern(flavor)), asset_named(names, CHECKSUMS)
        if rpm and checksums:
            found.append((version_key(version), {
                "tag": tag, "published_at": release["published_at"],
                "rpm": rpm, "checksums": checksums}))
    found.sort(key=lambda item: item[0], reverse=True)
    return [release for _, release in found[:keep]]


def repo_file(flavor: str) -> str:
    """The .repo file users drop into /etc/yum.repos.d: the metadata is signed
    and checked, the RPMs are not signed and are checked through it."""
    return "\n".join([
        "[logsquirl]",
        f"name=LogSquirl for {FLAVORS[flavor][1]}",
        f"baseurl={HOST}/dnf/{flavor}",
        "enabled=1",
        "gpgcheck=0",
        "repo_gpgcheck=1",
        f"gpgkey={HOST}/{KEY_FILE}",
    ]) + "\n"


def createrepo(directory: Path, *, revision: int) -> None:
    tool = shutil.which("createrepo_c")
    if not tool:
        raise ReleaseError("createrepo_c is not installed (apt-get install createrepo-c).")
    # The revision is the newest release's publication, not the build time.
    subprocess.run([tool, "--quiet", "--revision", str(revision), "--set-timestamp-to-revision",
                    str(directory)], check=True, capture_output=True, text=True)


def build(manifest: list[dict], *, flavor: str, downloads: Path, out: Path) -> None:
    if not manifest:
        raise ReleaseError(f"No stable release with a {flavor} RPM: nothing to publish.")
    repo = out / "dnf" / flavor
    repo.mkdir(parents=True)
    newest = 0
    for release in manifest:
        source = downloads / release["tag"]
        rpm = source / release["rpm"]
        verify_asset(rpm, (source / release["checksums"]).read_text(encoding="utf-8"))
        published = int(datetime.fromisoformat(
            release["published_at"].replace("Z", "+00:00")).timestamp())
        newest = max(newest, published)
        shutil.copyfile(rpm, repo / rpm.name)
        # createrepo records the file time, so a re-run must not see the copy time.
        os.utime(repo / rpm.name, (published, published))
    createrepo(repo, revision=newest)
    (out / f"logsquirl-{flavor}.repo").write_text(repo_file(flavor), encoding="utf-8")


def _repomds(out: Path) -> list[Path]:
    found = sorted((out / "dnf").glob("*/repodata/repomd.xml"))
    if not found:
        raise ReleaseError("The site has no DNF repository to sign or verify.")
    return found


def sign(out: Path, fingerprint: str) -> None:
    """Signs with a key that has no passphrase, as #379 creates it."""
    for repomd in _repomds(out):
        subprocess.run(["gpg", "--batch", "--yes", "--pinentry-mode", "loopback",
                        "--passphrase", "", "--local-user", fingerprint, "--armor",
                        "--detach-sign", "-o", f"{repomd}.asc", str(repomd)],
                       check=True, capture_output=True, text=True)


def verify(out: Path) -> None:
    """Every signature checks against the served public key alone."""
    repomds = _repomds(out)
    with tempfile.TemporaryDirectory() as tmp:
        keyring = Path(tmp) / "logsquirl.gpg"
        with keyring.open("wb") as ring:
            subprocess.run(["gpg", "--dearmor"], input=(out / KEY_FILE).read_bytes(),
                           stdout=ring, stderr=subprocess.PIPE, check=True)
        for repomd in repomds:
            subprocess.run(["gpgv", "--keyring", str(keyring), f"{repomd}.asc", str(repomd)],
                           check=True, capture_output=True, text=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sel = sub.add_parser("select")
    sel.add_argument("--flavor", choices=sorted(FLAVORS), required=True)
    sel.add_argument("--releases", type=Path, required=True)
    sel.add_argument("--manifest", type=Path, required=True)
    sel.add_argument("--keep", type=int, default=KEEP)
    bld = sub.add_parser("build")
    bld.add_argument("--flavor", choices=sorted(FLAVORS), required=True)
    bld.add_argument("--manifest", type=Path, required=True)
    bld.add_argument("--downloads", type=Path, required=True)
    bld.add_argument("--out", type=Path, required=True)
    sgn = sub.add_parser("sign")
    sgn.add_argument("--out", type=Path, required=True)
    sgn.add_argument("--fingerprint", required=True)
    ver = sub.add_parser("verify")
    ver.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        if args.command == "select":
            listing = json.loads(args.releases.read_text(encoding="utf-8"))
            # `gh api --paginate --slurp` wraps every page's array in one.
            if listing and isinstance(listing[0], list):
                listing = [release for page in listing for release in page]
            chosen = select_releases(listing, flavor=args.flavor, keep=args.keep)
            args.manifest.write_text(json.dumps(chosen, indent=2) + "\n", encoding="utf-8")
            for release in chosen:
                print(f"{release['tag']}\t{release['rpm']}\t{release['checksums']}")
        elif args.command == "build":
            build(json.loads(args.manifest.read_text(encoding="utf-8")), flavor=args.flavor,
                  downloads=args.downloads, out=args.out)
        elif args.command == "sign":
            sign(args.out, args.fingerprint)
        else:
            verify(args.out)
        found = []
    except subprocess.CalledProcessError as err:
        found = [f"{' '.join(map(str, err.cmd[:2]))} failed: {(err.stderr or '').strip()}"]
    except (ReleaseError, OSError, ValueError) as err:
        found = [str(err)]
    return report(found)


if __name__ == "__main__":
    sys.exit(main())
