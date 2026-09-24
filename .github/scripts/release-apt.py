#!/usr/bin/env python3
"""Builds the signed APT repository from the last stable releases (#380).

The repository is served at https://packages.lunicorn-lab.de/apt by GitHub
Pages. It holds the noble .deb of the last three stable releases, exactly as
published on GitHub Releases, and is rebuilt completely on every deploy, so
there is no repository state to corrupt and a re-run gives the same
repository. BUILD.md ("Package repository") documents it.

  select --releases F --manifest M [--keep N]
      Picks the N (default 3) newest stable, published releases of the GitHub
      releases listing F (`gh api --paginate --slurp .../releases`) that carry
      a noble .deb and a checksum file; writes them to manifest M and prints
      `tag<TAB>deb<TAB>checksums` per release for the download.

  build --manifest M --downloads D --out O --public-key K
      Checks each downloaded .deb against its release's checksum file and lays
      out the site in O: apt/pool, apt/dists/noble (Packages, Release), the
      public key K, the deb822 .sources file and an index page. Needs
      apt-ftparchive.

  sign --out O --fingerprint FPR
      Signs O/apt/dists/noble/Release as InRelease and Release.gpg with the
      secret key FPR of the current GnuPG home.

  verify --out O
      Checks both signatures with the public key the site serves alone, in an
      empty keyring, as apt does.

A refused step exits 1 with an ::error:: annotation.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path

from releases import ReleaseError, asset_named, CHECKSUMS, listed_hash, parse_tag, report, verify_asset, version_key

HOST = "https://packages.lunicorn-lab.de"
SUITE = "noble"
COMPONENT = "main"
ARCH = "amd64"
KEEP = 3
KEY_FILE = "logsquirl-packages.asc"
SOURCES_FILE = "logsquirl.sources"
KEYRING = "/etc/apt/keyrings/logsquirl.asc"

_DEB = re.compile(r"logsquirl-[0-9]+(?:\.[0-9]+){2,3}-noble\.deb")


def select_releases(listing: list[dict], *, keep: int = KEEP) -> list[dict]:
    """The newest `keep` stable, published releases that have a noble .deb and
    a checksum file, newest first, as {tag, published_at, deb, checksums}."""
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
        deb, checksums = asset_named(names, _DEB), asset_named(names, CHECKSUMS)
        if deb and checksums:
            found.append((version_key(version), {
                "tag": tag, "published_at": release["published_at"],
                "deb": deb, "checksums": checksums}))
    found.sort(key=lambda item: item[0], reverse=True)
    return [release for _, release in found[:keep]]


def verify_deb(deb: Path, checksums: str) -> None:
    """Refuses a .deb that is not the release asset the checksum file lists,
    so what the repository serves is what the release's attestations cover."""
    verify_asset(deb, checksums)


def http_date(when: datetime) -> str:
    return format_datetime(when.astimezone(timezone.utc), usegmt=True).replace("GMT", "UTC")


def release_file(indexes: dict[str, bytes], *, when: datetime) -> str:
    """The Release file of the suite. Its date is the newest release's
    publication, not the build time, so a re-run gives the same file."""
    lines = [
        "Origin: LogSquirl",
        "Label: LogSquirl",
        f"Suite: {SUITE}",
        f"Codename: {SUITE}",
        f"Date: {http_date(when)}",
        f"Architectures: {ARCH}",
        f"Components: {COMPONENT}",
        "Description: LogSquirl packages for Ubuntu 24.04 (noble)",
        "SHA256:",
    ]
    for path in sorted(indexes):
        lines.append(f" {hashlib.sha256(indexes[path]).hexdigest()} {len(indexes[path]):>16} {path}")
    return "\n".join(lines) + "\n"


def sources_file() -> str:
    """The deb822 file users drop into /etc/apt/sources.list.d."""
    return "\n".join([
        "Types: deb",
        f"URIs: {HOST}/apt",
        f"Suites: {SUITE}",
        f"Components: {COMPONENT}",
        f"Architectures: {ARCH}",
        f"Signed-By: {KEYRING}",
    ]) + "\n"


def index_page() -> str:
    return f"""<!doctype html>
<meta charset="utf-8">
<title>LogSquirl packages</title>
<h1>LogSquirl packages</h1>
<p>APT repository for Ubuntu 24.04 (noble), amd64. Set it up once:</p>
<pre>sudo install -d -m 0755 /etc/apt/keyrings
sudo curl -fsSL {HOST}/{KEY_FILE} -o {KEYRING}
sudo curl -fsSL {HOST}/{SOURCES_FILE} -o /etc/apt/sources.list.d/logsquirl.sources
sudo apt update
sudo apt install logsquirl</pre>
<p>Updates arrive with <code>sudo apt upgrade</code>.</p>
<p>Fedora 44 and Oracle Linux 10 (dnf): <code>sudo curl -fsSL {HOST}/logsquirl-fedora.repo -o /etc/yum.repos.d/logsquirl.repo</code>
(<code>logsquirl-el10.repo</code> on Oracle Linux 10), then <code>sudo dnf install logsquirl</code>.</p>
"""


def packages_index(apt_root: Path) -> bytes:
    """apt-ftparchive's Packages for the pool, one stanza per .deb, sorted by
    file name so the index does not depend on the directory order."""
    apt_ftparchive = shutil.which("apt-ftparchive")
    if not apt_ftparchive:
        raise ReleaseError("apt-ftparchive is not installed (apt-get install apt-utils).")
    out = subprocess.run([apt_ftparchive, "packages", "pool"], cwd=apt_root, check=True,
                         capture_output=True, text=True).stdout
    stanzas = sorted((s.strip("\n") for s in out.split("\n\n") if s.strip()),
                     key=lambda s: re.search(r"^Filename: (.+)$", s, re.MULTILINE).group(1))
    return ("\n\n".join(stanzas) + "\n").encode()


def build(manifest: list[dict], *, downloads: Path, out: Path, public_key: Path) -> None:
    if not manifest:
        raise ReleaseError("No stable release with a noble .deb: nothing to publish.")
    apt = out / "apt"
    pool = apt / "pool" / COMPONENT / "l" / "logsquirl"
    pool.mkdir(parents=True)
    for release in manifest:
        source = downloads / release["tag"]
        deb = source / release["deb"]
        verify_deb(deb, (source / release["checksums"]).read_text(encoding="utf-8"))
        shutil.copyfile(deb, pool / deb.name)

    packages = packages_index(apt)
    # mtime 0: the same index compresses to the same bytes on a re-run.
    packages_gz = gzip.compress(packages, mtime=0)
    binary = f"{COMPONENT}/binary-{ARCH}"
    dist = apt / "dists" / SUITE
    (dist / binary).mkdir(parents=True)
    (dist / binary / "Packages").write_bytes(packages)
    (dist / binary / "Packages.gz").write_bytes(packages_gz)
    newest = max(datetime.fromisoformat(r["published_at"].replace("Z", "+00:00")) for r in manifest)
    (dist / "Release").write_text(release_file(
        {f"{binary}/Packages": packages, f"{binary}/Packages.gz": packages_gz}, when=newest),
        encoding="utf-8")

    shutil.copyfile(public_key, out / KEY_FILE)
    (out / SOURCES_FILE).write_text(sources_file(), encoding="utf-8")
    (out / "index.html").write_text(index_page(), encoding="utf-8")


def _gpg(*args: str, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(["gpg", "--batch", "--yes", *args], check=True,
                          capture_output=True, text=True, **kwargs)


def sign(out: Path, fingerprint: str) -> None:
    """Signs with a key that has no passphrase, as #379 creates it: the secret
    is the key alone, and gpg is told the empty passphrase."""
    dist = out / "apt" / "dists" / SUITE
    signing = ["--pinentry-mode", "loopback", "--passphrase", "", "--local-user", fingerprint]
    _gpg(*signing, "--clearsign", "-o", str(dist / "InRelease"), str(dist / "Release"))
    _gpg(*signing, "--armor", "--detach-sign", "-o", str(dist / "Release.gpg"), str(dist / "Release"))


def verify(out: Path) -> None:
    """Both signatures check against the served public key alone."""
    dist = out / "apt" / "dists" / SUITE
    with tempfile.TemporaryDirectory() as tmp:
        keyring = Path(tmp) / "logsquirl.gpg"
        with keyring.open("wb") as ring:
            subprocess.run(["gpg", "--dearmor"], input=(out / KEY_FILE).read_bytes(),
                           stdout=ring, stderr=subprocess.PIPE, check=True)
        for signed in ([dist / "InRelease"], [dist / "Release.gpg", dist / "Release"]):
            subprocess.run(["gpgv", "--keyring", str(keyring), *map(str, signed)], check=True,
                           capture_output=True, text=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sel = sub.add_parser("select")
    sel.add_argument("--releases", type=Path, required=True)
    sel.add_argument("--manifest", type=Path, required=True)
    sel.add_argument("--keep", type=int, default=KEEP)
    bld = sub.add_parser("build")
    bld.add_argument("--manifest", type=Path, required=True)
    bld.add_argument("--downloads", type=Path, required=True)
    bld.add_argument("--out", type=Path, required=True)
    bld.add_argument("--public-key", type=Path, required=True)
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
            chosen = select_releases(listing, keep=args.keep)
            args.manifest.write_text(json.dumps(chosen, indent=2) + "\n", encoding="utf-8")
            for release in chosen:
                print(f"{release['tag']}\t{release['deb']}\t{release['checksums']}")
        elif args.command == "build":
            build(json.loads(args.manifest.read_text(encoding="utf-8")),
                  downloads=args.downloads, out=args.out, public_key=args.public_key)
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
