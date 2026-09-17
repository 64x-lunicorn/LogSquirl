"""Tests for release-build.py (#197, #221): which CI Build run a release may
publish, which of its artifacts it downloads, and whether the downloaded build
carries the tag's version. No network, no gh."""

from __future__ import annotations

import importlib.util
import io
import json
import plistlib
import sys
import tarfile
import zipfile
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "release_build", Path(__file__).with_name("release-build.py"))
rb = importlib.util.module_from_spec(_SPEC)
sys.modules["release_build"] = rb
_SPEC.loader.exec_module(rb)

REPO = "64x-lunicorn/LogSquirl"
COMMIT = "39fa5b49fb78c7737dac9fb2da8b41a63206b182"
OTHER = "edff6ea90cd59cdd3c74107a8bda72ea9c264742"
VERSION = "26.08.0.1066"


def run(**overrides):
    base = {
        "id": 35187407744, "run_number": 349, "run_attempt": 1,
        "path": ".github/workflows/ci-build.yml", "event": "push", "head_branch": "master",
        "head_sha": COMMIT, "status": "completed", "conclusion": "success",
        "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO},
        "html_url": "https://github.com/64x-lunicorn/LogSquirl/actions/runs/35187407744",
    }
    base.update(overrides)
    return base


def artifact(name, id_, **overrides):
    base = {"id": id_, "name": name, "expired": False, "digest": "sha256:" + "0" * 64,
            "workflow_run": {"id": 35187407744, "head_sha": COMMIT, "head_branch": "master"}}
    base.update(overrides)
    return base


def all_artifacts():
    extra = [artifact("test-results-noble", 1)]
    return extra + [artifact(name, 100 + i) for i, name in enumerate(rb.REQUIRED_ARTIFACTS)]


# ── tag ─────────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("tag, base, pre", [
    ("v26.04.2", "26.04.2", False),
    ("v26.05.0-beta1", "26.05.0", True),
    ("v26.03.1-beta.2", "26.03.1", True),
    ("v26.08.0-rc1", "26.08.0", True),
])
def test_a_release_tag_names_its_base_version_and_whether_it_is_a_prerelease(tag, base, pre):
    assert rb.parse_tag(tag) == (base, pre)


@pytest.mark.parametrize("tag", ["", "26.04.2", "v26.04", "v26.04.2-foo1", "v26.04.2\nX=1", "master"])
def test_anything_else_is_not_a_release_tag(tag):
    with pytest.raises(rb.ReleaseError, match="not a release tag"):
        rb.parse_tag(tag)


# ── run given by ci-run-id ─────────────────────────────────────────────────

def test_a_successful_master_push_run_of_the_tagged_commit_is_accepted():
    rb.check_run(run(), commit=COMMIT, repository=REPO)


@pytest.mark.parametrize("overrides, message", [
    ({"path": ".github/workflows/ci-release.yml"}, "not the CI Build workflow"),
    ({"conclusion": "failure"}, "did not succeed"),
    ({"status": "in_progress", "conclusion": None}, "did not succeed"),
    ({"event": "pull_request", "head_branch": "feature"}, "'pull_request' on branch 'feature'"),
    ({"event": "workflow_dispatch"}, "'workflow_dispatch' on branch 'master'"),
    ({"head_branch": "release"}, "'push' on branch 'release'"),
    ({"head_repository": {"full_name": "someone/LogSquirl"}}, "someone/LogSquirl"),
    ({"head_sha": OTHER}, "built commit " + OTHER),
])
def test_any_other_run_is_rejected_with_its_branch_and_event(overrides, message):
    with pytest.raises(rb.ReleaseError, match=message) as err:
        rb.check_run(run(**overrides), commit=COMMIT, repository=REPO)
    assert str(run()["id"]) in str(err.value)


# ── run found for the commit ───────────────────────────────────────────────

def test_the_newest_successful_run_of_the_commit_is_chosen():
    runs = [run(id=1, run_number=10), run(id=3, run_number=12, conclusion="failure"),
            run(id=2, run_number=11)]
    assert rb.select_run(runs, commit=COMMIT, repository=REPO)["id"] == 2


def test_runs_of_other_commits_events_or_branches_are_never_chosen():
    runs = [run(id=1, head_sha=OTHER), run(id=2, event="pull_request"), run(id=3, head_branch="x"),
            run(id=4, path=".github/workflows/zizmor.yml")]
    with pytest.raises(rb.ReleaseError, match="No CI Build run for a push to master built commit"):
        rb.select_run(runs, commit=COMMIT, repository=REPO)


def test_without_any_run_the_error_explains_how_a_commit_gets_one():
    with pytest.raises(rb.ReleaseError, match=r"skip ci|paths-ignore"):
        rb.select_run([], commit=COMMIT, repository=REPO)


def test_a_commit_whose_runs_did_not_succeed_names_them_and_their_state():
    runs = [run(id=7, status="in_progress", conclusion=None), run(id=8, conclusion="failure")]
    with pytest.raises(rb.ReleaseError) as err:
        rb.select_run(runs, commit=COMMIT, repository=REPO)
    text = str(err.value)
    assert "no successful" in text and "in_progress" in text and "failure" in text


# ── artifacts ──────────────────────────────────────────────────────────────

def test_exactly_the_release_artifacts_of_the_run_are_selected():
    ids = rb.select_artifacts(all_artifacts(), run_id=35187407744, commit=COMMIT)
    assert set(ids) == set(rb.REQUIRED_ARTIFACTS)
    assert "test-results-noble" not in ids


def test_a_missing_artifact_stops_the_release():
    arts = [a for a in all_artifacts() if a["name"] != "packages-mac-arm64"]
    with pytest.raises(rb.ReleaseError, match="packages-mac-arm64"):
        rb.select_artifacts(arts, run_id=35187407744, commit=COMMIT)


def test_an_expired_artifact_stops_the_release():
    arts = [artifact(a["name"], a["id"], expired=a["name"] == "sbom-base") for a in all_artifacts()]
    with pytest.raises(rb.ReleaseError, match="sbom-base.*expired"):
        rb.select_artifacts(arts, run_id=35187407744, commit=COMMIT)


@pytest.mark.parametrize("workflow_run", [
    {"id": 1, "head_sha": COMMIT},
    {"id": 35187407744, "head_sha": OTHER},
])
def test_an_artifact_of_another_run_or_commit_stops_the_release(workflow_run):
    arts = all_artifacts()
    arts[-1]["workflow_run"] = workflow_run
    with pytest.raises(rb.ReleaseError, match=arts[-1]["name"]):
        rb.select_artifacts(arts, run_id=35187407744, commit=COMMIT)


def test_of_an_artifact_uploaded_again_by_a_rerun_the_latest_is_selected():
    arts = all_artifacts() + [artifact("packages-noble", 999)]
    assert rb.select_artifacts(arts, run_id=35187407744, commit=COMMIT)["packages-noble"] == 999


# ── downloaded build ───────────────────────────────────────────────────────

def write_build(root: Path, *, version=VERSION, commit=COMMIT, sbom_version=None):
    (root / "logsquirl_version").mkdir()
    (root / "logsquirl_version/logsquirl_version.txt").write_text(version + "\n")
    (root / "sbom-base").mkdir()
    (root / "sbom-base/logsquirl-sbom-base.cdx.json").write_text(json.dumps({"metadata": {"component": {
        "version": sbom_version or version,
        "properties": [{"name": "logsquirl:git-commit", "value": commit}]}}}))
    for directory, name in [("packages-noble", f"logsquirl-{version}-noble.deb"),
                            ("packages-oracle", f"logsquirl-{version}-oracle.rpm"),
                            ("packages-fedora", f"logsquirl-{version}-fedora.rpm"),
                            ("packages-appimage", f"logsquirl-{version}-x86_64.AppImage"),
                            ("packages-windows-x64", "logsquirl-win-x64-setup.exe"),
                            ("packages-mac-arm64", "logsquirl-mac-arm64.dmg")]:
        (root / directory).mkdir()
        (root / directory / name).write_bytes(b"x")
    write_portable(root, exe=b"MZ...\0" + version.encode() + b"\0...")
    write_mac_app(root, bundle_version=version.rsplit(".", 1)[0],
                  binary=b"\xcf\xfa\xed\xfe\0" + version.encode() + b"\0")


def write_portable(root: Path, *, exe: bytes | None):
    with zipfile.ZipFile(root / "packages-windows-x64/logsquirl-win-x64-portable.zip", "w") as z:
        z.writestr("platforms/qwindows.dll", b"MZ")
        if exe is not None:
            z.writestr("logsquirl_portable.exe", exe)


def write_mac_app(root: Path, *, bundle_version: str | None, binary: bytes | None):
    def add(tar, name, data):
        info = tarfile.TarInfo(name)
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))

    with tarfile.open(root / "packages-mac-arm64/logsquirl-arm64.app.tar.gz", "w:gz") as tar:
        if bundle_version is not None:
            add(tar, "logsquirl.app/Contents/Info.plist", plistlib.dumps(
                {"CFBundleVersion": bundle_version, "CFBundleShortVersionString": "26.08"}))
        if binary is not None:
            add(tar, "logsquirl.app/Contents/MacOS/logsquirl", binary)


def test_a_build_of_the_tagged_commit_with_the_tag_version_passes(tmp_path):
    write_build(tmp_path)
    assert rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT) == VERSION
    assert rb.check_build(tmp_path, tag="v26.08.0-beta1", commit=COMMIT) == VERSION


def test_a_build_whose_version_differs_from_the_tag_says_to_bump_cmakelists(tmp_path):
    write_build(tmp_path, version="26.07.0.1066")
    with pytest.raises(rb.ReleaseError, match=r"26\.07\.0\.1066.*v26\.08\.0.*CMakeLists\.txt"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


@pytest.mark.parametrize("text", ["26.08.0", "26.08.0.x", "", "26.08.0.1\n26.08.0.2"])
def test_a_malformed_version_file_stops_the_release(tmp_path, text):
    write_build(tmp_path)
    (tmp_path / "logsquirl_version/logsquirl_version.txt").write_text(text)
    with pytest.raises(rb.ReleaseError, match="version"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


def test_an_sbom_of_another_commit_stops_the_release(tmp_path):
    write_build(tmp_path, commit=OTHER)
    with pytest.raises(rb.ReleaseError, match=OTHER):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


def test_an_sbom_of_another_version_stops_the_release(tmp_path):
    write_build(tmp_path, sbom_version="26.08.0.1")
    with pytest.raises(rb.ReleaseError, match=r"26\.08\.0\.1\b"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


def test_a_linux_package_without_the_version_in_its_name_stops_the_release(tmp_path):
    write_build(tmp_path)
    (tmp_path / f"packages-fedora/logsquirl-{VERSION}-fedora.rpm").rename(
        tmp_path / "packages-fedora/logsquirl-26.08.0.1-fedora.rpm")
    with pytest.raises(rb.ReleaseError, match="packages-fedora"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


def test_an_empty_package_directory_stops_the_release(tmp_path):
    write_build(tmp_path)
    for f in (tmp_path / "packages-mac-arm64").iterdir():
        f.unlink()
    with pytest.raises(rb.ReleaseError, match="packages-mac-arm64"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


# The Windows and macOS package names carry no version; the binaries in them do.

@pytest.mark.parametrize("exe", [None, b"MZ 26.08.0.1 ", b"MZ 26.08.0.10661 ", b"MZ 126.08.0.1066 "])
def test_a_windows_executable_without_the_exact_version_stops_the_release(tmp_path, exe):
    write_build(tmp_path)
    write_portable(tmp_path, exe=exe)
    with pytest.raises(rb.ReleaseError, match="logsquirl-win-x64-portable.zip"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


def test_a_missing_portable_zip_stops_the_release(tmp_path):
    write_build(tmp_path)
    (tmp_path / "packages-windows-x64/logsquirl-win-x64-portable.zip").unlink()
    with pytest.raises(rb.ReleaseError, match="logsquirl-win-x64-portable.zip"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


@pytest.mark.parametrize("bundle_version, binary", [
    ("26.07.0", b"26.08.0.1066"),
    (None, b"26.08.0.1066"),
    ("26.08.0", b"26.08.0.1065"),
    ("26.08.0", None),
])
def test_a_mac_app_without_the_version_stops_the_release(tmp_path, bundle_version, binary):
    write_build(tmp_path)
    write_mac_app(tmp_path, bundle_version=bundle_version, binary=binary)
    with pytest.raises(rb.ReleaseError, match="logsquirl-arm64.app.tar.gz"):
        rb.check_build(tmp_path, tag="v26.08.0", commit=COMMIT)


# ── robustness ─────────────────────────────────────────────────────────────

def test_every_page_of_a_listing_is_read():
    pages = {1: {"total_count": 3, "artifacts": [{"id": 1}, {"id": 2}]},
             2: {"total_count": 3, "artifacts": [{"id": 3}]}}
    calls = []

    def fetch(path):
        calls.append(path)
        return pages.get(int(path.rsplit("page=", 1)[1]), {"total_count": 3, "artifacts": []})

    items = rb.paginate(fetch, "repos/x/actions/runs/1/artifacts", "artifacts", per_page=2)
    assert [a["id"] for a in items] == [1, 2, 3]
    assert calls == ["repos/x/actions/runs/1/artifacts?per_page=2&page=1",
                     "repos/x/actions/runs/1/artifacts?per_page=2&page=2"]


def test_a_listing_path_with_a_query_gets_its_page_appended():
    seen = []
    rb.paginate(lambda p: seen.append(p) or {"total_count": 0, "runs": []}, "runs?event=push", "runs")
    assert seen == ["runs?event=push&per_page=100&page=1"]


def test_a_listing_that_ends_early_stops_instead_of_looping():
    seen = []
    items = rb.paginate(lambda p: seen.append(p) or {"total_count": 50, "runs": []}, "runs", "runs")
    assert items == [] and len(seen) == 1


def test_an_artifact_without_an_id_is_an_error_not_a_crash():
    arts = all_artifacts()
    del arts[-1]["id"]
    with pytest.raises(rb.ReleaseError, match="unexpected"):
        rb.select_artifacts(arts, run_id=35187407744, commit=COMMIT)


def test_a_run_without_an_id_is_an_error_not_a_crash():
    bad = run()
    del bad["id"]
    with pytest.raises(rb.ReleaseError, match="unexpected"):
        rb.select_run([bad], commit=COMMIT, repository=REPO)


def test_the_command_line_turns_an_unexpected_response_into_an_annotation(monkeypatch, capsys):
    monkeypatch.delenv("GITHUB_OUTPUT", raising=False)
    no_id = run()
    del no_id["id"]
    monkeypatch.setattr(rb, "_gh_api", lambda path: no_id)
    assert rb.main(["find-run", "--repository", REPO, "--commit", COMMIT, "--run-id", "5"]) == 1
    assert capsys.readouterr().out.startswith("::error::")
