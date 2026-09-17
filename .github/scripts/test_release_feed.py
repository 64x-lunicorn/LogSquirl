"""Tests for release-feed.py (#306, #310): what makes the update feed valid,
and how CI Release records a published release in it. No network."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_feed", Path(__file__).with_name("release-feed.py"))
rf = importlib.util.module_from_spec(_SPEC)
sys.modules["release_feed"] = rf
_SPEC.loader.exec_module(rf)

PAGE = "https://github.com/64x-lunicorn/LogSquirl/releases/tag/"


def feed():
    return {
        "ci": "26.07.0",
        "ci_url": "https://github.com/64x-lunicorn/LogSquirl/releases/latest#",
        "stable": "26.07.0", "stable_url": PAGE + "v26.07.0", "stable_build": "26.07.0.741",
        "beta": "26.07.0", "beta_url": PAGE + "v26.07.0", "beta_build": "26.07.0.741",
        "releases": ["26.06.1", "26.07.0"],
        "changelog": [
            {"version": "26.07.0", "description": "Windows dark mode icon fixes"},
            {"version": "26.10.0-beta1", "description": "Crash and Search stall fixes"},
        ],
        "stable_version": "26.07.0", "beta_version": "26.07.0",
    }


# ── check ─────────────────────────────────────────────────────────────────

def test_the_feed_on_master_is_valid():
    master = json.loads(Path(__file__).parents[2].joinpath("latest.json").read_text(encoding="utf-8"))
    assert rf.problems(master) == []


def test_a_valid_feed_has_no_problems():
    assert rf.problems(feed()) == []


@pytest.mark.parametrize("field", ["stable", "stable_url", "stable_build", "beta", "beta_url",
                                   "beta_build", "ci", "ci_url", "releases", "changelog"])
def test_a_missing_field_is_named(field):
    broken = feed()
    del broken[field]
    assert f"'{field}' is missing" in " ".join(rf.problems(broken))


@pytest.mark.parametrize("field, value, message", [
    ("stable_build", "26.07.0", "'stable_build' 26.07.0 is not YY.MM.PATCH.BUILD"),
    ("beta_build", "26.08.0.760", "'beta_build' 26.08.0.760 is not a build of 26.07.0"),
    ("stable_url", "https://example.com/v26.07.0", f"'stable_url' should be {PAGE}v26.07.0"),
    ("beta_url", PAGE + "v26.06.1", f"'beta_url' should be {PAGE}v26.07.0"),
    ("ci_url", "https://github.com/64x-lunicorn/LogSquirl/releases/latest", "'ci_url' must end in '#'"),
    ("stable", 26.07, "'stable' is not a text"),
    ("releases", "26.07.0", "'releases' is not a list of texts"),
])
def test_a_malformed_field_is_named(field, value, message):
    broken = feed()
    broken[field] = value
    assert message in rf.problems(broken)


@pytest.mark.parametrize("entry, message", [
    ({"version": "26.07.0"}, "changelog entry 1 has no description"),
    ({"description": "fixes"}, "changelog entry 1 has no version"),
    ("26.07.0", "changelog entry 1 has no version"),
])
def test_a_malformed_changelog_entry_is_named(entry, message):
    broken = feed()
    broken["changelog"][0] = entry
    assert message in rf.problems(broken)


def test_an_announced_release_must_be_listed_and_described():
    broken = feed()
    broken["releases"] = ["26.06.1"]
    broken["changelog"] = broken["changelog"][1:]
    assert rf.problems(broken) == [
        "'stable' 26.07.0 is not in 'releases'",
        "'stable' 26.07.0 has no changelog entry",
        "'beta' 26.07.0 is not in 'releases'",
        "'beta' 26.07.0 has no changelog entry",
    ]


def test_text_that_is_not_a_feed_object_is_one_problem():
    assert rf.problems([]) == ["the feed is not a JSON object"]


def test_the_command_line_checks_a_file(tmp_path, capsys):
    path = tmp_path / "latest.json"
    path.write_text("{ not json", encoding="utf-8")
    assert rf.main(["check", "--feed", str(path)]) == 1
    assert capsys.readouterr().out.startswith("::error::latest.json is not valid JSON")
    path.write_text(json.dumps(feed()), encoding="utf-8")
    assert rf.main(["check", "--feed", str(path)]) == 0


# ── record ────────────────────────────────────────────────────────────────

def test_a_beta_is_recorded_in_the_beta_fields_and_the_release_list():
    recorded = rf.record(feed(), tag="v26.10.0-beta1", build="26.10.0.760")
    assert recorded["beta"] == recorded["beta_version"] == "26.10.0-beta1"
    assert recorded["beta_url"] == PAGE + "v26.10.0-beta1"
    assert recorded["beta_build"] == "26.10.0.760"
    assert recorded["releases"] == ["26.06.1", "26.07.0", "26.10.0-beta1"]
    assert (recorded["stable"], recorded["ci"]) == ("26.07.0", "26.07.0")


def test_a_stable_release_also_sets_the_field_old_installations_read():
    stable = feed()
    stable["changelog"].append({"version": "26.10.0", "description": "Stable"})
    recorded = rf.record(stable, tag="v26.10.0", build="26.10.0.790")
    assert (recorded["stable"], recorded["stable_build"], recorded["ci"]) == ("26.10.0", "26.10.0.790", "26.10.0")
    assert recorded["beta"] == "26.07.0"


def test_recording_a_release_again_changes_nothing():
    once = rf.record(feed(), tag="v26.10.0-beta1", build="26.10.0.760")
    assert rf.record(copy.deepcopy(once), tag="v26.10.0-beta1", build="26.10.0.760") == once


def test_an_older_release_does_not_replace_a_newer_one():
    newer = rf.record(feed(), tag="v26.10.0-beta1", build="26.10.0.760")
    assert rf.record(copy.deepcopy(newer), tag="v26.07.0", build="26.07.0.700") == newer
    newer_build = copy.deepcopy(newer)
    newer_build["beta_build"] = "26.10.0.1000"
    assert rf.record(copy.deepcopy(newer_build), tag="v26.10.0-beta1", build="26.10.0.999") == newer_build


def test_a_release_without_a_changelog_entry_is_not_recorded():
    with pytest.raises(rf.ReleaseError, match="'beta' 26.11.0-beta1 has no changelog entry"):
        rf.record(feed(), tag="v26.11.0-beta1", build="26.11.0.800")


def test_a_build_of_another_version_is_not_recorded():
    with pytest.raises(rf.ReleaseError, match="'beta_build' 26.11.0.800 is not a build of 26.10.0-beta1"):
        rf.record(feed(), tag="v26.10.0-beta1", build="26.11.0.800")


def test_the_command_line_records_in_place_and_keeps_the_format(tmp_path):
    path = tmp_path / "latest.json"
    path.write_text(json.dumps(feed(), indent=2) + "\n", encoding="utf-8")
    assert rf.main(["record", "--feed", str(path), "--tag", "v26.10.0-beta1", "--build", "26.10.0.760"]) == 0
    text = path.read_text(encoding="utf-8")
    assert text == json.dumps(rf.record(feed(), tag="v26.10.0-beta1", build="26.10.0.760"), indent=2) + "\n"


def test_the_prepare_check_needs_a_changelog_entry_for_the_tag(tmp_path, capsys):
    path = tmp_path / "latest.json"
    path.write_text(json.dumps(feed()), encoding="utf-8")
    assert rf.main(["check-tag", "--feed", str(path), "--tag", "v26.10.0-beta1"]) == 0
    assert rf.main(["check-tag", "--feed", str(path), "--tag", "v26.11.0"]) == 1
    assert "no changelog entry for 26.11.0" in capsys.readouterr().out
