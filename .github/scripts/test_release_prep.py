"""Tests for release-prep.py (#312, #314): whether a pull request brings its
CHANGELOG entry, and whether a release preparation is complete. No network."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_prep", Path(__file__).with_name("release-prep.py"))
rp = importlib.util.module_from_spec(_SPEC)
sys.modules["release_prep"] = rp
_SPEC.loader.exec_module(rp)

RELEASED = """\
# v26.10.0-beta1 (2026-09-17)

## Changes

- **System theme**: Follows the operating system.
"""

WITH_ENTRY = """\
# Unreleased

## Bug fixes

- **Zoom**: Keeps the font.

---

""" + RELEASED


# ── CHANGELOG entry of a pull request ─────────────────────────────────────

def test_a_pull_request_that_adds_an_unreleased_entry_passes():
    assert rp.changelog_entry_problem(base=RELEASED, head=WITH_ENTRY, labels=[], author="someone") is None


def test_a_pull_request_that_adds_to_an_existing_unreleased_section_passes():
    more = WITH_ENTRY.replace("- **Zoom**: Keeps the font.", "- **Zoom**: Keeps the font.\n- **Marks**: Stay.")
    assert rp.changelog_entry_problem(base=WITH_ENTRY, head=more, labels=[], author="someone") is None


def test_a_pull_request_without_an_entry_is_told_both_ways_to_pass():
    problem = rp.changelog_entry_problem(base=WITH_ENTRY, head=WITH_ENTRY, labels=["ui"], author="someone")
    assert problem == ("CHANGELOG.md: add an entry under '# Unreleased' for this change, "
                       "or add the 'no-changelog' label if it needs none.")


def test_a_change_to_a_released_section_is_not_an_entry():
    edited = RELEASED.replace("Follows", "Now follows")
    assert rp.changelog_entry_problem(base=RELEASED, head=edited, labels=[], author="someone") is not None


def test_the_no_changelog_label_passes():
    assert rp.changelog_entry_problem(base=RELEASED, head=RELEASED, labels=["no-changelog"], author="someone") is None


@pytest.mark.parametrize("bot", ["dependabot[bot]", "renovate[bot]"])
def test_dependency_bots_pass_without_an_entry(bot):
    assert rp.changelog_entry_problem(base=RELEASED, head=RELEASED, labels=[], author=bot) is None


def test_the_update_feed_pull_request_of_a_release_passes_without_an_entry():
    assert rp.changelog_entry_problem(base=RELEASED, head=RELEASED, labels=[], author="someone",
                                      branch="feed/v26.10.0-beta1") is None


def test_another_branch_still_needs_an_entry():
    assert rp.changelog_entry_problem(base=RELEASED, head=RELEASED, labels=[], author="someone",
                                      branch="feature/feed") is not None


def test_a_release_preparation_that_turns_unreleased_into_the_release_passes():
    prepared = WITH_ENTRY.replace("# Unreleased", "# v26.10.0-beta2 (2026-10-01)")
    assert rp.changelog_entry_problem(base=WITH_ENTRY, head=prepared, labels=[], author="someone") is None


def test_a_new_top_heading_that_is_not_a_release_is_not_an_entry():
    stray = "# Notes\n\nSomething.\n\n---\n\n" + RELEASED
    assert rp.changelog_entry_problem(base=RELEASED, head=stray, labels=[], author="someone") is not None


def test_the_command_line_reads_both_changelogs(tmp_path, capsys):
    base, head = tmp_path / "base.md", tmp_path / "head.md"
    base.write_text(RELEASED, encoding="utf-8")
    head.write_text(RELEASED, encoding="utf-8")
    args = ["changelog-entry", "--base", str(base), "--head", str(head), "--author", "someone"]
    assert rp.main([*args, "--labels", '["ui"]']) == 1
    assert capsys.readouterr().out.startswith("::error::CHANGELOG.md: add an entry")
    assert rp.main([*args, "--labels", '["no-changelog"]']) == 0


# ── consistency of a release preparation ──────────────────────────────────

def cmake(version):
    return f"project(\n  logsquirl\n  VERSION {version}\n  LANGUAGES C CXX ASM\n)\n"


def news(tmp_path, **pages):
    directory = tmp_path / "news"
    directory.mkdir()
    for name, version in pages.items():
        (directory / f"{name}.md").write_text(
            f"---\ntitle: T\nrelease:\n  version: {version}\n  date: 2026-09-17\n  channel: beta\n---\n",
            encoding="utf-8")
    (directory / "index.mdx").write_text("---\ntitle: Releases\n---\n", encoding="utf-8")
    return directory


FEED = {"changelog": [{"version": "26.07.0", "description": "Fixes"},
                      {"version": "26.10.0-beta1", "description": "Crash fixes"}]}


def test_a_complete_beta_preparation_passes(tmp_path):
    assert rp.release_preparation_problems(
        base_cmake=cmake("26.07.0"), head_cmake=cmake("26.10.0"), changelog=RELEASED, feed=FEED,
        news_dir=news(tmp_path, **{"release-26-10": "26.10.0-beta1"})) == []


def test_a_pull_request_that_keeps_the_version_is_not_a_release_preparation(tmp_path):
    assert rp.release_preparation_problems(
        base_cmake=cmake("26.10.0"), head_cmake=cmake("26.10.0"), changelog=WITH_ENTRY, feed={},
        news_dir=news(tmp_path)) == []


def test_every_missing_piece_is_named_with_the_expected_release(tmp_path):
    assert rp.release_preparation_problems(
        base_cmake=cmake("26.10.0"), head_cmake=cmake("26.11.0"), changelog=WITH_ENTRY, feed=FEED,
        news_dir=news(tmp_path, **{"release-26-10": "26.10.0-beta1"})) == [
        "CHANGELOG.md: the version is now 26.11.0, but the top section is '# Unreleased'; "
        "turn it into '# v26.11.0 (YYYY-MM-DD)' or a pre-release such as '# v26.11.0-beta1 (YYYY-MM-DD)'",
        "latest.json: no changelog entry for 26.11.0 or a pre-release of it",
        "website: no release page with 'version: 26.11.0' or a pre-release of it",
    ]


def test_the_release_named_by_the_changelog_is_the_one_expected_elsewhere(tmp_path):
    prepared = RELEASED.replace("v26.10.0-beta1", "v26.10.0-beta2")
    assert rp.release_preparation_problems(
        base_cmake=cmake("26.07.0"), head_cmake=cmake("26.10.0"), changelog=prepared, feed=FEED,
        news_dir=news(tmp_path, **{"release-26-10": "26.10.0-beta1"})) == [
        "latest.json: no changelog entry for 26.10.0-beta2",
        "website: no release page with 'version: 26.10.0-beta2'",
    ]


def test_the_preparation_of_this_repository_passes():
    root = Path(__file__).parents[2]
    head_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    # The entries of pull requests after the last preparation sit above it
    # under Unreleased; the preparation itself is the release section below.
    if changelog.startswith(rp.UNRELEASED + "\n"):
        changelog = changelog[changelog.index("\n# v"):].lstrip("\n")
    assert rp.release_preparation_problems(
        base_cmake=cmake("26.07.0"), head_cmake=head_cmake,
        changelog=changelog,
        feed=json.loads((root / "latest.json").read_text(encoding="utf-8")),
        news_dir=root / "website/src/content/docs/news") == []
