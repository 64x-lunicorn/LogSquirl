"""Tests for release-cask.py (#378): how CI Release sets the Homebrew cask to
a published stable release. No network."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_cask", Path(__file__).with_name("release-cask.py"))
rc = importlib.util.module_from_spec(_SPEC)
sys.modules["release_cask"] = rc
_SPEC.loader.exec_module(rc)

OLD_HASH = "f6160cfa0e996de319113f9159d1149611b62d3672cec6861ced64cdfce896aa"
NEW_HASH = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

# The cask as the tap holds it (64x-lunicorn/homebrew-tap, Casks/logsquirl.rb).
CASK = f'''cask "logsquirl" do
  version "26.07.0"
  sha256 "{OLD_HASH}"

  url "https://github.com/64x-lunicorn/LogSquirl/releases/download/v#{{version}}/logsquirl-mac-arm64.dmg"
  name "LogSquirl"
  desc "Fast, smart log file explorer"
  homepage "https://logsquirl.lunicorn-lab.de/"

  livecheck do
    url :url
    strategy :github_latest
  end

  depends_on arch: :arm64
  depends_on macos: :sequoia

  app "logsquirl.app"
end
'''


def checksums(dmg_hash=NEW_HASH):
    """A checksum file as CI Release writes it: `sha256sum --binary`, basenames."""
    return (
        "8bf890b2b62e93a20609935400a3b6589dab215e46a04a6273804410cf47700f *logsquirl-26.10.0.790-noble.deb\n"
        f"{dmg_hash} *logsquirl-mac-arm64.dmg\n"
        "74d06177e815dcf3e0e09d6531ff84c5d533f279757a3bbdd2a832bad7e49563 *logsquirl-arm64.app.tar.gz\n"
    )


# ── dmg_hash ──────────────────────────────────────────────────────────────

def test_the_dmg_hash_is_read_from_its_line():
    assert rc.dmg_hash(checksums()) == NEW_HASH


def test_a_text_mode_line_is_read_too():
    assert rc.dmg_hash(f"{NEW_HASH}  logsquirl-mac-arm64.dmg\n") == NEW_HASH


def test_the_app_archive_is_not_mistaken_for_the_dmg():
    only_archive = f"{NEW_HASH} *logsquirl-arm64.app.tar.gz\n"
    with pytest.raises(rc.ReleaseError, match="no logsquirl-mac-arm64.dmg"):
        rc.dmg_hash(only_archive)


def test_a_checksum_file_without_the_dmg_is_refused():
    # Releases up to 26.07.0 wrote no line for the macOS packages.
    old = "8bf890b2b62e93a20609935400a3b6589dab215e46a04a6273804410cf47700f *./packages-noble/x.deb\n"
    with pytest.raises(rc.ReleaseError, match="no logsquirl-mac-arm64.dmg"):
        rc.dmg_hash(old)


def test_two_dmg_lines_are_refused():
    with pytest.raises(rc.ReleaseError, match="more than once"):
        rc.dmg_hash(checksums() + f"{OLD_HASH} *logsquirl-mac-arm64.dmg\n")


def test_a_malformed_hash_is_refused():
    with pytest.raises(rc.ReleaseError, match="not a SHA-256"):
        rc.dmg_hash("abc123 *logsquirl-mac-arm64.dmg\n")


# ── update ────────────────────────────────────────────────────────────────

def test_a_newer_release_sets_version_and_hash():
    updated = rc.update(CASK, tag="v26.10.0", sha256=NEW_HASH)
    assert 'version "26.10.0"' in updated
    assert f'sha256 "{NEW_HASH}"' in updated
    assert updated.replace("26.10.0", "26.07.0").replace(NEW_HASH, OLD_HASH) == CASK


def test_the_same_release_with_the_same_hash_changes_nothing():
    assert rc.update(CASK, tag="v26.07.0", sha256=OLD_HASH) == CASK


def test_the_same_release_with_a_new_hash_takes_the_new_hash():
    # A re-run of CI Release signs and uploads the DMG again.
    updated = rc.update(CASK, tag="v26.07.0", sha256=NEW_HASH)
    assert updated == CASK.replace(OLD_HASH, NEW_HASH)


@pytest.mark.parametrize("tag", ["v26.06.1", "v25.12.9"])
def test_an_older_release_never_downgrades_the_cask(tag):
    assert rc.update(CASK, tag=tag, sha256=NEW_HASH) == CASK


def test_versions_compare_as_numbers_not_text():
    cask = CASK.replace('version "26.07.0"', 'version "26.9.0"')
    assert 'version "26.10.0"' in rc.update(cask, tag="v26.10.0", sha256=NEW_HASH)


@pytest.mark.parametrize("tag", ["v26.10.0-beta1", "v26.10.0-rc2"])
def test_a_prerelease_is_refused(tag):
    with pytest.raises(rc.ReleaseError, match="pre-release"):
        rc.update(CASK, tag=tag, sha256=NEW_HASH)


def test_a_malformed_tag_is_refused():
    with pytest.raises(rc.ReleaseError, match="not a release tag"):
        rc.update(CASK, tag="26.10.0", sha256=NEW_HASH)


@pytest.mark.parametrize("line", ['  version "26.07.0"\n', f'  sha256 "{OLD_HASH}"\n'])
def test_a_cask_without_the_line_is_refused(line):
    with pytest.raises(rc.ReleaseError, match="exactly one"):
        rc.update(CASK.replace(line, ""), tag="v26.10.0", sha256=NEW_HASH)


def test_a_cask_with_the_line_twice_is_refused():
    doubled = CASK.replace('  version "26.07.0"\n', '  version "26.07.0"\n  version "26.07.0"\n')
    with pytest.raises(rc.ReleaseError, match="exactly one"):
        rc.update(doubled, tag="v26.10.0", sha256=NEW_HASH)


# ── command line ──────────────────────────────────────────────────────────

def files(tmp_path, dmg_hash=NEW_HASH):
    cask = tmp_path / "logsquirl.rb"
    cask.write_text(CASK, encoding="utf-8")
    sums = tmp_path / "logsquirl-26.10.0.790-sha256.txt"
    sums.write_text(checksums(dmg_hash), encoding="utf-8")
    return cask, sums


def test_update_writes_the_cask(tmp_path, capsys):
    cask, sums = files(tmp_path)
    assert rc.main(["update", "--cask", str(cask), "--tag", "v26.10.0",
                    "--checksums", str(sums)]) == 0
    assert 'version "26.10.0"' in cask.read_text(encoding="utf-8")
    assert "26.10.0" in capsys.readouterr().out


def test_update_leaves_an_unchanged_cask_byte_for_byte(tmp_path):
    cask, sums = files(tmp_path, dmg_hash=OLD_HASH)
    before = cask.stat().st_mtime_ns
    assert rc.main(["update", "--cask", str(cask), "--tag", "v26.07.0",
                    "--checksums", str(sums)]) == 0
    assert cask.read_text(encoding="utf-8") == CASK
    assert cask.stat().st_mtime_ns == before


def test_update_reports_a_problem_as_an_annotation(tmp_path, capsys):
    cask, sums = files(tmp_path)
    assert rc.main(["update", "--cask", str(cask), "--tag", "v26.10.0-beta1",
                    "--checksums", str(sums)]) == 1
    assert capsys.readouterr().out.startswith("::error::")
    assert cask.read_text(encoding="utf-8") == CASK
