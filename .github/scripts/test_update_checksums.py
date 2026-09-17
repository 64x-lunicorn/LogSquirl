"""Tests for update-checksums.py (#211): parsing the `# renovate:` version/hash
pairs and building their download URLs. No network."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "update_checksums", Path(__file__).with_name("update-checksums.py"))
uc = importlib.util.module_from_spec(_SPEC)
sys.modules["update_checksums"] = uc
_SPEC.loader.exec_module(uc)

HASH_A = "a" * 64
HASH_B = "b" * 64


def test_a_yaml_env_pair_is_parsed():
    text = f"""\
      env:
        # renovate: datasource=github-releases depName=anchore/grype
        GRYPE_VERSION: 0.118.0
        GRYPE_SHA256: {HASH_A}
      run: |
"""
    [pair] = uc.parse_pairs(text, "action.yml")
    assert pair.dep_name == "anchore/grype"
    assert pair.version == "0.118.0"
    assert [(h.name, h.value, h.line) for h in pair.hashes] == [("GRYPE_SHA256", HASH_A, 3)]


def test_a_version_without_a_hash_is_not_a_pair():
    # Qt, the sccache action input and pip pins carry no download checksum.
    text = """\
        # renovate: datasource=pypi depName=clang-format
        CLANG_FORMAT_VERSION: 23.1.1
        OTHER: value
"""
    assert uc.parse_pairs(text, "ci.yml") == []


def test_every_hash_of_a_pair_gets_the_url_of_its_own_asset():
    text = f"""\
        # renovate: datasource=github-releases depName=ninja-build/ninja
        NINJA_VERSION: 1.13.2
        NINJA_SHA256_WINDOWS: {HASH_A}
        NINJA_SHA256_LINUX: {HASH_B}
"""
    [pair] = uc.parse_pairs(text, "action.yml")
    assert uc.download_urls(pair) == {
        "NINJA_SHA256_WINDOWS": "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
        "NINJA_SHA256_LINUX": "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip",
    }


def test_a_shell_pair_with_quotes_and_an_underscored_version_url():
    text = f"""\
        # renovate: datasource=github-tags depName=boostorg/boost extractVersion=^boost-(?<version>.*)$
        BOOST_VERSION="1.86.0"
        BOOST_SHA256="{HASH_A}"
        BOOST_VERSION_UNDERSCORE=$(echo "$BOOST_VERSION" | tr '.' '_')
"""
    [pair] = uc.parse_pairs(text, "action.yml")
    assert pair.version == "1.86.0"
    assert uc.download_urls(pair) == {
        "BOOST_SHA256": "https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2"}


def test_a_cmake_pair_is_parsed_with_the_url_of_each_platform_asset():
    # The minidump tool the app ships is downloaded at configure time (#318).
    text = f"""\
# renovate: datasource=github-releases depName=rust-minidump/rust-minidump
set(MINIDUMP_STACKWALK_VERSION "0.27.0")
set(MINIDUMP_STACKWALK_SHA256_MACOS_ARM64 "{HASH_A}")
set(MINIDUMP_STACKWALK_SHA256_WINDOWS_X64 {HASH_B})

set(LOGSQUIRL_MINIDUMP_STACKWALK "")
"""
    [pair] = uc.parse_pairs(text, "cmake/MinidumpStackwalk.cmake")
    assert pair.version == "0.27.0"
    assert [(h.name, h.value, h.line) for h in pair.hashes] == [
        ("MINIDUMP_STACKWALK_SHA256_MACOS_ARM64", HASH_A, 2), ("MINIDUMP_STACKWALK_SHA256_WINDOWS_X64", HASH_B, 3)]
    release = "https://github.com/rust-minidump/rust-minidump/releases/download/v0.27.0/"
    assert uc.download_urls(pair) == {
        "MINIDUMP_STACKWALK_SHA256_MACOS_ARM64": release + "minidump-stackwalk-aarch64-apple-darwin.tar.xz",
        "MINIDUMP_STACKWALK_SHA256_WINDOWS_X64": release + "minidump-stackwalk-x86_64-pc-windows-msvc.zip",
    }


def test_the_cmake_modules_are_scanned(tmp_path, capsys):
    write(tmp_path, "cmake/Tool.cmake", f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
set(NINJA_VERSION 1.13.2)
set(NINJA_SHA256 "{HASH_A}")
""")
    assert uc.main(["--list", "--repo-root", str(tmp_path)]) == 0
    assert "cmake/Tool.cmake:2 ninja-build/ninja 1.13.2 NINJA_SHA256" in capsys.readouterr().out


def test_the_repository_pins_have_a_url_rule(capsys):
    root = Path(__file__).resolve().parents[2]
    assert uc.main(["--list", "--repo-root", str(root)]) == 0
    assert "cmake/MinidumpStackwalk.cmake" in capsys.readouterr().out


def test_a_pair_the_url_table_does_not_know_is_an_error():
    text = f"""\
        # renovate: datasource=github-releases depName=someone/newtool
        NEWTOOL_VERSION: 1.0.0
        NEWTOOL_SHA256: {HASH_A}
"""
    [pair] = uc.parse_pairs(text, "action.yml")
    with pytest.raises(uc.UnknownPair, match="action.yml:3: no download URL for someone/newtool NEWTOOL_SHA256"):
        uc.download_urls(pair)


def write(root: Path, rel: str, text: str) -> Path:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_list_passes_when_every_pair_is_known(tmp_path, capsys):
    write(tmp_path, "docker/img/Dockerfile", f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
ENV NINJA_VERSION=1.13.2
ENV NINJA_SHA256={HASH_A}
""")
    assert uc.main(["--list", "--repo-root", str(tmp_path)]) == 0
    assert "docker/img/Dockerfile:2 ninja-build/ninja 1.13.2 NINJA_SHA256" in capsys.readouterr().out


def test_list_fails_on_a_pair_without_a_url_rule(tmp_path, capsys):
    write(tmp_path, ".github/actions/x/action.yml", f"""\
        # renovate: datasource=github-releases depName=someone/newtool
        NEWTOOL_VERSION: 1.0.0
        NEWTOOL_SHA256: {HASH_A}
""")
    assert uc.main(["--list", "--repo-root", str(tmp_path)]) == 1
    assert "no download URL for someone/newtool" in capsys.readouterr().err


def test_list_fails_on_a_checksum_renovate_cannot_see(tmp_path, capsys):
    # A pinned download without the comment would never be updated (#211).
    write(tmp_path, "packaging/linux/fetch.sh", f'TOOL_VERSION="1.0"\nTOOL_SHA256="{HASH_A}"\n')
    assert uc.main(["--list", "--repo-root", str(tmp_path)]) == 1
    assert "packaging/linux/fetch.sh:2: TOOL_SHA256 has no # renovate: comment" in capsys.readouterr().err


NINJA_URL = "https://github.com/ninja-build/ninja/releases/download/v{}/ninja-linux.zip"


def ninja_script(version: str, sha: str) -> str:
    return f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
NINJA_VERSION={version}
NINJA_SHA256={sha}
echo done
"""


def update(tmp_path, base_files: dict[str, str], content: bytes = b"new release"):
    """Runs the rewrite mode against a base whose files are base_files."""
    fetched = []

    def fetch(url: str) -> bytes:
        fetched.append(url)
        return content

    status = uc.main(["--base", "origin/master", "--repo-root", str(tmp_path)], fetch=fetch,
                     read_base=lambda ref, path: base_files.get(path) if ref == "origin/master" else None)
    return status, fetched


def test_update_rewrites_the_checksum_of_a_changed_version(tmp_path):
    content = b"new release"
    fresh = uc.hashlib.sha256(content).hexdigest()
    path = write(tmp_path, "docker/shared/install.sh", ninja_script("1.14.0", HASH_A))
    status, fetched = update(tmp_path, {"docker/shared/install.sh": ninja_script("1.13.2", HASH_A)}, content)
    assert status == 0
    assert fetched == [NINJA_URL.format("1.14.0")]
    assert path.read_text() == ninja_script("1.14.0", fresh)


def test_update_rewrites_the_checksum_of_a_pair_the_base_does_not_have(tmp_path):
    fresh = uc.hashlib.sha256(b"new release").hexdigest()
    path = write(tmp_path, "docker/shared/install.sh", ninja_script("1.14.0", HASH_A))
    assert update(tmp_path, {})[0] == 0
    assert path.read_text() == ninja_script("1.14.0", fresh)


def test_update_refuses_to_rewrite_a_mismatch_of_an_unchanged_version(tmp_path, capsys):
    # The same version now downloading other bytes is a moved release or a
    # tampered mirror, not a Renovate bump (#211).
    text = ninja_script("1.13.2", HASH_A)
    path = write(tmp_path, "docker/shared/install.sh", text)
    assert update(tmp_path, {"docker/shared/install.sh": text})[0] == 1
    assert path.read_text() == text
    err = capsys.readouterr().err
    assert "::error" in err and "ninja-build/ninja 1.13.2" in err and NINJA_URL.format("1.13.2") in err


def test_update_leaves_an_unchanged_matching_checksum_untouched(tmp_path):
    content = b"old release"
    text = ninja_script("1.13.2", uc.hashlib.sha256(content).hexdigest())
    path = write(tmp_path, "docker/shared/install.sh", text)
    status, _ = update(tmp_path, {"docker/shared/install.sh": text}, content)
    assert status == 0
    assert path.read_text() == text


def test_update_needs_a_base(tmp_path, capsys):
    write(tmp_path, "docker/shared/install.sh", ninja_script("1.14.0", HASH_A))
    with pytest.raises(SystemExit):
        uc.main(["--repo-root", str(tmp_path)], fetch=lambda url: b"")
    assert "--base" in capsys.readouterr().err


def test_check_reports_a_mismatch_without_rewriting(tmp_path, capsys):
    text = f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
ENV NINJA_VERSION=1.14.0
ENV NINJA_SHA256={HASH_A}
"""
    path = write(tmp_path, "docker/img/Dockerfile", text)
    assert uc.main(["--check", "--repo-root", str(tmp_path)], fetch=lambda url: b"other") == 1
    assert path.read_text() == text
    assert "docker/img/Dockerfile:3: NINJA_SHA256 mismatch" in capsys.readouterr().err


def test_list_fails_on_a_pair_in_a_workflow_file(tmp_path, capsys):
    # The Renovate Checksums workflow pushes with GITHUB_TOKEN, which may not
    # change .github/workflows/, so a hash there could never be refreshed (#211).
    write(tmp_path, ".github/workflows/ci.yml", f"""\
    env:
      # renovate: datasource=github-releases depName=anchore/grype
      GRYPE_VERSION: 0.118.0
      GRYPE_SHA256: {HASH_A}
""")
    assert uc.main(["--list", "--repo-root", str(tmp_path)]) == 1
    err = capsys.readouterr().err
    assert ".github/workflows/ci.yml:3: anchore/grype" in err
    assert "composite action under .github/actions/" in err


def git(root: Path, *args: str) -> None:
    uc.subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)


def test_the_base_is_read_from_git_and_an_unknown_base_is_an_error(tmp_path, capsys):
    # A base the checkout lacks (not fetched) must not make every pair look new
    # and so rewrite every hash (#211).
    write(tmp_path, "docker/shared/install.sh", ninja_script("1.13.2", HASH_A))
    git(tmp_path, "init", "-q", "-b", "master")
    git(tmp_path, "-c", "user.name=t", "-c", "user.email=t@example.com", "add", ".")
    git(tmp_path, "-c", "user.name=t", "-c", "user.email=t@example.com", "commit", "-q", "-m", "base")
    reader = uc.git_reader(tmp_path)
    assert reader("master", "docker/shared/install.sh") == ninja_script("1.13.2", HASH_A)
    assert reader("master", "docker/shared/missing.sh") is None
    with pytest.raises(uc.BaseUnavailable, match="origin/nope"):
        reader("origin/nope", "docker/shared/install.sh")

    assert uc.main(["--base", "origin/nope", "--repo-root", str(tmp_path)], fetch=lambda url: b"x") == 1
    assert "origin/nope" in capsys.readouterr().err
