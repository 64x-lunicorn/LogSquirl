#!/usr/bin/env python3
"""Finds and checks the CI Build a release publishes, and its notes (#197, #221, #307).

A release does not build. It publishes the packages of the successful CI Build
run for a push to master that built the tagged commit, signed in CI Release.
This script is the one place that decides which run and which artifacts that
is, and whether the downloaded build is the one the tag names:

  check-tag  --tag T
      T is a release tag (vX.Y.Z or vX.Y.Z-alphaN/betaN/rcN).
      Outputs: tag, base_version, is_prerelease
  find-run   --repository R --commit SHA [--run-id ID]
      Without ID, the newest successful CI Build push run on master for SHA;
      with ID, that run, rejected unless it is one. Either way the release
      artifacts of the run must exist, be unexpired and belong to that run
      and commit.
      Outputs: run_id, artifact_ids (comma-separated, for actions/download-artifact,
      which also checks each download against the artifact's digest)
  check-build --tag T --commit SHA [--root DIR]
      The downloaded artifacts in DIR (one directory per artifact) come from
      SHA (the SBOM records the commit) and carry T's version: the version
      file, the SBOM, the Linux package names, logsquirl_portable.exe in the
      Windows portable zip, and the macOS app's binary and CFBundleVersion. The base
      version of a CI Build is project(VERSION) in CMakeLists.txt, so a tag
      must name the version its commit declares.
      Outputs: version
  release-notes --tag T --out FILE [--changelog CHANGELOG.md]
      Writes T's CHANGELOG section, without its heading, to FILE; a
      CHANGELOG without a section for T rejects the release (#307).

Outputs are appended to $GITHUB_OUTPUT as name=value lines, or printed when
it is not set. A rejected release exits 1 with an ::error:: annotation.
find-run needs `gh` authenticated with actions: read.
"""

from __future__ import annotations

import argparse
import json
import os
import plistlib
import re
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

WORKFLOW_PATH = ".github/workflows/ci-build.yml"
RELEASE_BRANCH = "master"

# The artifacts CI Release downloads; test results and the like stay behind.
REQUIRED_ARTIFACTS = (
    "logsquirl_version",
    "sbom-base",
    "packages-noble",
    "packages-oracle",
    "packages-fedora",
    "packages-appimage",
    "packages-windows-x64",
    "packages-mac-arm64",
)
# Directory -> file name each Linux package must have, {version} filled in.
VERSIONED_PACKAGES = {
    "packages-noble": "logsquirl-{version}-noble.deb",
    "packages-oracle": "logsquirl-{version}-oracle.rpm",
    "packages-fedora": "logsquirl-{version}-fedora.rpm",
    "packages-appimage": "logsquirl-{version}-x86_64.AppImage",
}
# Windows and macOS package names carry no version, so the binaries inside
# are checked: the version string compiled in (src/logsquirl_version) and the
# app bundle's CFBundleVersion (YY.MM.PATCH).
WINDOWS_PORTABLE = "packages-windows-x64/logsquirl-win-x64-portable.zip"
WINDOWS_EXE = "logsquirl_portable.exe"
MAC_APP = "packages-mac-arm64/logsquirl-arm64.app.tar.gz"
MAC_INFO_PLIST = "logsquirl.app/Contents/Info.plist"
MAC_BINARY = "logsquirl.app/Contents/MacOS/logsquirl"
VERSION_FILE = "logsquirl_version/logsquirl_version.txt"
SBOM_FILE = "sbom-base/logsquirl-sbom-base.cdx.json"

# Accepts the historical formats: v26.04.2, v26.05.0-beta1, v26.03.1-beta.2
_TAG = re.compile(r"v([0-9]+\.[0-9]+\.[0-9]+)(-(?:alpha|beta|rc)\.?[0-9]+)?")
_VERSION = re.compile(r"([0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+")


class ReleaseError(Exception):
    pass


def parse_tag(tag: str) -> tuple[str, bool]:
    """Returns (base version, is prerelease) of a release tag."""
    m = _TAG.fullmatch(tag)
    if not m:
        raise ReleaseError(f"Tag {tag!r} is not a release tag "
                           "(expected vX.Y.Z or vX.Y.Z-alphaN/betaN/rcN).")
    return m.group(1), m.group(2) is not None


def _describe(run: dict) -> str:
    return (f"CI run {run.get('id')} (branch '{run.get('head_branch')}', "
            f"event '{run.get('event')}')")


def _is_ci_build(run: dict) -> bool:
    # A run's path may carry the ref it ran from: ".github/workflows/x.yml@refs/..."
    return (run.get("path") or "").split("@", 1)[0] == WORKFLOW_PATH


def _id(item: dict, what: str) -> int:
    """The numeric id of an API object; a response without one is an error, not a crash."""
    value = item.get("id")
    if not isinstance(value, int):
        raise ReleaseError(f"unexpected API response: {what} {item.get('name', '')} has no id.")
    return value


def _repo_name(run: dict, key: str) -> str | None:
    return (run.get(key) or {}).get("full_name")


def _succeeded(run: dict) -> bool:
    return run.get("status") == "completed" and run.get("conclusion") == "success"


def _is_master_push(run: dict, repository: str) -> bool:
    return (run.get("event") == "push" and run.get("head_branch") == RELEASE_BRANCH
            and _repo_name(run, "head_repository") == repository
            and _repo_name(run, "repository") == repository)


def check_run(run: dict, *, commit: str, repository: str) -> None:
    """Rejects a run the release may not publish, naming its branch and event."""
    who = _describe(run)
    if not _is_ci_build(run):
        raise ReleaseError(f"{who} is from {run.get('path')}, not the CI Build workflow.")
    if not _succeeded(run):
        raise ReleaseError(f"{who} did not succeed (status '{run.get('status')}', "
                           f"conclusion '{run.get('conclusion')}').")
    if not _is_master_push(run, repository):
        head_repo = _repo_name(run, "head_repository")
        raise ReleaseError(f"{who} was triggered by '{run.get('event')}' on branch "
                           f"'{run.get('head_branch')}' of {head_repo}; only a push to "
                           f"{RELEASE_BRANCH} of {repository} can be released.")
    if run.get("head_sha") != commit:
        raise ReleaseError(f"{who} built commit {run.get('head_sha')}, but the tag points "
                           f"to {commit}; a release publishes the build of its own commit.")
    _id(run, "CI run")


def select_run(runs: list[dict], *, commit: str, repository: str) -> dict:
    """The newest successful CI Build push run on master that built commit."""
    candidates = [r for r in runs if _is_ci_build(r) and _is_master_push(r, repository)
                  and r.get("head_sha") == commit]
    if not candidates:
        raise ReleaseError(
            f"No CI Build run for a push to {RELEASE_BRANCH} built commit {commit}. "
            f"Only a push to {RELEASE_BRANCH} creates one; a commit marked [skip ci], or one "
            "whose changes CI Build ignores (paths-ignore), has none. Tag a commit that "
            "CI Build built, or push a change and tag that commit.")
    good = [r for r in candidates if _succeeded(r)]
    for r in good:
        _id(r, "CI run")
    if not good:
        states = ", ".join(f"run {r.get('id')} {r.get('status')}/{r.get('conclusion')}"
                           for r in candidates)
        raise ReleaseError(
            f"CI Build has no successful run for commit {commit} ({states}). Wait for a "
            "running build to finish, or re-run a failed one, then re-run this release.")
    return max(good, key=lambda r: (r.get("run_number", 0), r.get("run_attempt", 0)))


def select_artifacts(artifacts: list[dict], *, run_id: int, commit: str) -> dict[str, int]:
    """Artifact ID per required name; of several with one name (a re-run), the latest."""
    latest: dict[str, dict] = {}
    for a in artifacts:
        name = a.get("name")
        if name in REQUIRED_ARTIFACTS:
            if name not in latest or _id(a, "artifact") > _id(latest[name], "artifact"):
                latest[name] = a
    missing = [n for n in REQUIRED_ARTIFACTS if n not in latest]
    if missing:
        raise ReleaseError(f"CI run {run_id} has no artifact {', '.join(missing)}.")
    ids: dict[str, int] = {}
    for name in REQUIRED_ARTIFACTS:
        a = latest[name]
        ids[name] = _id(a, "artifact")
        origin = a.get("workflow_run") or {}
        if origin.get("id") != run_id or origin.get("head_sha") != commit:
            raise ReleaseError(f"Artifact {name} ({ids[name]}) belongs to run {origin.get('id')} "
                               f"of commit {origin.get('head_sha')}, not to run {run_id} "
                               f"of commit {commit}.")
        if a.get("expired"):
            raise ReleaseError(f"Artifact {name} of CI run {run_id} has expired; re-run "
                               "that CI Build to release this commit.")
    return ids


def _sbom_commit(bom: dict) -> str | None:
    for prop in bom.get("metadata", {}).get("component", {}).get("properties", []):
        if prop.get("name") == "logsquirl:git-commit":
            return prop.get("value")
    return None


def release_notes(changelog: str, *, tag: str) -> str:
    """The CHANGELOG section of a release tag, without its heading (#307).

    The section starts at the level-one heading naming the tag's version
    (`# v26.10.0-beta1 (2026-09-17)`, or the older `# 26.04.2 (2026-04-19):`)
    and ends at the next level-one heading; the `---` separator before that
    heading is not part of it.
    """
    parse_tag(tag)
    heading = re.compile(rf"# v?{re.escape(tag[1:])}(?=[\s:(]|$).*")
    lines = changelog.splitlines()
    start = next((i for i, line in enumerate(lines) if heading.fullmatch(line)), None)
    if start is None:
        raise ReleaseError(f"CHANGELOG.md has no section for {tag}: add a heading "
                           f"'# {tag} (YYYY-MM-DD)' before tagging.")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("# ")), len(lines))
    section = lines[start + 1:end]
    while section and section[-1].strip() in ("", "---"):
        section.pop()
    while section and not section[0].strip():
        section.pop(0)
    return "\n".join(section) + "\n"


def check_build(root: Path, *, tag: str, commit: str) -> str:
    """Returns the version of the downloaded build if it is the tag's build."""
    base, _ = parse_tag(tag)
    for name in REQUIRED_ARTIFACTS:
        directory = root / name
        if not directory.is_dir() or not any(p.is_file() for p in directory.iterdir()):
            raise ReleaseError(f"Downloaded artifact {name} is missing or empty.")

    words = (root / VERSION_FILE).read_text().split()
    match = _VERSION.fullmatch(words[0]) if len(words) == 1 else None
    if not match:
        raise ReleaseError(f"{VERSION_FILE} does not hold one YY.MM.PATCH.BUILD version: {words!r}")
    version = match.group(0)
    if match.group(1) != base:
        raise ReleaseError(
            f"The CI Build of commit {commit} carries version {version}, but tag {tag} "
            f"releases {base}. Set project(VERSION {base}) in CMakeLists.txt on "
            f"{RELEASE_BRANCH}, let CI Build pass, and tag that commit instead.")

    bom = json.loads((root / SBOM_FILE).read_text())
    bom_version = bom.get("metadata", {}).get("component", {}).get("version")
    bom_commit = _sbom_commit(bom)
    if bom_commit != commit:
        raise ReleaseError(f"{SBOM_FILE} was generated from commit {bom_commit}, not {commit}.")
    if bom_version != version:
        raise ReleaseError(f"{SBOM_FILE} records version {bom_version}, not {version}.")

    for directory, pattern in VERSIONED_PACKAGES.items():
        expected = pattern.format(version=version)
        if not (root / directory / expected).is_file():
            found = sorted(p.name for p in (root / directory).iterdir())
            raise ReleaseError(f"{directory} has no {expected} (found {', '.join(found)}).")
    _check_windows(root / WINDOWS_PORTABLE, version)
    _check_mac(root / MAC_APP, version, base)
    return version


def _carries(data: bytes, version: str) -> bool:
    """The binary holds the version string itself, not merely a longer one containing it."""
    return re.search(rb"(?<![0-9.])" + re.escape(version.encode()) + rb"(?![0-9])", data) is not None


def _check_windows(portable: Path, version: str) -> None:
    # The installer is NSIS-compressed; the portable zip holds the executable.
    where = f"packages-windows-x64/{portable.name}"
    if not portable.is_file():
        raise ReleaseError(f"{where} is missing.")
    with zipfile.ZipFile(portable) as z:
        exes = [n for n in z.namelist() if n.rsplit("/", 1)[-1] == WINDOWS_EXE]
        if len(exes) != 1:
            raise ReleaseError(f"{where} holds {len(exes)} {WINDOWS_EXE} instead of one.")
        if not _carries(z.read(exes[0]), version):
            raise ReleaseError(f"{where}: {exes[0]} was not built as version {version}.")


def _check_mac(archive: Path, version: str, base: str) -> None:
    where = f"packages-mac-arm64/{archive.name}"
    if not archive.is_file():
        raise ReleaseError(f"{where} is missing.")
    info = binary = None
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            if member.name == MAC_INFO_PLIST:
                info = tar.extractfile(member).read()
            elif member.name == MAC_BINARY:
                binary = tar.extractfile(member).read()
    if info is None or binary is None:
        raise ReleaseError(f"{where} lacks {MAC_INFO_PLIST} or {MAC_BINARY}.")
    bundle_version = plistlib.loads(info).get("CFBundleVersion")
    if bundle_version != base:
        raise ReleaseError(f"{where}: CFBundleVersion is {bundle_version}, not {base}.")
    if not _carries(binary, version):
        raise ReleaseError(f"{where}: {MAC_BINARY} was not built as version {version}.")


# ── command line ────────────────────────────────────────────────────────────

def _gh_api(path: str) -> dict:
    result = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if result.returncode != 0:
        raise ReleaseError(f"gh api {path} failed: {result.stderr.strip()}")
    return json.loads(result.stdout)


def paginate(fetch, path: str, key: str, *, per_page: int = 100) -> list[dict]:
    """Every item of a paged REST listing (runs, artifacts): the items under key
    of each page, until total_count is reached or a page comes back empty."""
    items: list[dict] = []
    separator = "&" if "?" in path else "?"
    page = 1
    while True:
        body = fetch(f"{path}{separator}per_page={per_page}&page={page}")
        batch = body.get(key) or []
        items.extend(batch)
        if not batch or len(items) >= body.get("total_count", 0):
            return items
        page += 1


def _output(**values: str) -> None:
    lines = "".join(f"{k}={v}\n" for k, v in values.items())
    target = os.environ.get("GITHUB_OUTPUT")
    if target:
        with open(target, "a", encoding="utf-8") as f:
            f.write(lines)
    sys.stdout.write(lines)


def _find_run(repository: str, commit: str, run_id: str | None) -> None:
    if run_id:
        if not re.fullmatch(r"[0-9]+", run_id):
            raise ReleaseError(f"ci-run-id {run_id!r} is not a numeric run ID.")
        run = _gh_api(f"repos/{repository}/actions/runs/{run_id}")
        check_run(run, commit=commit, repository=repository)
    else:
        runs = paginate(_gh_api, f"repos/{repository}/actions/workflows/"
                        f"{Path(WORKFLOW_PATH).name}/runs?head_sha={commit}&event=push"
                        f"&branch={RELEASE_BRANCH}", "workflow_runs")
        run = select_run(runs, commit=commit, repository=repository)
    run_id = _id(run, "CI run")
    print(f"Releasing {_describe(run)}: {run.get('html_url')}", file=sys.stderr)
    artifacts = paginate(_gh_api, f"repos/{repository}/actions/runs/{run_id}/artifacts", "artifacts")
    ids = select_artifacts(artifacts, run_id=run_id, commit=commit)
    _output(run_id=str(run_id), artifact_ids=",".join(str(i) for i in ids.values()))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    tag = sub.add_parser("check-tag")
    tag.add_argument("--tag", required=True)
    find = sub.add_parser("find-run")
    find.add_argument("--repository", required=True)
    find.add_argument("--commit", required=True)
    find.add_argument("--run-id", default="")
    build = sub.add_parser("check-build")
    build.add_argument("--tag", required=True)
    build.add_argument("--commit", required=True)
    build.add_argument("--root", type=Path, default=Path("."))
    notes = sub.add_parser("release-notes")
    notes.add_argument("--tag", required=True)
    notes.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    notes.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        if args.command == "check-tag":
            base, prerelease = parse_tag(args.tag)
            _output(tag=args.tag, base_version=base, is_prerelease=str(prerelease).lower())
        elif args.command == "find-run":
            _find_run(args.repository, args.commit, args.run_id)
        elif args.command == "release-notes":
            text = release_notes(args.changelog.read_text(encoding="utf-8"), tag=args.tag)
            args.out.write_text(text, encoding="utf-8")
        else:
            _output(version=check_build(args.root, tag=args.tag, commit=args.commit))
    except (ReleaseError, OSError, ValueError, KeyError, TypeError, AttributeError,
            tarfile.TarError, zipfile.BadZipFile) as err:
        # One line: the annotation ends at the first newline.
        print(f"::error::{' '.join(str(err).split())}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
