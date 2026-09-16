"""Tests for check-action-allowlist.py (#201): how an action reference inside
a workflow or another action resolves. No network, no gh."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "check_action_allowlist", Path(__file__).with_name("check-action-allowlist.py"))
aa = importlib.util.module_from_spec(_SPEC)
sys.modules["check_action_allowlist"] = aa
_SPEC.loader.exec_module(aa)

REMOTE = ("jurplel", "install-qt-action", "d325aaf2a8baeeda41ad0b5d39f84a6af9bcf005")


def test_a_local_action_of_this_repository_is_not_followed_remotely():
    assert aa.resolve("./.github/actions/agent-setup", None) is None


def test_dollar_slash_inside_a_remote_action_names_that_action_repository():
    assert aa.resolve("$/action", REMOTE) == ("jurplel", "install-qt-action", "action", REMOTE[2])


def test_dot_slash_inside_a_remote_action_is_the_callers_workspace():
    # At runtime `./` resolves against the calling workflow's checkout, i.e.
    # an action of this repository, which the scan already reads from its own
    # files; it is not a directory of the remote action's repository.
    assert aa.resolve("./setup", REMOTE) is None


def test_a_remote_reference_is_split_into_owner_repo_path_and_ref():
    assert aa.resolve("github/codeql-action/upload-sarif@abc", None) == ("github", "codeql-action",
                                                                         "upload-sarif", "abc")
    assert aa.resolve("docker://alpine:3", REMOTE) is None
