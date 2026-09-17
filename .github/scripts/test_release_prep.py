"""Tests for release-prep.py (#312, #314): whether a pull request brings its
CHANGELOG entry, and whether a release preparation is complete. No network."""

from __future__ import annotations

import importlib.util
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


def test_a_release_preparation_that_turns_unreleased_into_the_release_passes():
    prepared = WITH_ENTRY.replace("# Unreleased", "# v26.10.0-beta2 (2026-10-01)")
    assert rp.changelog_entry_problem(base=WITH_ENTRY, head=prepared, labels=[], author="someone") is None


def test_the_command_line_reads_both_changelogs(tmp_path, capsys):
    base, head = tmp_path / "base.md", tmp_path / "head.md"
    base.write_text(RELEASED, encoding="utf-8")
    head.write_text(RELEASED, encoding="utf-8")
    args = ["changelog-entry", "--base", str(base), "--head", str(head), "--author", "someone"]
    assert rp.main([*args, "--labels", '["ui"]']) == 1
    assert capsys.readouterr().out.startswith("::error::CHANGELOG.md: add an entry")
    assert rp.main([*args, "--labels", '["no-changelog"]']) == 0
