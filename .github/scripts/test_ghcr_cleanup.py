"""Tests for ghcr-cleanup.py (#230): which versions of a build image package
are deleted. No network, no gh."""

from __future__ import annotations

import importlib.util
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location("ghcr_cleanup", Path(__file__).with_name("ghcr-cleanup.py"))
gc = importlib.util.module_from_spec(_SPEC)
sys.modules["ghcr_cleanup"] = gc
_SPEC.loader.exec_module(gc)

NOW = datetime(2026, 9, 17, 12, 0, tzinfo=timezone.utc)
CURRENT = "c" * 64
OLD_HASH = "0" * 64
RECENT_HASH = "1" * 64
COMMIT_TAG = "d27f5f885ea8d9f53eb15720a67fcaf882cac67a"


def digest(n: int) -> str:
    return f"sha256:{n:064x}"


def version(id_: int, tags: list[str], age_days: float, name: str | None = None) -> dict:
    stamp = (NOW - timedelta(days=age_days)).isoformat().replace("+00:00", "Z")
    return {"id": id_, "name": name or digest(id_), "created_at": stamp, "updated_at": stamp,
            "metadata": {"package_type": "container", "container": {"tags": tags}}}


def signature_tag(n: int) -> str:
    return f"sha256-{n:064x}"


def plan(versions, children=None, **kwargs):
    kwargs.setdefault("protected_tags", {"latest", CURRENT})
    kwargs.setdefault("retention", timedelta(days=30))
    kwargs.setdefault("min_age", timedelta(days=2))
    kwargs.setdefault("now", NOW)
    return gc.plan(versions, children=lambda d: (children or {}).get(d, []), **kwargs)


def deleted_ids(result) -> set[int]:
    return {v["id"] for v, _ in result.delete}


def test_the_current_image_and_latest_are_kept_with_their_children():
    versions = [
        version(1, ["latest", CURRENT], 90),
        version(2, [], 90),  # platform manifest of 1
        version(3, [], 90),  # attestation manifest of 1
    ]
    result = plan(versions, children={digest(1): [digest(2), digest(3)]})
    assert deleted_ids(result) == set()


def test_old_commit_and_hash_tags_go_with_their_children():
    versions = [
        version(1, ["latest", CURRENT], 90),
        version(10, [COMMIT_TAG], 200),
        version(11, [OLD_HASH], 60),
        version(12, [], 60),  # child of 11, never resolved: 11 is not kept
    ]
    result = plan(versions)
    assert deleted_ids(result) == {10, 11, 12}


def test_a_hash_tag_within_the_retention_is_kept_for_open_pull_requests():
    versions = [
        version(1, ["latest", CURRENT], 5),
        version(20, [RECENT_HASH], 10),
        version(21, [], 10),
    ]
    result = plan(versions, children={digest(20): [digest(21)]})
    assert deleted_ids(result) == set()


def test_a_commit_tag_is_not_protected_by_the_retention():
    # The old tag scheme: no CI run resolves an image by commit any more.
    versions = [version(1, ["latest", CURRENT], 5), version(10, [COMMIT_TAG], 10)]
    assert deleted_ids(plan(versions)) == {10}


def test_a_signature_goes_with_its_image_and_stays_with_a_kept_one():
    versions = [
        version(1, ["latest", CURRENT], 90),
        version(2, [signature_tag(1)], 90),
        version(3, [], 90),  # signature bundle, child of the signature index 2
        version(11, [OLD_HASH], 60),
        version(12, [signature_tag(11)], 60),
        version(13, [], 60),
        version(14, [signature_tag(999)], 60),  # signs an image already gone
    ]
    result = plan(versions, children={digest(2): [digest(3)], digest(12): [digest(13)]})
    assert deleted_ids(result) == {11, 12, 13, 14}


def test_nothing_younger_than_the_minimum_age_is_deleted():
    # A push in progress uploads its manifests before the index gets its tag.
    versions = [
        version(1, ["latest", CURRENT], 90),
        version(30, [], 0.5),
        version(31, [OLD_HASH], 1),
    ]
    assert deleted_ids(plan(versions)) == set()


def test_untagged_leftovers_are_deleted():
    versions = [version(1, ["latest", CURRENT], 90), version(40, [], 30)]
    result = plan(versions)
    assert deleted_ids(result) == {40}
    assert "untagged" in result.delete[0][1]


def test_without_a_protected_version_nothing_is_deleted():
    # A package whose latest and current tags are both missing is not in the
    # state this script expects; deleting by the other rules could empty it.
    versions = [version(10, [COMMIT_TAG], 200), version(11, [], 200)]
    with pytest.raises(gc.CleanupError, match="no version tagged"):
        plan(versions)


def test_an_unresolvable_kept_image_stops_the_plan():
    def children(d):
        raise OSError("registry down")

    versions = [version(1, ["latest", CURRENT], 90), version(40, [], 30)]
    with pytest.raises(OSError):
        gc.plan(versions, children=children, protected_tags={"latest", CURRENT},
                retention=timedelta(days=30), min_age=timedelta(days=2), now=NOW)


def test_image_arguments_are_parsed():
    assert gc.parse_image("logsquirl-ubuntu-noble=docker/ubuntu24.04") == ("logsquirl-ubuntu-noble", "docker/ubuntu24.04")
    with pytest.raises(ValueError):
        gc.parse_image("logsquirl-ubuntu-noble")


def test_a_failed_delete_is_reported_and_the_rest_still_deleted(monkeypatch, capsys):
    versions = [version(1, ["latest", CURRENT], 1), version(2, [OLD_HASH], 90), version(3, [COMMIT_TAG], 90)]
    deleted = []

    def gh_api(*args):
        if args[0] == "--paginate":
            return json.dumps([versions])
        if args[-1].endswith("/versions/2"):
            raise gc.subprocess.CalledProcessError(1, ["gh", "api"])
        deleted.append(args[-1])
        return ""

    monkeypatch.setattr(gc, "gh_api", gh_api)
    monkeypatch.setattr(gc, "inputs_hash", lambda directory: CURRENT)
    monkeypatch.setattr(gc, "registry_children", lambda owner, package, token: lambda d: [])
    monkeypatch.setattr(gc, "datetime", type("FixedNow", (datetime,), {"now": staticmethod(lambda tz=None: NOW)}))

    status = gc.main(["--owner", "me", "--image", "logsquirl-x=docker/x"])

    assert status == 1
    assert deleted == ["/users/me/packages/container/logsquirl-x/versions/3"]
    out = capsys.readouterr().out
    assert "::error::logsquirl-x: deleting" in out
    assert "deleted 1 of 3 versions" in out
