"""Tests for release-dnf.py (#381): how CI Release builds the signed DNF
repositories of the last stable releases. No network; createrepo_c is faked
and the signing test needs gpg."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_dnf", Path(__file__).with_name("release-dnf.py"))
rd = importlib.util.module_from_spec(_SPEC)
sys.modules["release_dnf"] = rd
_SPEC.loader.exec_module(rd)

PUBLISHED = "2026-07-13T12:35:51Z"


def release(tag, *, prerelease=False, draft=False, assets=None):
    version = tag[1:]
    if assets is None:
        assets = [f"logsquirl-{version}.1-fedora.rpm", f"logsquirl-{version}.1-oracle.rpm",
                  f"logsquirl-{version}.1-noble.deb", f"logsquirl-{version}.1-sha256.txt"]
    return {"tag_name": tag, "prerelease": prerelease, "draft": draft,
            "published_at": PUBLISHED, "assets": [{"name": name} for name in assets]}


def tags(chosen):
    return [r["tag"] for r in chosen]


def test_keeps_the_three_newest_stable_releases_newest_first_and_no_pre_release():
    listing = [release("v26.7.0"), release("v26.10.0"), release("v26.10.1-beta1", prerelease=True),
               release("v26.9.0"), release("v26.6.0"), release("v26.11.0", draft=True)]
    assert tags(rd.select_releases(listing, flavor="fedora")) == ["v26.10.0", "v26.9.0", "v26.7.0"]


def test_each_flavor_takes_its_own_rpm():
    only_oracle = release("v26.8.0", assets=["logsquirl-26.8.0.1-oracle.rpm",
                                             "logsquirl-26.8.0.1-sha256.txt"])
    listing = [only_oracle, release("v26.7.0")]
    assert tags(rd.select_releases(listing, flavor="fedora")) == ["v26.7.0"]
    assert tags(rd.select_releases(listing, flavor="el10")) == ["v26.8.0", "v26.7.0"]
    manifest = rd.select_releases([only_oracle], flavor="el10")
    assert manifest == [{"tag": "v26.8.0", "published_at": PUBLISHED,
                         "rpm": "logsquirl-26.8.0.1-oracle.rpm",
                         "checksums": "logsquirl-26.8.0.1-sha256.txt"}]


def test_skips_a_release_without_checksums():
    bare = release("v26.8.0", assets=["logsquirl-26.8.0.1-fedora.rpm"])
    assert tags(rd.select_releases([bare, release("v26.7.0")], flavor="fedora")) == ["v26.7.0"]


def test_select_prints_the_downloads(tmp_path, capsys):
    (tmp_path / "r.json").write_text(json.dumps([[release("v26.7.0")]]))
    assert rd.main(["select", "--flavor", "el10", "--releases", str(tmp_path / "r.json"),
                    "--manifest", str(tmp_path / "m.json")]) == 0
    assert capsys.readouterr().out == \
        "v26.7.0\tlogsquirl-26.7.0.1-oracle.rpm\tlogsquirl-26.7.0.1-sha256.txt\n"


def test_repo_file_checks_the_metadata_and_not_the_rpms():
    text = rd.repo_file("fedora")
    assert "baseurl=https://packages.lunicorn-lab.de/dnf/fedora\n" in text
    assert "gpgcheck=0\nrepo_gpgcheck=1\n" in text
    assert "gpgkey=https://packages.lunicorn-lab.de/logsquirl-packages.asc\n" in text
    assert "dnf/el10" in rd.repo_file("el10")


@pytest.fixture
def downloads(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(rd, "createrepo",
                        lambda directory, *, revision: calls.append((directory, revision)))
    manifest = []
    for tag, build in (("v26.7.0", "26.7.0.741"), ("v26.6.1", "26.3.0.740")):
        directory = tmp_path / "downloads" / tag
        directory.mkdir(parents=True)
        rpm = f"logsquirl-{build}-fedora.rpm"
        content = f"rpm of {tag}".encode()
        (directory / rpm).write_bytes(content)
        (directory / f"logsquirl-{build}-sha256.txt").write_text(
            f"{hashlib.sha256(content).hexdigest()} *./packages-fedora/{rpm}\n")
        manifest.append({"tag": tag, "published_at": PUBLISHED, "rpm": rpm,
                         "checksums": f"logsquirl-{build}-sha256.txt"})
    return manifest, tmp_path, calls


def test_build_lays_out_the_repository_with_the_release_assets(downloads):
    manifest, root, calls = downloads
    rd.build(manifest, flavor="fedora", downloads=root / "downloads", out=root / "site")
    repo = root / "site/dnf/fedora"
    assert sorted(p.name for p in repo.iterdir()) == [
        "logsquirl-26.3.0.740-fedora.rpm", "logsquirl-26.7.0.741-fedora.rpm"]
    assert (repo / "logsquirl-26.7.0.741-fedora.rpm").read_bytes() == b"rpm of v26.7.0"
    assert (root / "site/logsquirl-fedora.repo").read_text() == rd.repo_file("fedora")
    # The repository revision is the newest release's publication, not the build time.
    assert calls == [(repo, 1783946151)]


def test_build_refuses_an_rpm_that_changed_after_the_release(downloads):
    manifest, root, _ = downloads
    (root / "downloads/v26.7.0" / manifest[0]["rpm"]).write_bytes(b"tampered")
    with pytest.raises(rd.ReleaseError, match="does not match"):
        rd.build(manifest, flavor="fedora", downloads=root / "downloads", out=root / "site")


def test_build_refuses_to_publish_nothing(tmp_path):
    with pytest.raises(rd.ReleaseError, match="nothing to publish"):
        rd.build([], flavor="el10", downloads=tmp_path, out=tmp_path / "site")


def test_sign_and_verify_need_a_repository(tmp_path):
    with pytest.raises(rd.ReleaseError, match="no DNF repository"):
        rd.verify(tmp_path)


@pytest.mark.skipif(not (shutil.which("gpg") and shutil.which("gpgv")), reason="needs gpg")
def test_signed_metadata_verifies_with_the_served_key_alone(tmp_path, monkeypatch):
    # A short path: gpg-agent's socket path must fit sockaddr_un.
    home = Path(tempfile.mkdtemp(dir="/tmp", prefix="gpg-"))
    monkeypatch.setenv("GNUPGHOME", str(home))
    try:
        subprocess.run(["gpg", "--batch", "--passphrase", "", "--quick-gen-key",
                        "LogSquirl Test", "rsa2048", "sign", "never"],
                       check=True, capture_output=True)
        fingerprint = subprocess.run(
            ["gpg", "--list-keys", "--with-colons"], check=True, capture_output=True, text=True
        ).stdout.split("fpr:::::::::")[1].split(":")[0]
        for flavor in rd.FLAVORS:
            repodata = tmp_path / "dnf" / flavor / "repodata"
            repodata.mkdir(parents=True)
            (repodata / "repomd.xml").write_text(f"<repomd>{flavor}</repomd>")
        (tmp_path / rd.KEY_FILE).write_bytes(subprocess.run(
            ["gpg", "--armor", "--export", fingerprint], check=True, capture_output=True).stdout)
        rd.sign(tmp_path, fingerprint)
        rd.verify(tmp_path)
        (tmp_path / "dnf/el10/repodata/repomd.xml").write_text("tampered")
        with pytest.raises(subprocess.CalledProcessError):
            rd.verify(tmp_path)
    finally:
        subprocess.run(["gpgconf", "--kill", "gpg-agent"], check=False)
        shutil.rmtree(home, ignore_errors=True)
