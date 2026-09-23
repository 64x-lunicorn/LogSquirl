"""Tests for release-apt.py (#380): how CI Release builds the signed APT
repository of the last stable releases. No network; the signing test needs gpg."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_apt", Path(__file__).with_name("release-apt.py"))
ra = importlib.util.module_from_spec(_SPEC)
sys.modules["release_apt"] = ra
_SPEC.loader.exec_module(ra)


def release(tag, *, prerelease=False, draft=False, assets=None, published="2026-07-13T12:35:51Z"):
    version = tag[1:]
    if assets is None:
        assets = [f"logsquirl-{version}.1-noble.deb", f"logsquirl-{version}.1-sha256.txt",
                  "logsquirl-mac-arm64.dmg"]
    return {"tag_name": tag, "prerelease": prerelease, "draft": draft,
            "published_at": published, "assets": [{"name": name} for name in assets]}


def tags(chosen):
    return [r["tag"] for r in chosen]


def test_keeps_the_three_newest_stable_releases_newest_first():
    listing = [release("v26.04.2"), release("v26.10.0"), release("v26.07.0"),
               release("v26.06.1"), release("v26.9.0")]
    assert tags(ra.select_releases(listing)) == ["v26.10.0", "v26.9.0", "v26.07.0"]


def test_orders_by_version_not_by_text():
    listing = [release("v26.7.0"), release("v26.10.0"), release("v26.9.1")]
    assert tags(ra.select_releases(listing)) == ["v26.10.0", "v26.9.1", "v26.7.0"]


def test_no_pre_release_and_no_draft():
    listing = [release("v26.10.0-beta2", prerelease=True), release("v26.10.0-rc1"),
               release("v26.11.0", draft=True), release("v26.07.0")]
    assert tags(ra.select_releases(listing)) == ["v26.07.0"]


def test_skips_a_release_without_a_noble_deb_or_checksum_file():
    no_deb = release("v26.09.0", assets=["logsquirl-26.09.0.1-sha256.txt"])
    no_sums = release("v26.08.0", assets=["logsquirl-26.08.0.1-noble.deb"])
    other_distro = release("v26.06.0", assets=["logsquirl-26.06.0.1-el9.rpm",
                                               "logsquirl-26.06.0.1-sha256.txt"])
    assert tags(ra.select_releases([no_deb, no_sums, other_distro, release("v26.07.0")])) \
        == ["v26.07.0"]


def test_skips_a_tag_that_is_not_a_release_tag():
    assert tags(ra.select_releases([release("nightly"), release("v26.07.0")])) == ["v26.07.0"]


def test_manifest_names_what_to_download():
    chosen = ra.select_releases([release("v26.07.0", published="2026-07-13T12:35:51Z")])
    assert chosen == [{"tag": "v26.07.0", "published_at": "2026-07-13T12:35:51Z",
                       "deb": "logsquirl-26.07.0.1-noble.deb",
                       "checksums": "logsquirl-26.07.0.1-sha256.txt"}]


def test_select_reads_a_slurped_listing_and_prints_the_downloads(tmp_path, capsys):
    pages = [[release("v26.07.0")], [release("v26.06.1")]]
    (tmp_path / "releases.json").write_text(json.dumps(pages))
    assert ra.main(["select", "--releases", str(tmp_path / "releases.json"),
                    "--manifest", str(tmp_path / "manifest.json")]) == 0
    assert capsys.readouterr().out.splitlines() == [
        "v26.07.0\tlogsquirl-26.07.0.1-noble.deb\tlogsquirl-26.07.0.1-sha256.txt",
        "v26.06.1\tlogsquirl-26.06.1.1-noble.deb\tlogsquirl-26.06.1.1-sha256.txt"]
    assert tags(json.loads((tmp_path / "manifest.json").read_text())) == ["v26.07.0", "v26.06.1"]


def checksums_for(name, content):
    """The checksum file as a release lists a package: with its path."""
    return f"{hashlib.sha256(content).hexdigest()} *./packages-noble/{name}\n"


def test_a_deb_that_is_not_the_listed_asset_is_refused(tmp_path):
    deb = tmp_path / "logsquirl-26.07.0.1-noble.deb"
    deb.write_bytes(b"the release asset")
    ra.verify_deb(deb, checksums_for(deb.name, b"the release asset"))
    with pytest.raises(ra.ReleaseError, match="does not match"):
        ra.verify_deb(deb, checksums_for(deb.name, b"another file"))


def test_a_deb_the_checksum_file_does_not_list_is_refused(tmp_path):
    deb = tmp_path / "logsquirl-26.07.0.1-noble.deb"
    deb.write_bytes(b"x")
    with pytest.raises(ra.ReleaseError, match="must list"):
        ra.verify_deb(deb, checksums_for("logsquirl-26.06.0.1-noble.deb", b"x"))


WHEN = datetime(2026, 7, 13, 12, 35, 51, tzinfo=timezone.utc)


def test_release_file_lists_the_indexes_with_hash_and_size():
    text = ra.release_file({"main/binary-amd64/Packages": b"abc",
                            "main/binary-amd64/Packages.gz": b"zz"}, when=WHEN)
    assert "Suite: noble\nCodename: noble\n" in text
    assert "Architectures: amd64\nComponents: main\n" in text
    assert f" {hashlib.sha256(b'abc').hexdigest()}                3 main/binary-amd64/Packages\n" in text
    assert text.index("Packages\n") < text.index("Packages.gz\n")


def test_release_file_is_dated_by_the_release_so_a_rerun_gives_the_same_file():
    assert "Date: Mon, 13 Jul 2026 12:35:51 UTC\n" in ra.release_file({}, when=WHEN)


def test_sources_file_points_at_the_repository_and_its_key():
    assert ra.sources_file() == (
        "Types: deb\nURIs: https://packages.lunicorn-lab.de/apt\nSuites: noble\n"
        "Components: main\nArchitectures: amd64\nSigned-By: /etc/apt/keyrings/logsquirl.asc\n")


@pytest.fixture
def downloads(tmp_path):
    """Two releases downloaded as CI Release's job does: one directory per tag."""
    manifest = []
    for tag, build in (("v26.07.0", "26.07.0.741"), ("v26.06.1", "26.03.0.740")):
        directory = tmp_path / "downloads" / tag
        directory.mkdir(parents=True)
        deb = f"logsquirl-{build}-noble.deb"
        (directory / deb).write_bytes(f"deb of {tag}".encode())
        (directory / f"logsquirl-{build}-sha256.txt").write_text(
            checksums_for(deb, f"deb of {tag}".encode()))
        manifest.append({"tag": tag, "published_at": "2026-07-13T12:35:51Z", "deb": deb,
                         "checksums": f"logsquirl-{build}-sha256.txt"})
    (tmp_path / "key.asc").write_text("public key\n")
    return manifest, tmp_path


def fake_index(monkeypatch):
    """Stands in for apt-ftparchive, which only exists on Debian systems."""
    monkeypatch.setattr(ra, "packages_index", lambda apt: b"Package: logsquirl\n")


def test_build_lays_out_the_site(monkeypatch, downloads):
    fake_index(monkeypatch)
    manifest, root = downloads
    ra.build(manifest, downloads=root / "downloads", out=root / "site", public_key=root / "key.asc")

    site = root / "site"
    pool = site / "apt/pool/main/l/logsquirl"
    assert sorted(p.name for p in pool.iterdir()) == [
        "logsquirl-26.03.0.740-noble.deb", "logsquirl-26.07.0.741-noble.deb"]
    assert (pool / "logsquirl-26.07.0.741-noble.deb").read_bytes() == b"deb of v26.07.0"
    dist = site / "apt/dists/noble"
    assert (dist / "main/binary-amd64/Packages").read_bytes() == b"Package: logsquirl\n"
    assert "main/binary-amd64/Packages.gz" in (dist / "Release").read_text()
    assert (site / "logsquirl-packages.asc").read_text() == "public key\n"
    assert (site / "logsquirl.sources").read_text() == ra.sources_file()
    assert (site / "index.html").is_file()


def test_build_twice_gives_the_same_repository(monkeypatch, downloads):
    fake_index(monkeypatch)
    manifest, root = downloads
    for name in ("first", "second"):
        ra.build(manifest, downloads=root / "downloads", out=root / name,
                 public_key=root / "key.asc")

    def files(directory):
        return {str(p.relative_to(directory)): p.read_bytes()
                for p in sorted(directory.rglob("*")) if p.is_file()}
    assert files(root / "first") == files(root / "second")


def test_build_refuses_a_deb_that_changed_after_the_release(monkeypatch, downloads):
    fake_index(monkeypatch)
    manifest, root = downloads
    (root / "downloads/v26.07.0" / manifest[0]["deb"]).write_bytes(b"tampered")
    with pytest.raises(ra.ReleaseError, match="does not match"):
        ra.build(manifest, downloads=root / "downloads", out=root / "site",
                 public_key=root / "key.asc")


def test_build_refuses_to_publish_nothing(tmp_path):
    with pytest.raises(ra.ReleaseError, match="nothing to publish"):
        ra.build([], downloads=tmp_path, out=tmp_path / "site", public_key=tmp_path / "k")


@pytest.mark.skipif(not (shutil.which("gpg") and shutil.which("gpgv")), reason="needs gpg")
def test_signed_release_verifies_with_the_served_key_alone(tmp_path, monkeypatch):
    # A short path: gpg-agent's socket path must fit sockaddr_un, which
    # pytest's temp directory on macOS does not.
    home = Path(tempfile.mkdtemp(dir="/tmp", prefix="gpg-"))
    monkeypatch.setenv("GNUPGHOME", str(home))
    try:
        _sign_and_verify(tmp_path)
    finally:
        subprocess.run(["gpgconf", "--kill", "gpg-agent"], check=False)
        shutil.rmtree(home, ignore_errors=True)


def _sign_and_verify(tmp_path):
    subprocess.run(["gpg", "--batch", "--passphrase", "", "--quick-gen-key",
                    "LogSquirl Test", "rsa2048", "sign", "never"], check=True, capture_output=True)
    fingerprint = subprocess.run(
        ["gpg", "--list-keys", "--with-colons"], check=True, capture_output=True, text=True
    ).stdout.split("fpr:::::::::")[1].split(":")[0]
    site = tmp_path / "site"
    dist = site / "apt/dists/noble"
    dist.mkdir(parents=True)
    (dist / "Release").write_text(ra.release_file({}, when=WHEN))
    (site / ra.KEY_FILE).write_bytes(subprocess.run(
        ["gpg", "--armor", "--export", fingerprint], check=True, capture_output=True).stdout)

    ra.sign(site, fingerprint)
    ra.verify(site)

    (dist / "Release").write_text("tampered\n")
    with pytest.raises(subprocess.CalledProcessError):
        ra.verify(site)
