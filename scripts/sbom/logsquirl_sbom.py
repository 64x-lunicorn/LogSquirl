#!/usr/bin/env python3
"""CycloneDX SBOM of a LogSquirl release (#212).

The SBOM is built in two stages, so each part is taken from the place that
knows it:

``generate`` (CI Build, every run, same commit as the binaries)
    * every ``CPMAddPackage`` of ``3rdparty/CMakeLists.txt`` with its version,
      pinned commit (#209), purl and the CMake condition it is added under;
    * the platform components that are not CPM packages: Qt (version from the
      CI matrix and the Linux build images, which must agree), Boost (the
      version agent-setup downloads for macOS/Windows; Linux builds use the
      build image's distribution headers), OpenSSL and ICU (not pinned in the
      repository, so they carry no version yet).

    The CMake file is parsed rather than read back from a configure run: a
    configure evaluates one platform's branch and needs the full toolchain,
    while one release ships Linux, macOS and Windows packages. The parser sees
    every branch, records the enclosing ``if()`` conditions, and fails on
    anything it cannot classify, so a new package or condition cannot silently
    drop out of the SBOM.

``merge`` (CI Release, before the checksum file is written)
    * detects the versions of Qt, OpenSSL and ICU inside the unpacked packages
      that bundle them (AppImage, Windows portable zip, macOS app) and fills
      them in, one component per detected version;
    * adds what syft found in the packages that is not already described.

``appimage-debs`` (CI Build, in the AppImage build image after linuxdeploy)
    records the Debian package and version of every system library linuxdeploy
    bundled (#227); ``merge --deb-manifest`` adds them with ``pkg:deb`` purls.

``validate`` checks a BOM against the CycloneDX 1.6 JSON schema (strict, via
cyclonedx-python-lib) plus the invariants the schema cannot express.

Everything except ``validate`` uses the standard library only.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import json
import os
import re
import struct
import sys
import uuid
from pathlib import Path
from urllib.parse import quote

SPEC_VERSION = "1.6"
ALL_PLATFORMS = ("linux", "macos", "windows")
PROP = "logsquirl:"
DEFAULT_REPOSITORY = "64x-lunicorn/LogSquirl"


class SbomError(Exception):
    """Input the generator refuses to guess about."""


# ── purl ────────────────────────────────────────────────────────────────────
# Same percent-encoding as packageurl-python's canonical form (the tests
# compare against it): ':' and '/' stay, everything else reserved is escaped.


def _q(value: str) -> str:
    return quote(value, safe=":/")


def purl(type_: str, name: str, version: str | None = None, namespace: str | None = None,
         qualifiers: dict[str, str] | None = None) -> str:
    out = f"pkg:{type_}/"
    if namespace:
        out += "/".join(_q(part) for part in namespace.split("/")) + "/"
    out += _q(name)
    if version:
        out += "@" + _q(version)
    if qualifiers:
        out += "?" + "&".join(f"{k}={_q(v)}" for k, v in sorted(qualifiers.items()))
    return out


def github_purl(owner: str, repo: str, ref: str) -> str:
    # The github purl type is case-insensitive and canonically lowercase.
    return purl("github", repo.lower(), ref, namespace=owner.lower())


# ── CMake lexer ─────────────────────────────────────────────────────────────


@dataclasses.dataclass
class Arg:
    value: str
    line: int
    comment: str | None = None  # "# ..." on the same line, directly after it


@dataclasses.dataclass
class Command:
    name: str
    args: list[Arg]
    line: int


_BRACKET_OPEN = re.compile(r"\[(=*)\[")


def parse_cmake(text: str) -> list[Command]:
    """Split a CMake file into commands. Parentheses inside arguments (as in
    ``if(NOT (A AND B))``) are kept as ``(`` and ``)`` arguments."""
    commands: list[Command] = []
    i, line, n = 0, 1, len(text)

    def skip_comment(pos: int, ln: int) -> tuple[int, int, str | None]:
        m = _BRACKET_OPEN.match(text, pos + 1)
        if m:
            end = text.find("]" + m.group(1) + "]", m.end())
            if end < 0:
                raise SbomError(f"line {ln}: unterminated bracket comment")
            body = text[pos:end]
            return end + len(m.group(1)) + 2, ln + body.count("\n"), None
        end = text.find("\n", pos)
        end = n if end < 0 else end
        return end, ln, text[pos + 1:end].strip()

    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif c == "#":
            i, line, _ = skip_comment(i, line)
        else:
            m = re.compile(r"[A-Za-z_][A-Za-z0-9_]*").match(text, i)
            if not m:
                raise SbomError(f"line {line}: expected a command, found {text[i:i + 20]!r}")
            name, cmd_line = m.group(0), line
            i = m.end()
            while i < n and text[i] in " \t":
                i += 1
            if i >= n or text[i] != "(":
                raise SbomError(f"line {line}: command {name} without '('")
            i += 1
            args: list[Arg] = []
            depth = 1
            while True:
                if i >= n:
                    raise SbomError(f"line {cmd_line}: unterminated {name}(")
                c = text[i]
                if c == "\n":
                    line += 1
                    i += 1
                elif c.isspace():
                    i += 1
                elif c == "#":
                    i, new_line, comment = skip_comment(i, line)
                    if comment is not None and args and args[-1].line == line:
                        args[-1].comment = comment
                    line = new_line
                elif c == "(":
                    depth += 1
                    args.append(Arg("(", line))
                    i += 1
                elif c == ")":
                    depth -= 1
                    i += 1
                    if depth == 0:
                        break
                    args.append(Arg(")", line))
                elif c == '"':
                    j, buf = i + 1, []
                    while j < n and text[j] != '"':
                        if text[j] == "\\" and j + 1 < n:
                            buf.append(text[j:j + 2])
                            j += 2
                            continue
                        buf.append(text[j])
                        j += 1
                    if j >= n:
                        raise SbomError(f"line {line}: unterminated string")
                    value = "".join(buf)
                    args.append(Arg(value, line))
                    line += value.count("\n")
                    i = j + 1
                else:
                    m = re.compile(r'[^\s()#"]+').match(text, i)
                    assert m
                    args.append(Arg(m.group(0), line))
                    i = m.end()
            commands.append(Command(name, args, cmd_line))
    return commands


# ── CPM packages ────────────────────────────────────────────────────────────

# cmake/CPM.cmake (0.38.1) keyword lists; OPTIONS and URL collect values up
# to the next keyword, like cmake_parse_arguments does.
_CPM_SINGLE = {"NAME", "FORCE", "VERSION", "GIT_TAG", "DOWNLOAD_ONLY", "GITHUB_REPOSITORY",
               "GITLAB_REPOSITORY", "BITBUCKET_REPOSITORY", "GIT_REPOSITORY", "SOURCE_DIR",
               "DOWNLOAD_COMMAND", "FIND_PACKAGE_ARGUMENTS", "NO_CACHE", "SYSTEM", "GIT_SHALLOW",
               "EXCLUDE_FROM_ALL", "SOURCE_SUBDIR"}
_CPM_MULTI = {"URL", "OPTIONS"}
# Sources the SBOM cannot describe with a pinned commit (#209 pins by git SHA).
_CPM_UNSUPPORTED = {"GITLAB_REPOSITORY", "BITBUCKET_REPOSITORY", "SOURCE_DIR", "DOWNLOAD_COMMAND", "URL"}
_SHA = re.compile(r"^[0-9a-f]{40}$")


@dataclasses.dataclass(frozen=True)
class CpmPackage:
    name: str
    repository_url: str
    github: tuple[str, str] | None  # (owner, repo)
    commit: str
    version: str | None  # VERSION argument
    tag: str | None  # "# <tag>" comment next to GIT_TAG
    conditions: tuple[str, ...]
    download_only: bool
    line: int


def _join_condition(args: list[Arg]) -> str:
    return re.sub(r"\( ", "(", re.sub(r" \)", ")", " ".join(a.value for a in args)))


def _negate(cond: str) -> str:
    if cond.startswith("NOT ") and " " not in cond[4:]:
        return cond[4:]
    return f"NOT {cond}" if " " not in cond else f"NOT ({cond})"


def parse_cpm_packages(text: str) -> list[CpmPackage]:
    packages: list[CpmPackage] = []
    # One frame per open if(): the conditions of the earlier branches and the
    # condition of the branch being read.
    stack: list[tuple[list[str], str]] = []

    def current_conditions() -> tuple[str, ...]:
        out: list[str] = []
        for previous, cond in stack:
            out.extend(_negate(p) for p in previous)
            if cond:
                out.append(cond)
        return tuple(out)

    for cmd in parse_cmake(text):
        lname = cmd.name.lower()
        if lname == "if":
            stack.append(([], _join_condition(cmd.args)))
        elif lname in ("elseif", "else"):
            if not stack:
                raise SbomError(f"line {cmd.line}: {cmd.name}() without if()")
            previous, cond = stack.pop()
            new_cond = _join_condition(cmd.args) if lname == "elseif" else ""
            stack.append((previous + [cond], new_cond))
        elif lname == "endif":
            if not stack:
                raise SbomError(f"line {cmd.line}: endif() without if()")
            stack.pop()
        elif lname == "cpmaddpackage":
            packages.append(_cpm_package(cmd, current_conditions()))
    if stack:
        raise SbomError("unterminated if() at end of file")
    return packages


def _cpm_package(cmd: Command, conditions: tuple[str, ...]) -> CpmPackage:
    where = f"line {cmd.line}"
    if len(cmd.args) == 1:
        raise SbomError(f"{where}: CPMAddPackage shorthand {cmd.args[0].value!r} is not supported; "
                        "use NAME/GITHUB_REPOSITORY/GIT_TAG")
    values: dict[str, Arg] = {}
    i, args = 0, cmd.args
    while i < len(args):
        key = args[i].value
        if key in _CPM_SINGLE:
            if i + 1 >= len(args):
                raise SbomError(f"{where}: {key} without a value")
            values[key] = args[i + 1]
            i += 2
        elif key in _CPM_MULTI:
            values[key] = args[i]
            i += 1
            while i < len(args) and args[i].value not in _CPM_SINGLE | _CPM_MULTI:
                i += 1
        else:
            raise SbomError(f"{where}: unexpected CPMAddPackage argument {key!r}")

    if "NAME" not in values:
        raise SbomError(f"{where}: CPMAddPackage without NAME")
    name = values["NAME"].value
    where = f"{where} ({name})"
    for key in _CPM_UNSUPPORTED:
        if key in values:
            raise SbomError(f"{where}: {key} sources are not supported by the SBOM generator")

    if "GITHUB_REPOSITORY" in values:
        owner_repo = values["GITHUB_REPOSITORY"].value
        if not re.fullmatch(r"[\w.-]+/[\w.-]+", owner_repo):
            raise SbomError(f"{where}: malformed GITHUB_REPOSITORY {owner_repo!r}")
        owner, repo = owner_repo.split("/")
        github: tuple[str, str] | None = (owner, repo)
        url = f"https://github.com/{owner}/{repo}"
    elif "GIT_REPOSITORY" in values:
        url = values["GIT_REPOSITORY"].value.removesuffix(".git")
        m = re.fullmatch(r"https://github\.com/([\w.-]+)/([\w.-]+)", url)
        github = (m.group(1), m.group(2)) if m else None
    else:
        raise SbomError(f"{where}: no GITHUB_REPOSITORY or GIT_REPOSITORY")

    if "GIT_TAG" not in values:
        raise SbomError(f"{where}: no GIT_TAG; every package is pinned to a commit SHA (#209)")
    git_tag = values["GIT_TAG"]
    if not _SHA.match(git_tag.value):
        raise SbomError(f"{where}: GIT_TAG {git_tag.value!r} is not a full commit SHA (#209)")

    version = values["VERSION"].value if "VERSION" in values else None
    download_only = "DOWNLOAD_ONLY" in values and values["DOWNLOAD_ONLY"].value.upper() in ("YES", "ON", "TRUE", "1")
    return CpmPackage(name=name, repository_url=url, github=github, commit=git_tag.value, version=version,
                      tag=git_tag.comment or None, conditions=conditions, download_only=download_only,
                      line=cmd.line)


@dataclasses.dataclass(frozen=True)
class ConditionEffect:
    platforms: frozenset[str] = frozenset(ALL_PLATFORMS)
    excluded: str | None = None  # reason the package is not part of the shipped binaries


# How each CMake condition in 3rdparty/CMakeLists.txt applies to the release
# build. A condition missing here stops the generator: someone has to decide
# whether the package it guards ships.
CONDITION_EFFECTS: dict[str, ConditionEffect] = {
    "APPLE": ConditionEffect(frozenset({"macos"})),
    "WIN32": ConditionEffect(frozenset({"windows"})),
    "NOT WIN32": ConditionEffect(frozenset({"linux", "macos"})),
    # Every release build is 64-bit, where the option defaults to ON.
    "LOGSQUIRL_USE_VECTORSCAN": ConditionEffect(),
    # prepare-workspace-env configures every CI build with -DLOGSQUIRL_USE_SENTRY=ON.
    "LOGSQUIRL_USE_SENTRY": ConditionEffect(),
    # No build environment installs KF6Archive, so the CPM fallback is built.
    "NOT _KARCHIVE_FOUND": ConditionEffect(),
    "LOGSQUIRL_BUILD_TESTS": ConditionEffect(excluded="only linked into the test executables"),
}

# CPM packages that are build tooling, not code in the binaries.
BUILD_TOOLS = {"macdeployqtfix": "macOS packaging script, run at build time"}


def _version_from_tag(tag: str | None) -> str | None:
    if not tag:
        return None
    last = tag.rsplit("/", 1)[-1]
    return last[1:] if re.fullmatch(r"v\d.*", last) else last


def cpm_component(pkg: CpmPackage) -> dict:
    platforms = set(ALL_PLATFORMS)
    excluded = BUILD_TOOLS.get(pkg.name)
    for cond in pkg.conditions:
        effect = CONDITION_EFFECTS.get(cond)
        if effect is None:
            raise SbomError(f"line {pkg.line} ({pkg.name}): unknown CMake condition {cond!r}; "
                            "add it to CONDITION_EFFECTS in scripts/sbom/logsquirl_sbom.py")
        platforms &= effect.platforms
        excluded = excluded or effect.excluded
    if not platforms:
        raise SbomError(f"line {pkg.line} ({pkg.name}): conditions {pkg.conditions} exclude every platform")

    version = pkg.version or _version_from_tag(pkg.tag) or pkg.commit
    if pkg.github:
        package_url = github_purl(*pkg.github, pkg.tag or pkg.commit)
    else:
        package_url = purl("generic", pkg.repository_url.rsplit("/", 1)[-1].lower(), version,
                           qualifiers={"vcs_url": f"git+{pkg.repository_url}@{pkg.commit}"})
    props = {
        "source": "cpm",
        "git-commit": pkg.commit,
        "git-tag": pkg.tag,
        "platforms": ",".join(p for p in ALL_PLATFORMS if p in platforms),
        "cmake-condition": " AND ".join(pkg.conditions) or None,
        "excluded-reason": excluded,
    }
    return _component(
        ref=f"cpm:{pkg.name}", name=pkg.name, version=version, purl_=package_url,
        scope="excluded" if excluded else "required",
        external=[{"type": "vcs", "url": pkg.repository_url}], props=props)


def _component(*, ref: str, name: str, version: str | None, purl_: str | None, scope: str = "required",
               cpe: str | None = None, external: list[dict] | None = None, props: dict[str, str | None],
               type_: str = "library") -> dict:
    comp: dict = {"bom-ref": ref, "type": type_, "name": name}
    if version:
        comp["version"] = version
    comp["scope"] = scope
    if cpe:
        comp["cpe"] = cpe
    if purl_:
        comp["purl"] = purl_
    if external:
        comp["externalReferences"] = external
    comp["properties"] = [{"name": PROP + k, "value": v} for k, v in props.items() if v]
    return comp


# ── platform components (single sources of truth in the repository) ────────


@dataclasses.dataclass(frozen=True)
class PlatformPins:
    qt_version: str
    qt_archives: tuple[str, ...]
    boost_version: str
    linux_images: tuple[str, ...]  # build image directories whose Boost headers the Linux builds use


def _one_value(found: dict[str, set[str]], what: str) -> str:
    values = set().union(*found.values()) if found else set()
    if len(values) != 1:
        detail = "; ".join(f"{src}: {', '.join(sorted(v))}" for src, v in sorted(found.items()))
        raise SbomError(f"{what} is not pinned to exactly one value ({detail or 'not found'})")
    return values.pop()


def read_platform_pins(repo_root: Path) -> PlatformPins:
    qt: dict[str, set[str]] = {}
    archives: set[str] = set()
    workflow = repo_root / ".github/workflows/ci-build.yml"
    versions = set(re.findall(r"^\s*qt_version:\s*([\d.]+)\s*$", workflow.read_text(), re.M))
    if versions:
        qt[str(workflow.relative_to(repo_root))] = versions
    for name in re.findall(r"^\s*qt_modules:\s*(.+?)\s*$", workflow.read_text(), re.M):
        archives.update(name.split())

    linux_images = []
    for dockerfile in sorted(repo_root.glob("docker/*/Dockerfile")):
        text = dockerfile.read_text()
        rel = str(dockerfile.relative_to(repo_root))
        versions = set(re.findall(r"^ENV QT_VERSION=([\d.]+)\s*$", text, re.M))
        if versions:
            qt[rel] = versions
            for flag in re.findall(r"--(?:archives|modules)\s+((?:[\w-]+\s*)+?)(?:\\|--|&&|$)", text, re.M):
                archives.update(flag.split())
        if re.search(r"\b(libboost-dev|boost-devel)\b", text):
            linux_images.append(str(dockerfile.parent.relative_to(repo_root)))

    setup = repo_root / ".github/actions/agent-setup/action.yml"
    setup_text = setup.read_text()
    boost = set(re.findall(r'BOOST_VERSION="([\d.]+)"', setup_text))
    for line in re.findall(r"^\s*archives:\s*(.+?)\s*$", setup_text, re.M):
        archives.update(line.split())

    return PlatformPins(
        qt_version=_one_value(qt, "Qt"),
        qt_archives=tuple(sorted(archives)),
        boost_version=_one_value({str(setup.relative_to(repo_root)): boost} if boost else {}, "Boost"),
        linux_images=tuple(linux_images),
    )


def _cpe(vendor: str, product: str, version: str) -> str:
    return f"cpe:2.3:a:{vendor}:{product}:{version}:*:*:*:*:*:*:*"


# Detection key -> (component name, CPE vendor, CPE product)
DETECTABLE = {
    "qt": ("qt", "qt", "qt"),
    "openssl": ("openssl", "openssl", "openssl"),
    "icu": ("icu", "unicode", "international_components_for_unicode"),
}


def platform_components(pins: PlatformPins) -> list[dict]:
    qt = pins.qt_version
    return [
        _component(
            ref="platform:qt", name="qt", version=qt, purl_=purl("generic", "qt", qt), cpe=_cpe("qt", "qt", qt),
            props={"source": "platform", "platforms": ",".join(ALL_PLATFORMS), "detect": "qt",
                   "qt-archives": ",".join(pins.qt_archives),
                   "version-source": "qt_version in .github/workflows/ci-build.yml and QT_VERSION in docker/*/Dockerfile"}),
        _component(
            ref="platform:icu", name="icu", version=None, purl_=purl("generic", "icu"),
            props={"source": "platform", "platforms": "linux", "detect": "icu",
                   "version-source": "ICU archive of the Qt install (aqt --archives icu); version detected in the packages"}),
        _component(
            ref="platform:openssl", name="openssl", version=None, purl_=purl("generic", "openssl"),
            props={"source": "platform", "platforms": "linux,windows", "detect": "openssl",
                   "version-source": "not pinned in the repository; version detected in the packages"}),
        _component(
            ref="platform:boost", name="boost", version=pins.boost_version,
            purl_=purl("generic", "boost", pins.boost_version), cpe=_cpe("boost", "boost", pins.boost_version),
            props={"source": "platform", "platforms": "macos,windows",
                   "linkage": "header-only, compiled into hyperscan/vectorscan",
                   "version-source": "BOOST_VERSION in .github/actions/agent-setup/action.yml"}),
        _component(
            ref="platform:boost-linux", name="boost", version=None, purl_=purl("generic", "boost"),
            props={"source": "platform", "platforms": "linux",
                   "linkage": "header-only, compiled into vectorscan",
                   "version-source": "distribution Boost headers of the build images " + ",".join(pins.linux_images)}),
    ]


# ── BOM assembly ────────────────────────────────────────────────────────────

TOOL = {"type": "application", "name": "logsquirl_sbom.py", "group": "64x-lunicorn"}


def _now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def build_base_bom(*, cpm_text: str, pins: PlatformPins, version: str, commit: str,
                   repository: str = DEFAULT_REPOSITORY, timestamp: str | None = None,
                   serial: str | None = None) -> dict:
    if not _SHA.match(commit):
        raise SbomError(f"--commit {commit!r} is not a full commit SHA")
    owner, repo = repository.split("/")
    components = [cpm_component(p) for p in parse_cpm_packages(cpm_text)] + platform_components(pins)
    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": SPEC_VERSION,
        "serialNumber": serial or uuid.uuid4().urn,
        "version": 1,
        "metadata": {
            "timestamp": timestamp or _now(),
            "tools": {"components": [dict(TOOL)]},
            "component": {
                "bom-ref": "logsquirl",
                "type": "application",
                "name": "logsquirl",
                "version": version,
                "purl": github_purl(owner, repo, commit),
                "externalReferences": [{"type": "vcs", "url": f"https://github.com/{owner}/{repo}"}],
                "properties": [{"name": PROP + "git-commit", "value": commit}],
            },
        },
        "components": components,
    }
    _set_dependencies(bom)
    return bom


def _set_dependencies(bom: dict) -> None:
    shipped = [c["bom-ref"] for c in bom["components"] if c.get("scope") != "excluded"]
    bom["dependencies"] = [{"ref": "logsquirl", "dependsOn": shipped}] + [
        {"ref": c["bom-ref"]} for c in bom["components"]]


def _prop(comp: dict, key: str) -> str | None:
    for p in comp.get("properties", []):
        if p["name"] == PROP + key:
            return p["value"]
    return None


def _set_prop(comp: dict, key: str, value: str | None) -> None:
    props = [p for p in comp.get("properties", []) if p["name"] != PROP + key]
    if value:
        props.append({"name": PROP + key, "value": value})
    comp["properties"] = props


# ── detection in unpacked packages ──────────────────────────────────────────


@dataclasses.dataclass(frozen=True)
class Detection:
    key: str
    version: str
    package: str  # appimage | windows | macos
    path: str
    upstream: bool  # an upstream build (CPE applies), not a distribution-patched one


# The unpacked packages that bundle third-party libraries, and their platform.
# deb and rpm packages ship only LogSquirl's own executables, which link the
# system's Qt and OpenSSL, so there is nothing bundled to detect in them.
PACKAGE_KINDS = {"appimage": "linux", "windows": "windows", "macos": "macos"}
# generate_appimage.sh copies libssl/libcrypto from the Ubuntu 22.04 build image:
# a distribution build whose security fixes do not show in the upstream version.
DISTRIBUTION_BUILDS = {("appimage", "openssl")}

_DETECTORS: list[tuple[str, re.Pattern[str], re.Pattern[bytes] | None]] = [
    ("qt", re.compile(r"^(libQt6Core\.so(\.\d+)*|Qt6Core\.dll|QtCore)$"), re.compile(rb"Qt (6\.\d+\.\d+) \(")),
    ("openssl", re.compile(r"^libcrypto[-.\w]*\.(so(\.\d+)*|dll|dylib)$"),
     re.compile(rb"OpenSSL (\d+\.\d+\.\d+[a-z]?) +\d{1,2} [A-Z][a-z]{2} \d{4}")),
    ("icu", re.compile(r"^(libicuuc\.so\.(?P<major>\d+)(\.\d+)*|icuuc(?P<wmajor>\d+)\.dll|libicuuc\.(?P<mmajor>\d+)\.dylib)$"), None),
]


def detect_bundled(package: str, root: Path) -> list[Detection]:
    if package not in PACKAGE_KINDS:
        raise SbomError(f"unknown package kind {package!r} (expected one of {sorted(PACKAGE_KINDS)})")
    found: list[Detection] = []
    for dirpath, _dirs, files in os.walk(root):
        for filename in sorted(files):
            path = Path(dirpath) / filename
            if path.is_symlink() or not path.is_file():
                continue
            for key, name_re, content_re in _DETECTORS:
                m = name_re.match(filename)
                if not m:
                    continue
                data = path.read_bytes()
                if key == "icu":
                    major = next(g for g in (m.group("major"), m.group("wmajor"), m.group("mmajor")) if g)
                    hit = re.search(rb"\x00(" + major.encode() + rb"\.\d+(?:\.\d+)*)\x00", data)
                else:
                    assert content_re is not None
                    hit = content_re.search(data)
                if hit:
                    found.append(Detection(key, hit.group(1).decode(), package, str(path.relative_to(root)),
                                           (package, key) not in DISTRIBUTION_BUILDS))
    return found


def merge_detections(bom: dict, detections: list[Detection], require: set[str] = frozenset()) -> None:
    by_key: dict[str, list[Detection]] = {}
    for d in detections:
        by_key.setdefault(d.key, []).append(d)
    missing = sorted(k for k in require if k not in by_key)
    if missing:
        raise SbomError(f"no bundled {', '.join(missing)} found in the scanned packages")

    result: list[dict] = []
    for comp in bom["components"]:
        key = _prop(comp, "detect")
        dets = by_key.get(key or "")
        if not dets:
            result.append(comp)
            continue
        versions: dict[str, list[Detection]] = {}
        for d in dets:
            versions.setdefault(d.version, []).append(d)
        if comp.get("version"):
            wrong = sorted(v for v in versions if v != comp["version"])
            if wrong:
                where = ", ".join(f"{d.package}:{d.path}" for v in wrong for d in versions[v])
                raise SbomError(f"{comp['name']} {comp['version']} is pinned, but the packages bundle "
                                f"{', '.join(wrong)} ({where})")
            _set_prop(comp, "bundled-in", ",".join(sorted({d.package for d in dets})))
            result.append(comp)
            continue
        name, vendor, product = DETECTABLE[key]
        for version, group in sorted(versions.items()):
            new = json.loads(json.dumps(comp))
            new["bom-ref"] = f"{comp['bom-ref']}@{version}"
            new["version"] = version
            new["purl"] = purl("generic", name, version)
            if all(d.upstream for d in group):
                new["cpe"] = _cpe(vendor, product, version)
            _set_prop(new, "platforms", ",".join(p for p in ALL_PLATFORMS
                                                 if p in {PACKAGE_KINDS[d.package] for d in group}))
            _set_prop(new, "bundled-in", ",".join(sorted({d.package for d in group})))
            _set_prop(new, "version-source", "detected in " + ", ".join(
                sorted({f"{d.package}:{d.path}" for d in group})))
            if not all(d.upstream for d in group):
                _set_prop(new, "distribution-build",
                          "patched distribution package; upstream version only, no CPE")
            result.append(_order(new))
    bom["components"] = result
    _set_dependencies(bom)


def _order(comp: dict) -> dict:
    keys = ["bom-ref", "type", "name", "version", "scope", "cpe", "purl", "externalReferences", "properties"]
    return {k: comp[k] for k in keys if k in comp} | {k: v for k, v in comp.items() if k not in keys}


# ── syft ────────────────────────────────────────────────────────────────────

# syft names of things this BOM already describes, mapped to our component name.
SYFT_ALIASES = {
    "qt6": "qt",
    "the openssl toolkit": "openssl",
    "oneapi threading building blocks (onetbb)": "tbb",
}


def _normalize_version(v: str | None) -> str:
    v = (v or "").strip()
    while re.fullmatch(r"\d+(\.\d+){3,}", v) and v.endswith(".0"):
        v = v[:-2]  # PE file versions carry a fourth ".0"
    return v


def merge_syft(bom: dict, syft: dict) -> None:
    tools = bom["metadata"]["tools"]["components"]
    for tool in syft.get("metadata", {}).get("tools", {}).get("components", []):
        entry = {k: tool[k] for k in ("type", "author", "name", "version") if k in tool}
        if entry not in tools:
            tools.append(entry)

    by_name: dict[str, list[dict]] = {}
    for comp in bom["components"]:
        by_name.setdefault(comp["name"].lower(), []).append(comp)

    collected: dict[tuple[str, str], dict] = {}
    for sc in syft.get("components", []):
        if sc.get("type") == "file":
            continue
        name, version = sc.get("name", ""), sc.get("version", "")
        if not name or name.lower().startswith("logsquirl"):
            continue  # LogSquirl's own executables and packages
        paths = [p["value"] for p in sc.get("properties", []) if re.fullmatch(r"syft:location:\d+:path", p["name"])]
        target = by_name.get(SYFT_ALIASES.get(name.lower(), name.lower()))
        if target:
            if not any(_normalize_version(c.get("version")) == _normalize_version(version) for c in target):
                for c in target:
                    seen = set(filter(None, (_prop(c, "syft-version") or "").split(",")))
                    _set_prop(c, "syft-version", ",".join(sorted(seen | {version})))
            continue
        key = (name, version)
        entry = collected.get(key)
        if entry is None:
            slug = re.sub(r"[^\w.@+-]+", "-", f"{name}@{version}").strip("-")
            entry = _component(
                ref=f"syft:{slug}", name=name, version=version or None, purl_=sc.get("purl"), cpe=sc.get("cpe"),
                type_=sc.get("type", "library"),
                props={"source": "syft", "found-by": next((p["value"] for p in sc.get("properties", [])
                                                           if p["name"] == "syft:package:foundBy"), None)})
            entry["_paths"] = set()
            collected[key] = entry
        entry["_paths"].update(paths)

    for entry in collected.values():
        _set_prop(entry, "syft-paths", ",".join(sorted(entry.pop("_paths"))))
        bom["components"].append(entry)
    _set_dependencies(bom)


# ── Debian packages of the AppImage's system libraries (#227) ───────────────
# linuxdeploy copies about 40 system libraries of the Ubuntu 22.04 build image
# into the AppImage (glib, krb5, libgcrypt, ...). Their files carry no version
# syft can read, so `appimage-debs` runs in the build image right after
# linuxdeploy and asks dpkg which package and version each one came from; the
# manifest travels with the AppImage artifact, and `merge` adds the packages
# with the pkg:deb purls grype matches against the Ubuntu security tracker.

_LIBRARY = re.compile(r"^lib[^/]*\.so(\.\d+)*$")
# Where the build image's linker finds system libraries, and the prefixes
# under which a library must belong to a dpkg package.
SYSTEM_LIB_DIRS = ("/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib", "/usr/lib")
SYSTEM_PREFIXES = ("/usr/lib", "/lib")


@dataclasses.dataclass(frozen=True)
class DebPackage:
    name: str
    version: str
    arch: str
    source: str
    source_version: str


def parse_dpkg_search(output: str, path: str) -> str | None:
    """The ``package:arch`` owning path in ``dpkg-query -S`` output, or None."""
    owners = None
    for line in output.splitlines():
        if line.startswith("diversion by "):
            continue
        head, sep, tail = line.rpartition(": ")
        if sep and tail == path:
            owners = [o.strip() for o in head.split(",")]
    if owners and len(owners) > 1:
        raise SbomError(f"{path} belongs to more than one package: {', '.join(owners)}")
    return owners[0] if owners else None


def parse_dpkg_show(line: str) -> DebPackage:
    """A line of ``dpkg-query -W -f '${Package}\t${Version}\t${Architecture}\t${Source}\n'``.
    Source is empty when it equals the package, and carries the source
    version in parentheses when that differs from the binary version."""
    fields = line.rstrip("\n").split("\t")
    if len(fields) != 4 or not all(fields[:3]):
        raise SbomError(f"unexpected dpkg-query output: {line!r}")
    name, version, arch, source = fields
    m = re.fullmatch(r"\s*([^\s(]*)\s*(?:\((.+)\))?\s*", source)
    if not m:
        raise SbomError(f"unexpected Source field {source!r} of {name}")
    return DebPackage(name, version, arch, m.group(1) or name, m.group(2) or version)


def _usrmerge_aliases(path: str, prefixes: list[str]) -> list[str]:
    # Ubuntu 22.04 is usrmerged (/lib is /usr/lib), but dpkg records each file
    # under the name its package ships, /lib/... or /usr/lib/...
    for prefix in prefixes:
        if path.startswith(prefix + "/"):
            return [path] + [other + path[len(prefix):] for other in prefixes if other != prefix]
    return [path]


def dpkg_owner(path: str) -> str | None:
    import subprocess
    result = subprocess.run(["dpkg-query", "-S", path], capture_output=True, text=True, check=False)
    return parse_dpkg_search(result.stdout, path) if result.returncode == 0 else None


def dpkg_show(package: str) -> DebPackage:
    import subprocess
    fmt = "${Package}\\t${Version}\\t${Architecture}\\t${Source}\\n"
    result = subprocess.run(["dpkg-query", "-W", "-f", fmt, package], capture_output=True, text=True, check=True)
    return parse_dpkg_show(result.stdout)


def appimage_deb_manifest(appdir: Path, *, lib_dirs: list[Path], system_prefixes: list[str],
                          owner=dpkg_owner, show=dpkg_show, distro: str) -> dict:
    """Package and version of every library in appdir that linuxdeploy took
    from a directory under system_prefixes. A library is looked up by name in
    lib_dirs, in the order the dynamic linker searched them while linuxdeploy
    ran (LD_LIBRARY_PATH first). One that resolves elsewhere (Qt from
    aqtinstall, Qt's plugins) is listed as unpackaged; one under a system
    prefix that dpkg does not know fails."""
    packages: dict[str, dict] = {}
    unpackaged: list[str] = []
    for path in sorted(appdir.rglob("*")):
        if path.is_symlink() or not path.is_file() or not _LIBRARY.match(path.name):
            continue
        rel = str(path.relative_to(appdir))
        found = next((d / path.name for d in lib_dirs if (d / path.name).exists()), None)
        if found is None or not any(str(found).startswith(p + "/") for p in system_prefixes):
            unpackaged.append(rel)
            continue
        # A -dev package's link (libssl.so) belongs to the runtime package of
        # the file it points to, so the resolved file is asked first.
        candidates = [alias for c in dict.fromkeys([os.path.realpath(found), str(found)])
                      for alias in _usrmerge_aliases(c, system_prefixes)]
        pkg = next(filter(None, (owner(c) for c in candidates)), None)
        if pkg is None:
            raise SbomError(f"{rel} ({found}) is a system library, but no dpkg package owns it")
        entry = packages.get(pkg)
        if entry is None:
            deb = show(pkg)
            entry = packages[pkg] = {"name": deb.name, "version": deb.version, "arch": deb.arch,
                                     "source": deb.source, "source-version": deb.source_version, "files": []}
        entry["files"].append(rel)
    if not packages:
        raise SbomError(f"no bundled system library found in {appdir}")
    return {"distro": distro, "packages": sorted(packages.values(), key=lambda p: p["name"]),
            "unpackaged": unpackaged}


def os_release_distro(text: str) -> str:
    fields = dict(re.findall(r'^(\w+)="?([^"\n]*)"?$', text, re.M))
    if not fields.get("ID") or not fields.get("VERSION_ID"):
        raise SbomError("os-release lacks ID or VERSION_ID")
    return f"{fields['ID']}-{fields['VERSION_ID']}"


def merge_deb_manifest(bom: dict, manifest: dict, package: str = "appimage") -> None:
    distro = manifest.get("distro", "")
    m = re.fullmatch(r"([a-z]+)-[\d.]+", distro)
    if not m:
        raise SbomError(f"deb manifest of {package}: unexpected distro {distro!r}")
    if not manifest.get("packages"):
        raise SbomError(f"deb manifest of {package} lists no packages")
    for p in manifest["packages"]:
        if not all(p.get(k) for k in ("name", "version", "arch", "source", "source-version", "files")):
            raise SbomError(f"deb manifest of {package}: {p.get('name')!r} lacks name, version, arch, source or files")
        qualifiers = {"arch": p["arch"], "distro": distro}
        if p["source"] != p["name"] or p["source-version"] != p["version"]:
            qualifiers["upstream"] = p["source"] + ("" if p["source-version"] == p["version"]
                                                    else "@" + p["source-version"])
        bom["components"].append(_component(
            ref=f"deb:{distro}/{p['name']}@{p['version']}", name=p["name"], version=p["version"],
            purl_=purl("deb", p["name"], p["version"], namespace=m.group(1), qualifiers=qualifiers),
            props={"source": "dpkg", "platforms": "linux", "bundled-in": package,
                   "bundled-files": ",".join(p["files"]), "source-package": f"{p['source']} {p['source-version']}",
                   "version-source": f"dpkg database of the {distro} image {package} is built in"}))
    for rel in manifest.get("unpackaged", []):
        print(f"{package}: {rel} has no dpkg package")
    _set_dependencies(bom)


# ── validation ──────────────────────────────────────────────────────────────


def check_invariants(bom: dict) -> list[str]:
    errors = []
    refs = [c.get("bom-ref") for c in bom.get("components", [])]
    dupes = sorted({r for r in refs if refs.count(r) > 1})
    if dupes:
        errors.append(f"duplicate bom-refs: {', '.join(dupes)}")
    known = set(refs) | {bom.get("metadata", {}).get("component", {}).get("bom-ref")}
    for dep in bom.get("dependencies", []):
        for ref in [dep["ref"], *dep.get("dependsOn", [])]:
            if ref not in known:
                errors.append(f"dependency refers to unknown bom-ref {ref}")
    for c in bom.get("components", []):
        if _prop(c, "source") == "cpm" and not (_prop(c, "git-commit") and c.get("version") and c.get("purl")):
            errors.append(f"CPM component {c.get('name')} lacks version, purl or git-commit")
    return errors


def validate_bom(text: str) -> list[str]:
    from cyclonedx.schema import SchemaVersion
    from cyclonedx.validation.json import JsonStrictValidator

    errors = JsonStrictValidator(SchemaVersion.V1_6).validate_str(text, all_errors=True)
    messages = [f"{'/'.join(map(str, e.data.absolute_path)) or '<root>'}: {e.data.message}" for e in errors or []]
    return messages + check_invariants(json.loads(text))


# ── AppImage ────────────────────────────────────────────────────────────────


def appimage_payload_offset(data: bytes) -> int:
    """Offset of the SquashFS image appended to an AppImage's ELF runtime: the
    end of the ELF section header table. Lets unsquashfs read the payload
    without executing the AppImage."""
    if data[:4] != b"\x7fELF":
        raise SbomError("not an ELF file")
    endian = {1: "<", 2: ">"}.get(data[5])
    if data[4] == 2 and endian:
        shoff = struct.unpack_from(endian + "Q", data, 0x28)[0]
        shentsize, shnum = struct.unpack_from(endian + "HH", data, 0x3A)
    elif data[4] == 1 and endian:
        shoff = struct.unpack_from(endian + "I", data, 0x20)[0]
        shentsize, shnum = struct.unpack_from(endian + "HH", data, 0x2E)
    else:
        raise SbomError("unsupported ELF header")
    offset = shoff + shentsize * shnum
    if data[offset:offset + 4] not in (b"hsqs", b"sqsh"):
        raise SbomError(f"no SquashFS image at offset {offset}")
    return offset


# ── CLI ─────────────────────────────────────────────────────────────────────


def _write(bom: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(bom, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="CPM and platform components of a source tree")
    gen.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    gen.add_argument("--version", required=True)
    gen.add_argument("--commit", required=True)
    gen.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY") or DEFAULT_REPOSITORY)
    gen.add_argument("--output", type=Path, required=True)

    merge = sub.add_parser("merge", help="add what the built packages contain to a generated BOM")
    merge.add_argument("--base", type=Path, required=True)
    merge.add_argument("--scan", action="append", default=[], metavar="KIND=DIR",
                       help=f"unpacked package to scan; KIND is one of {', '.join(sorted(PACKAGE_KINDS))}")
    merge.add_argument("--syft", type=Path, action="append", default=[], help="syft CycloneDX JSON output")
    merge.add_argument("--deb-manifest", action="append", default=[], metavar="KIND=FILE",
                       help="output of appimage-debs for the package KIND")
    merge.add_argument("--require-detected", default="", help="comma-separated keys that must be found")
    merge.add_argument("--output", type=Path, required=True)

    debs = sub.add_parser("appimage-debs", help="dpkg package of every system library in an AppDir "
                                              "(run in the AppImage build image)")
    debs.add_argument("--appdir", type=Path, required=True)
    debs.add_argument("--lib-dir", type=Path, action="append", default=[],
                      help="library directory in linker search order; default: LD_LIBRARY_PATH, then "
                           + ", ".join(SYSTEM_LIB_DIRS))
    debs.add_argument("--os-release", type=Path, default=Path("/etc/os-release"))
    debs.add_argument("--output", type=Path, required=True)

    val = sub.add_parser("validate", help="CycloneDX 1.6 schema and invariant check")
    val.add_argument("files", type=Path, nargs="+")

    off = sub.add_parser("appimage-offset", help="print the SquashFS offset of an AppImage")
    off.add_argument("file", type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            bom = build_base_bom(
                cpm_text=(args.repo_root / "3rdparty/CMakeLists.txt").read_text(),
                pins=read_platform_pins(args.repo_root), version=args.version, commit=args.commit,
                repository=args.repository)
            _write(bom, args.output)
            print(f"{args.output}: {len(bom['components'])} components")
        elif args.command == "merge":
            bom = json.loads(args.base.read_text(encoding="utf-8"))
            detections: list[Detection] = []
            for spec in args.scan:
                kind, sep, directory = spec.partition("=")
                if not sep or not Path(directory).is_dir():
                    raise SbomError(f"--scan {spec!r}: expected KIND=DIR with an existing directory")
                found = detect_bundled(kind, Path(directory))
                for d in found:
                    print(f"{d.package}: {d.key} {d.version} ({d.path})")
                detections += found
            merge_detections(bom, detections, {k for k in args.require_detected.split(",") if k})
            for spec in args.deb_manifest:
                kind, sep, file = spec.partition("=")
                if not sep or kind not in PACKAGE_KINDS or not Path(file).is_file():
                    raise SbomError(f"--deb-manifest {spec!r}: expected KIND=FILE with an existing file")
                merge_deb_manifest(bom, json.loads(Path(file).read_text(encoding="utf-8")), kind)
            for syft_file in args.syft:
                merge_syft(bom, json.loads(syft_file.read_text(encoding="utf-8")))
            bom["serialNumber"] = uuid.uuid4().urn
            bom["metadata"]["timestamp"] = _now()
            _write(bom, args.output)
            print(f"{args.output}: {len(bom['components'])} components")
        elif args.command == "appimage-debs":
            lib_dirs = args.lib_dir or [Path(d) for d in os.environ.get("LD_LIBRARY_PATH", "").split(":") if d] \
                + [Path(d) for d in SYSTEM_LIB_DIRS]
            manifest = appimage_deb_manifest(args.appdir, lib_dirs=lib_dirs, system_prefixes=list(SYSTEM_PREFIXES),
                                             distro=os_release_distro(args.os_release.read_text()))
            _write(manifest, args.output)
            for p in manifest["packages"]:
                print(f"{p['name']} {p['version']}: {', '.join(p['files'])}")
            for rel in manifest["unpackaged"]:
                print(f"no dpkg package: {rel}")
        elif args.command == "validate":
            failed = False
            for f in args.files:
                errors = validate_bom(f.read_text(encoding="utf-8"))
                for e in errors:
                    print(f"::error file={f}::{e}")
                failed |= bool(errors)
                if not errors:
                    print(f"{f}: valid CycloneDX {SPEC_VERSION}")
            return 1 if failed else 0
        elif args.command == "appimage-offset":
            print(appimage_payload_offset(args.file.read_bytes()))
    except SbomError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
