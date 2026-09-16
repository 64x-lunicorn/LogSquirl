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


def test_update_rewrites_a_stale_checksum_and_leaves_the_rest(tmp_path):
    content = b"new release"
    fresh = uc.hashlib.sha256(content).hexdigest()
    path = write(tmp_path, "docker/shared/install.sh", f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
NINJA_VERSION=1.14.0
NINJA_SHA256={HASH_A}
echo done
""")
    fetched = []

    def fetch(url: str) -> bytes:
        fetched.append(url)
        return content

    assert uc.main(["--repo-root", str(tmp_path)], fetch=fetch) == 0
    assert fetched == ["https://github.com/ninja-build/ninja/releases/download/v1.14.0/ninja-linux.zip"]
    assert path.read_text() == f"""\
# renovate: datasource=github-releases depName=ninja-build/ninja
NINJA_VERSION=1.14.0
NINJA_SHA256={fresh}
echo done
"""


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
