"""Tests of the release SBOM generator (#212). Run: python -m pytest scripts/sbom"""

from __future__ import annotations

import dataclasses
import json
import re
import struct
from pathlib import Path

import pytest
from packageurl import PackageURL

import logsquirl_sbom as sb

REPO = Path(__file__).resolve().parents[2]
CPM_FILE = REPO / "3rdparty/CMakeLists.txt"
COMMIT = "0123456789abcdef0123456789abcdef01234567"
SHA = "58c12ff5d84665e26e80547e5f0d72b29ba1c5d4"


def cpm(body: str) -> list[sb.CpmPackage]:
    return sb.parse_cpm_packages(body)


def base_bom(**kw) -> dict:
    return sb.build_base_bom(cpm_text=CPM_FILE.read_text(), pins=sb.read_platform_pins(REPO),
                             version="26.9.0.900", commit=COMMIT, timestamp="2026-09-16T00:00:00Z",
                             serial="urn:uuid:00000000-0000-4000-8000-000000000000", **kw)


def by_ref(bom: dict) -> dict[str, dict]:
    return {c["bom-ref"]: c for c in bom["components"]}


def prop(comp: dict, key: str) -> str | None:
    return sb._prop(comp, key)


# ── the real 3rdparty/CMakeLists.txt ────────────────────────────────────────


def test_every_cpm_package_of_the_real_file_is_listed():
    declared = re.findall(r"cpmaddpackage\(\s*NAME\s+([\w-]+)", CPM_FILE.read_text(), re.I)
    assert declared, "the regex cross-check found nothing"
    assert [p.name for p in cpm(CPM_FILE.read_text())] == declared


def test_real_packages_are_pinned_and_carry_version_purl_and_commit():
    for comp in base_bom()["components"]:
        if prop(comp, "source") != "cpm":
            continue
        commit = prop(comp, "git-commit")
        assert re.fullmatch(r"[0-9a-f]{40}", commit), comp["name"]
        assert comp["version"] and comp["purl"], comp["name"]
        assert commit in comp["purl"] or prop(comp, "git-tag"), comp["name"]
        # purl strings are canonical: packageurl-python round-trips them unchanged
        assert PackageURL.from_string(comp["purl"]).to_string() == comp["purl"]


def test_real_conditional_packages_are_classified():
    comps = by_ref(base_bom())
    assert prop(comps["cpm:vectorscan"], "platforms") == "linux,macos"
    assert prop(comps["cpm:hyperscan"], "platforms") == "windows"
    assert prop(comps["cpm:hyperscan"], "cmake-condition") == "LOGSQUIRL_USE_VECTORSCAN AND WIN32"
    assert prop(comps["cpm:vectorscan"], "cmake-condition") == "LOGSQUIRL_USE_VECTORSCAN AND NOT WIN32"
    assert comps["cpm:sentry"]["scope"] == "required"
    assert comps["cpm:Catch2"]["scope"] == "excluded"
    assert comps["cpm:macdeployqtfix"]["scope"] == "excluded"
    assert comps["cpm:mimalloc"]["scope"] == "required"
    assert prop(comps["cpm:tbb"], "platforms") == "linux,macos,windows"


def test_real_versions_come_from_version_then_tag_comment_then_commit():
    comps = by_ref(base_bom())
    assert comps["cpm:simdutf"]["version"] == "5.6.2"
    assert comps["cpm:simdutf"]["purl"] == "pkg:github/simdutf/simdutf@v5.6.2"
    assert comps["cpm:vectorscan"]["version"] == "5.4.12"  # "# vectorscan/5.4.12", no VERSION
    assert comps["cpm:maddy"]["version"] == "602e26613e624535e2de883b3f2c98a16729d1d4"
    uchardet = comps["cpm:Uchardet"]
    assert uchardet["purl"] == PackageURL(
        type="generic", name="uchardet", version="0.0.8",
        qualifiers={"vcs_url": "git+https://gitlab.freedesktop.org/uchardet/uchardet@"
                               "ae6302a016088ad07177f86d417b20010053632b"}).to_string()
    assert uchardet["externalReferences"] == [{"type": "vcs", "url": "https://gitlab.freedesktop.org/uchardet/uchardet"}]


def test_platform_components_take_versions_from_the_repository():
    pins = sb.read_platform_pins(REPO)
    workflow = (REPO / ".github/workflows/ci-build.yml").read_text()
    assert re.search(rf"qt_version: {re.escape(pins.qt_version)}\b", workflow)
    assert f'BOOST_VERSION="{pins.boost_version}"' in (REPO / ".github/actions/agent-setup/action.yml").read_text()
    assert {"qtbase", "qt5compat", "icu"} <= set(pins.qt_archives)
    names = {c["name"] for c in base_bom()["components"] if prop(c, "source") == "platform"}
    assert names == {"qt", "openssl", "boost", "icu"}


def test_base_bom_is_valid_cyclonedx():
    bom = base_bom()
    assert sb.validate_bom(json.dumps(bom)) == []
    root = bom["dependencies"][0]
    assert root["ref"] == "logsquirl"
    assert "cpm:Catch2" not in root["dependsOn"] and "cpm:zstd" in root["dependsOn"]
    assert bom["metadata"]["component"]["purl"] == f"pkg:github/64x-lunicorn/logsquirl@{COMMIT}"


# ── parser edge cases ───────────────────────────────────────────────────────


def test_parser_reads_both_argument_layouts_and_the_tag_comment():
    one_per_line = f"""
cpmaddpackage(
  NAME
  foo
  GITHUB_REPOSITORY
  Owner/Foo
  VERSION
  1.2
  GIT_TAG
  {SHA} # v1.2
  OPTIONS
  "A ON" # not a tag
  "B OFF"
  EXCLUDE_FROM_ALL
  YES
)"""
    inline = f'CPMAddPackage(NAME bar GIT_REPOSITORY https://github.com/o/bar.git GIT_TAG {SHA} DOWNLOAD_ONLY YES)'
    foo, bar = cpm(one_per_line + "\n" + inline)
    assert (foo.name, foo.github, foo.version, foo.tag, foo.commit) == ("foo", ("Owner", "Foo"), "1.2", "v1.2", SHA)
    assert bar.github == ("o", "bar") and bar.tag is None and bar.download_only


def test_else_and_elseif_negate_the_earlier_branches():
    text = f"""
if(A)
elseif(B AND NOT (C OR D))
  if(WIN32)
  else()
    CPMAddPackage(NAME x GITHUB_REPOSITORY o/x GIT_TAG {SHA})
  endif()
endif(A)
"""
    (pkg,) = cpm(text)
    assert pkg.conditions == ("NOT A", "B AND NOT (C OR D)", "NOT WIN32")


@pytest.mark.parametrize("body, message", [
    ('CPMAddPackage("gh:o/r@1.0")', "shorthand"),
    ("CPMAddPackage(GITHUB_REPOSITORY o/r GIT_TAG %s)" % SHA, "without NAME"),
    ("CPMAddPackage(NAME r GITHUB_REPOSITORY o/r VERSION 1.0)", "no GIT_TAG"),
    ("CPMAddPackage(NAME r GITHUB_REPOSITORY o/r GIT_TAG v1.0)", "not a full commit SHA"),
    ("CPMAddPackage(NAME r GITHUB_REPOSITORY o/r GIT_TAG %s PATCH_COMMAND x)" % SHA, "unexpected"),
    ("CPMAddPackage(NAME r URL https://x/r.tgz GIT_TAG %s)" % SHA, "URL sources"),
    ("CPMAddPackage(NAME r GIT_TAG %s)" % SHA, "no GITHUB_REPOSITORY"),
    ("CPMAddPackage(NAME r GITHUB_REPOSITORY o/r GIT_TAG %s" % SHA, "unterminated"),
    ("if(A)\n", "unterminated if"),
    ("endif()", "without if"),
    ("set(X 1) stray", "without '('"),
])
def test_unparseable_input_fails_loudly(body, message):
    with pytest.raises(sb.SbomError, match=re.escape(message)):
        cpm(body)


def test_a_new_condition_must_be_classified():
    (pkg,) = cpm(f"if(SOME_NEW_OPTION)\nCPMAddPackage(NAME r GITHUB_REPOSITORY o/r GIT_TAG {SHA})\nendif()")
    with pytest.raises(sb.SbomError, match="unknown CMake condition 'SOME_NEW_OPTION'"):
        sb.cpm_component(pkg)


def test_comments_strings_and_brackets_do_not_confuse_the_parser():
    text = f"""
#[[ CPMAddPackage(NAME hidden GITHUB_REPOSITORY o/h GIT_TAG {SHA}) ]]
# CPMAddPackage(NAME alsohidden)
message("a ) ( # b")
CPMAddPackage(NAME r GITHUB_REPOSITORY o/r GIT_TAG {SHA} OPTIONS "X \\"q\\"")
"""
    assert [p.name for p in cpm(text)] == ["r"]


def test_qt_versions_that_disagree_fail(tmp_path):
    (tmp_path / ".github/workflows").mkdir(parents=True)
    (tmp_path / ".github/actions/agent-setup").mkdir(parents=True)
    (tmp_path / "docker/img").mkdir(parents=True)
    (tmp_path / ".github/workflows/ci-build.yml").write_text("            qt_version: 6.11.2\n")
    (tmp_path / ".github/actions/agent-setup/action.yml").write_text('BOOST_VERSION="1.86.0"\n')
    (tmp_path / "docker/img/Dockerfile").write_text("ENV QT_VERSION=6.12.0\n")
    with pytest.raises(sb.SbomError, match="Qt is not pinned to exactly one value"):
        sb.read_platform_pins(tmp_path)


# ── detection in unpacked packages ──────────────────────────────────────────


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x7fELF junk\x00" + data + b"\x00more junk")


@pytest.fixture
def packages(tmp_path) -> dict[str, Path]:
    app, win, mac = tmp_path / "appimage", tmp_path / "windows", tmp_path / "macos"
    write(app / "usr/lib/libQt6Core.so.6", b"Qt 6.11.2 (x86_64-little_endian-lp64 shared")
    write(app / "usr/lib/libcrypto.so.3", b"OpenSSL 3.0.2 15 Mar 2022")
    write(app / "usr/lib/libicuuc.so.73", b"\x0073.2\x00")
    write(win / "Qt6Core.dll", b"Qt 6.11.2 (x86_64")
    write(win / "libcrypto-3-x64.dll", b"OpenSSL 3.6.2  7 Apr 2026")
    write(mac / "logsquirl.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore", b"Qt 6.11.2 (arm64")
    (win / "unrelated.dll").write_bytes(b"OpenSSL 9.9.9 1 Jan 2030")  # name does not match
    return {"appimage": app, "windows": win, "macos": mac}


def detections(packages) -> list[sb.Detection]:
    return [d for kind, root in packages.items() for d in sb.detect_bundled(kind, root)]


def test_detects_bundled_versions(packages):
    found = {(d.package, d.key, d.version, d.upstream) for d in detections(packages)}
    assert found == {
        ("appimage", "qt", "6.11.2", True), ("appimage", "openssl", "3.0.2", False),
        ("appimage", "icu", "73.2", True), ("windows", "qt", "6.11.2", True),
        ("windows", "openssl", "3.6.2", True), ("macos", "qt", "6.11.2", True)}


def test_detected_versions_fill_the_platform_components(packages):
    bom = base_bom()
    sb.merge_detections(bom, detections(packages), {"qt", "openssl", "icu"})
    comps = by_ref(bom)
    assert "platform:openssl" not in comps and "platform:icu" not in comps
    win_ssl, app_ssl = comps["platform:openssl@3.6.2"], comps["platform:openssl@3.0.2"]
    assert win_ssl["cpe"] == "cpe:2.3:a:openssl:openssl:3.6.2:*:*:*:*:*:*:*"
    assert (prop(win_ssl, "platforms"), prop(win_ssl, "bundled-in")) == ("windows", "windows")
    assert "cpe" not in app_ssl and prop(app_ssl, "distribution-build")  # Ubuntu-patched build
    assert prop(app_ssl, "platforms") == "linux"
    assert comps["platform:icu@73.2"]["purl"] == "pkg:generic/icu@73.2"
    assert prop(comps["platform:qt"], "bundled-in") == "appimage,macos,windows"
    assert "platform:openssl@3.6.2" in bom["dependencies"][0]["dependsOn"]
    assert sb.validate_bom(json.dumps(bom)) == []


def test_a_bundled_qt_other_than_the_pinned_one_fails(packages):
    write(packages["macos"] / "logsquirl.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore", b"Qt 6.9.0 (arm")
    with pytest.raises(sb.SbomError, match=r"qt 6\.11\.2 is pinned, but the packages bundle 6\.9\.0"):
        sb.merge_detections(base_bom(), detections(packages))


def test_a_required_library_that_is_not_found_fails(packages):
    (packages["appimage"] / "usr/lib/libicuuc.so.73").unlink()
    with pytest.raises(sb.SbomError, match="no bundled icu"):
        sb.merge_detections(base_bom(), detections(packages), {"qt", "icu"})


def test_unknown_package_kind_fails(tmp_path):
    with pytest.raises(sb.SbomError, match="unknown package kind"):
        sb.detect_bundled("deb", tmp_path)


# ── syft ────────────────────────────────────────────────────────────────────


def syft_component(name, version, path, type_="application", **extra):
    return {"type": type_, "name": name, "version": version, **extra, "properties": [
        {"name": "syft:package:foundBy", "value": "pe-binary-package-cataloger"},
        {"name": "syft:location:0:path", "value": path}]}


SYFT = {
    "bomFormat": "CycloneDX", "specVersion": "1.6",
    "metadata": {"tools": {"components": [{"type": "application", "author": "anchore", "name": "syft",
                                           "version": "1.51.1"}]}},
    "components": [
        syft_component("Qt6", "6.11.2.0", "/windows/Qt6Core.dll"),
        syft_component("The OpenSSL Toolkit", "3.6.2", "/windows/libcrypto-3-x64.dll"),
        syft_component("oneAPI Threading Building Blocks (oneTBB)", "2021.13.0", "/windows/tbb12.dll"),
        syft_component("Microsoft® C Runtime Library", "14.51.36247.0", "/windows/msvcp140.dll"),
        syft_component("Microsoft® C Runtime Library", "14.51.36247.0", "/windows/vcruntime140.dll"),
        syft_component("logsquirl log viewer", "26.07.0.741", "/windows/logsquirl.exe"),
        syft_component("logsquirl", "26.07.0.741", "/linux/x.deb", type_="library",
                       purl="pkg:deb/logsquirl@26.07.0.741?arch=amd64"),
        syft_component("libfoo", "1.0", "/x/libfoo.jar", type_="library", purl="pkg:maven/org/foo@1.0",
                       cpe="cpe:2.3:a:foo:foo:1.0:*:*:*:*:*:*:*"),
        {"type": "file", "name": "/windows/Qt6Core.dll"},
    ],
}


def test_syft_findings_are_merged_without_duplicates(packages):
    bom = base_bom()
    sb.merge_detections(bom, detections(packages))
    before = len(bom["components"])
    sb.merge_syft(bom, json.loads(json.dumps(SYFT)))
    comps = by_ref(bom)
    added = [c for c in bom["components"] if prop(c, "source") == "syft"]
    assert len(bom["components"]) == before + 2
    assert {c["name"] for c in added} == {"Microsoft® C Runtime Library", "libfoo"}
    crt = next(c for c in added if c["name"].startswith("Microsoft"))
    assert prop(crt, "syft-paths") == "/windows/msvcp140.dll,/windows/vcruntime140.dll"
    foo = next(c for c in added if c["name"] == "libfoo")
    assert foo["purl"] == "pkg:maven/org/foo@1.0" and foo["cpe"].startswith("cpe:2.3:a:foo")
    # tbb is pinned by commit only; syft's version is kept as a hint
    assert prop(comps["cpm:tbb"], "syft-version") == "2021.13.0"
    assert prop(comps["platform:qt"], "syft-version") is None
    assert {"name": "syft", "author": "anchore", "type": "application", "version": "1.51.1"} in \
        bom["metadata"]["tools"]["components"]
    assert sb.validate_bom(json.dumps(bom)) == []


# ── Debian packages of the AppImage's system libraries (#227) ───────────────

GLIB = sb.DebPackage("libglib2.0-0", "2.72.4-0ubuntu2.4", "amd64", "glib2.0", "2.72.4-0ubuntu2.4")
SSL = sb.DebPackage("libssl3", "3.0.2-0ubuntu1.18", "amd64", "openssl", "3.0.2-0ubuntu1.18")
KRB = sb.DebPackage("libkrb5-3", "1.19.2-2ubuntu0.4", "amd64", "krb5", "1.19.2-2ubuntu0.4")


@pytest.fixture
def jammy(tmp_path):
    """An AppDir after linuxdeploy, the build image's library directories and
    its dpkg database: /lib is the usrmerge alias of /usr/lib, and dpkg knows
    some files under one spelling and some under the other."""
    appdir, root = tmp_path / "appdir", tmp_path / "root"
    system = root / "usr/lib/x86_64-linux-gnu"
    qt = root / "opt/qt/6.11.2/gcc_64/lib"
    for d in (system, qt, root / "opt/qt/6.11.2/gcc_64/plugins/platforms"):
        d.mkdir(parents=True)
    (root / "lib").symlink_to("usr/lib")
    for name in ("libglib-2.0.so.0.7200.4", "libssl.so.3", "libkrb5.so.3.3", "libgssapi_krb5.so.2.2"):
        (system / name).write_bytes(b"\x7fELF")
    (system / "libglib-2.0.so.0").symlink_to("libglib-2.0.so.0.7200.4")
    (system / "libssl.so").symlink_to("libssl.so.3")  # libssl-dev's link, what `cp libssl*` copies
    (system / "libkrb5.so.3").symlink_to("libkrb5.so.3.3")
    (system / "libgssapi_krb5.so.2").symlink_to("libgssapi_krb5.so.2.2")
    (qt / "libQt6Core.so.6").write_bytes(b"\x7fELF")
    for rel in ("usr/lib/libglib-2.0.so.0", "usr/lib/libssl.so.3", "usr/lib/libssl.so", "usr/lib/libkrb5.so.3",
                "usr/lib/libgssapi_krb5.so.2", "usr/lib/libQt6Core.so.6", "usr/plugins/platforms/libqxcb.so"):
        (appdir / rel).parent.mkdir(parents=True, exist_ok=True)
        (appdir / rel).write_bytes(b"\x7fELF")
    (appdir / "usr/lib/libssl.so.3.link").symlink_to("libssl.so.3")
    (appdir / "usr/share/doc").mkdir(parents=True)
    (appdir / "usr/share/doc/README.so").write_text("not a library")

    def at(path: str) -> str:
        return str(root) + path

    owners = {
        at("/lib/x86_64-linux-gnu/libglib-2.0.so.0.7200.4"): "libglib2.0-0:amd64",
        at("/usr/lib/x86_64-linux-gnu/libssl.so.3"): "libssl3:amd64",
        at("/usr/lib/x86_64-linux-gnu/libssl.so"): "libssl-dev:amd64",
        at("/lib/x86_64-linux-gnu/libkrb5.so.3.3"): "libkrb5-3:amd64",
        at("/usr/lib/x86_64-linux-gnu/libgssapi_krb5.so.2.2"): "libgssapi-krb5-2:amd64",
    }
    packages = {"libglib2.0-0:amd64": GLIB, "libssl3:amd64": SSL, "libkrb5-3:amd64": KRB,
                "libgssapi-krb5-2:amd64": sb.DebPackage("libgssapi-krb5-2", KRB.version, "amd64", "krb5", KRB.version)}
    return {
        "appdir": appdir, "root": root, "owners": owners,
        "kw": dict(lib_dirs=[qt, root / "lib/x86_64-linux-gnu", root / "usr/lib/x86_64-linux-gnu"],
                   system_prefixes=[at("/lib"), at("/usr/lib")], owner=owners.get, show=packages.__getitem__,
                   distro="ubuntu-22.04"),
    }


def test_deb_manifest_names_the_package_of_every_bundled_system_library(jammy):
    manifest = sb.appimage_deb_manifest(jammy["appdir"], **jammy["kw"])
    assert manifest["distro"] == "ubuntu-22.04"
    packages = {p["name"]: p for p in manifest["packages"]}
    assert set(packages) == {"libglib2.0-0", "libssl3", "libkrb5-3", "libgssapi-krb5-2"}
    # a -dev symlink copied as a file belongs to the runtime package it points to
    assert packages["libssl3"]["files"] == ["usr/lib/libssl.so", "usr/lib/libssl.so.3"]
    assert packages["libglib2.0-0"] == {"name": "libglib2.0-0", "version": "2.72.4-0ubuntu2.4", "arch": "amd64",
                                        "source": "glib2.0", "source-version": "2.72.4-0ubuntu2.4",
                                        "files": ["usr/lib/libglib-2.0.so.0"]}
    # Qt comes from aqtinstall and its plugins are not in a library directory:
    # no dpkg package, described by the Qt component instead
    assert manifest["unpackaged"] == ["usr/lib/libQt6Core.so.6", "usr/plugins/platforms/libqxcb.so"]


def test_deb_manifest_fails_on_a_system_library_dpkg_does_not_know(jammy):
    del jammy["owners"][str(jammy["root"]) + "/lib/x86_64-linux-gnu/libkrb5.so.3.3"]
    with pytest.raises(sb.SbomError, match=r"libkrb5\.so\.3 .* no dpkg package"):
        sb.appimage_deb_manifest(jammy["appdir"], **jammy["kw"])


def test_deb_manifest_fails_without_any_packaged_library(tmp_path):
    (tmp_path / "usr/lib").mkdir(parents=True)
    with pytest.raises(sb.SbomError, match="no bundled system library"):
        sb.appimage_deb_manifest(tmp_path, lib_dirs=[], system_prefixes=["/usr/lib"], owner=lambda p: None,
                                 show=lambda p: GLIB, distro="ubuntu-22.04")


@pytest.mark.parametrize("output, expected", [
    ("libssl3:amd64: /usr/lib/x86_64-linux-gnu/libssl.so.3\n", "libssl3:amd64"),
    ("diversion by foo from: /usr/lib/x/libssl.so.3\ndiversion by foo to: /usr/lib/x/libssl.so.3.real\n"
     "libssl3:amd64: /usr/lib/x/libssl.so.3\n", "libssl3:amd64"),
    ("dpkg-query: no path found matching pattern /nope\n", None),
])
def test_parse_dpkg_search(output, expected):
    path = "/nope" if expected is None else output.rstrip("\n").rsplit(": ", 1)[1]
    assert sb.parse_dpkg_search(output, path) == expected


def test_parse_dpkg_search_refuses_a_file_of_two_packages():
    with pytest.raises(sb.SbomError, match="more than one package"):
        sb.parse_dpkg_search("a:amd64, b:amd64: /usr/lib/x/libz.so.1\n", "/usr/lib/x/libz.so.1")


def test_os_release_names_the_distro():
    assert sb.os_release_distro('NAME="Ubuntu"\nVERSION_ID="22.04"\nID=ubuntu\n') == "ubuntu-22.04"
    with pytest.raises(sb.SbomError, match="VERSION_ID"):
        sb.os_release_distro("ID=ubuntu\n")


@pytest.mark.parametrize("line, expected", [
    ("libssl3\t3.0.2-0ubuntu1.18\tamd64\topenssl\n", SSL),
    ("libglib2.0-0\t2.72.4-0ubuntu2.4\tamd64\tglib2.0\n", GLIB),
    ("zlib1g\t1:1.2.11.dfsg-2ubuntu9.2\tamd64\tzlib (1.2.11.dfsg-2ubuntu9.2)\n",
     sb.DebPackage("zlib1g", "1:1.2.11.dfsg-2ubuntu9.2", "amd64", "zlib", "1.2.11.dfsg-2ubuntu9.2")),
    ("libfuse2\t2.9.9-5ubuntu3\tamd64\t\n", sb.DebPackage("libfuse2", "2.9.9-5ubuntu3", "amd64", "libfuse2", "2.9.9-5ubuntu3")),
])
def test_parse_dpkg_show(line, expected):
    assert sb.parse_dpkg_show(line) == expected


def manifest() -> dict:
    zlib = sb.DebPackage("zlib1g", "1:1.2.11.dfsg-2ubuntu9.2", "amd64", "zlib", "1.2.11.dfsg-2ubuntu9.2")
    return {"distro": "ubuntu-22.04", "unpackaged": ["usr/lib/libQt6Core.so.6"], "packages": [
        {**dataclasses.asdict(p), "source-version": p.source_version, "files": files}
        for p, files in ((SSL, ["usr/lib/libssl.so.3"]), (GLIB, ["usr/lib/libglib-2.0.so.0"]),
                         (zlib, ["usr/lib/libz.so.1"]))]}


def test_merged_deb_packages_carry_the_purls_grype_matches(packages):
    bom = base_bom()
    sb.merge_detections(bom, detections(packages))
    sb.merge_deb_manifest(bom, manifest())
    comps = by_ref(bom)
    ssl = comps["deb:ubuntu-22.04/libssl3@3.0.2-0ubuntu1.18"]
    assert ssl["purl"] == "pkg:deb/ubuntu/libssl3@3.0.2-0ubuntu1.18?arch=amd64&distro=ubuntu-22.04&upstream=openssl"
    assert "cpe" not in ssl and ssl["type"] == "library" and ssl["name"] == "libssl3"
    assert prop(ssl, "bundled-in") == "appimage" and prop(ssl, "platforms") == "linux"
    assert prop(ssl, "bundled-files") == "usr/lib/libssl.so.3"
    # the source version differs from the binary version only by the epoch
    zlib = comps["deb:ubuntu-22.04/zlib1g@1:1.2.11.dfsg-2ubuntu9.2"]
    assert zlib["purl"] == ("pkg:deb/ubuntu/zlib1g@1:1.2.11.dfsg-2ubuntu9.2"
                            "?arch=amd64&distro=ubuntu-22.04&upstream=zlib%401.2.11.dfsg-2ubuntu9.2")
    for comp in (ssl, zlib):
        assert PackageURL.from_string(comp["purl"]).to_string() == comp["purl"]
    assert "deb:ubuntu-22.04/libglib2.0-0@2.72.4-0ubuntu2.4" in bom["dependencies"][0]["dependsOn"]
    assert sb.validate_bom(json.dumps(bom)) == []


@pytest.mark.parametrize("change, message", [
    (lambda m: m.update(packages=[]), "lists no packages"),
    (lambda m: m.update(distro="jammy"), "distro"),
    (lambda m: m["packages"][0].pop("version"), "lacks"),
])
def test_an_unusable_deb_manifest_fails(change, message):
    m = manifest()
    change(m)
    with pytest.raises(sb.SbomError, match=message):
        sb.merge_deb_manifest(base_bom(), m)


# ── validation, AppImage, CLI ───────────────────────────────────────────────


def test_validation_reports_schema_and_invariant_errors():
    bom = base_bom()
    bom["components"][0]["scope"] = "sometimes"
    bom["components"].append(dict(bom["components"][1]))
    bom["dependencies"].append({"ref": "nowhere"})
    errors = sb.validate_bom(json.dumps(bom))
    assert any("scope" in e for e in errors)
    assert any("duplicate bom-refs" in e for e in errors)
    assert any("unknown bom-ref nowhere" in e for e in errors)


def fake_appimage(payload_offset_pad: int = 16) -> tuple[bytes, int]:
    shoff, shentsize, shnum = 0x100, 64, 3
    header = bytearray(0x40)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<Q", header, 0x28, shoff)
    struct.pack_into("<HH", header, 0x3A, shentsize, shnum)
    end = shoff + shentsize * shnum
    return bytes(header) + b"\x00" * (end - len(header)) + b"hsqs" + b"\x00" * payload_offset_pad, end


def test_appimage_payload_offset():
    data, end = fake_appimage()
    assert sb.appimage_payload_offset(data) == end
    with pytest.raises(sb.SbomError, match="no SquashFS"):
        sb.appimage_payload_offset(data[:end] + b"xxxx")
    with pytest.raises(sb.SbomError, match="not an ELF"):
        sb.appimage_payload_offset(b"MZ" + data[2:])


def test_cli_generate_merge_validate(tmp_path, packages, capsys):
    base, final, syft = tmp_path / "base.json", tmp_path / "out/final.json", tmp_path / "syft.json"
    syft.write_text(json.dumps(SYFT))
    assert sb.main(["generate", "--repo-root", str(REPO), "--version", "1.2.3.4", "--commit", COMMIT,
                    "--repository", "64x-lunicorn/LogSquirl", "--output", str(base)]) == 0
    scans = [a for kind, root in packages.items() for a in ("--scan", f"{kind}={root}")]
    debs = tmp_path / "debs.json"
    debs.write_text(json.dumps(manifest()))
    assert sb.main(["merge", "--base", str(base), *scans, "--syft", str(syft), "--deb-manifest", f"appimage={debs}",
                    "--require-detected", "qt,openssl,icu", "--output", str(final)]) == 0
    assert sb.main(["validate", str(base), str(final)]) == 0
    assert "deb:ubuntu-22.04/libssl3@3.0.2-0ubuntu1.18" in by_ref(json.loads(final.read_text()))
    assert json.loads(base.read_text())["serialNumber"] != json.loads(final.read_text())["serialNumber"]

    assert sb.main(["merge", "--base", str(base), "--scan", f"deb={tmp_path}", "--output", str(final)]) == 1
    assert sb.main(["generate", "--version", "1", "--commit", "main", "--output", str(base)]) == 1
    assert "not a full commit SHA" in capsys.readouterr().err
