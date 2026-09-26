"""Tests of the SBOM vulnerability scan (#213). Run: python -m pytest scripts/sbom

The OSV and NVD APIs are replaced by fake transports; grype's output by a
hand-written report in grype's JSON shape; Qt's advisory page by a saved copy
(#252). CVSS scores in the assertions are the NVD published base scores of
those vectors."""

from __future__ import annotations

import dataclasses
import datetime as dt
import json
import re
from pathlib import Path

import pytest

import logsquirl_vulns as vs

REPO = Path(__file__).resolve().parents[2]
TODAY = dt.date(2026, 9, 16)
# Qt's advisory page as it was on TODAY (#252).
QT_PAGE = Path(__file__).resolve().parent / "fixtures/qt-known-vulnerabilities.html"

V31_CRITICAL = "CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H"  # 9.8
V31_HIGH = "CVSS:3.1/AV:L/AC:L/PR:L/UI:R/S:U/C:H/I:H/A:H"  # 7.3
V40_CRITICAL = "CVSS:4.0/AV:N/AC:L/AT:N/PR:N/UI:N/VC:H/VI:H/VA:H/SC:N/SI:N/SA:N"  # 9.3
V2_TEN = "AV:N/AC:L/Au:N/C:C/I:C/A:C"  # 10.0, CVSS v2
V31_LOW = "CVSS:3.1/AV:L/AC:H/PR:N/UI:N/S:U/C:N/I:L/A:N"  # 2.9
# NVD's record of CVE-2026-15037 (accepted in vuln-ignore.yml) scores 2.9.
V40_LOW_15037 = "CVSS:4.0/AV:N/AC:L/AT:P/PR:N/UI:N/VC:N/VI:L/VA:N/SC:N/SI:N/SA:N/E:P"


def props(**kw) -> list[dict]:
    return [{"name": f"logsquirl:{k.replace('_', '-')}", "value": v} for k, v in kw.items()]


def bom() -> dict:
    return {
        "bomFormat": "CycloneDX", "specVersion": "1.6",
        "components": [
            {"bom-ref": "cpm:zstd", "name": "zstd", "version": "1.5.6", "scope": "required",
             "purl": "pkg:github/facebook/zstd@v1.5.6",
             "externalReferences": [{"type": "vcs", "url": "https://github.com/facebook/zstd"}],
             "properties": props(source="cpm", git_commit="7" * 40, git_tag="v1.5.6")},
            {"bom-ref": "cpm:whereami", "name": "whereami", "version": "d" * 40, "scope": "required",
             "externalReferences": [{"type": "vcs", "url": "https://github.com/gpakosz/whereami"}],
             "properties": props(source="cpm", git_commit="d" * 40)},
            {"bom-ref": "cpm:Catch2", "name": "Catch2", "version": "2.13.8", "scope": "excluded",
             "externalReferences": [{"type": "vcs", "url": "https://github.com/catchorg/Catch2"}],
             "properties": props(source="cpm", git_commit="c" * 40, git_tag="v2.13.8",
                                 excluded_reason="only linked into the test executables")},
            {"bom-ref": "platform:qt", "name": "qt", "version": "6.10.3", "scope": "required",
             "cpe": "cpe:2.3:a:qt:qt:6.10.3:*:*:*:*:*:*:*", "properties": props(source="platform")},
            {"bom-ref": "platform:openssl@3.6.2", "name": "openssl", "version": "3.6.2", "scope": "required",
             "cpe": "cpe:2.3:a:openssl:openssl:3.6.2:*:*:*:*:*:*:*", "properties": props(source="platform")},
        ],
    }


def finding(**kw) -> vs.Finding:
    base = dict(ref="cpm:zstd", component="zstd", version="1.5.6", id="CVE-2026-0001", aliases=frozenset(),
                severity="HIGH", score=7.3, summary="", sources=frozenset({"osv"}))
    return vs.Finding(**(base | kw))


# ── OSV requests ────────────────────────────────────────────────────────────


def test_osv_queries_ask_by_commit_and_by_tag_of_the_repository():
    queries = vs.osv_queries(bom())
    assert queries == [
        ("cpm:zstd", {"commit": "7" * 40}),
        ("cpm:zstd", {"package": {"ecosystem": "GIT", "name": "https://github.com/facebook/zstd"},
                      "version": "v1.5.6"}),
        ("cpm:whereami", {"commit": "d" * 40}),
    ]


def test_components_that_are_not_shipped_are_not_scanned():
    refs = {ref for ref, _ in vs.osv_queries(bom())}
    assert "cpm:Catch2" not in refs
    assert "cpm:Catch2" not in {c["bom-ref"] for c in vs.shipped_components(bom())}


class FakeOsv:
    """The two OSV endpoints the scan uses, over canned data."""

    def __init__(self, by_query: dict[str, list[list[str]]], records: dict[str, dict]):
        self.by_query = by_query  # json.dumps(query) -> pages of vulnerability ids
        self.records = records
        self.calls: list[tuple[str, str]] = []

    def __call__(self, method: str, url: str, body: dict | None) -> dict:
        self.calls.append((method, url))
        if url.endswith("/v1/querybatch"):
            results = []
            for q in body["queries"]:
                page = int(q.get("page_token", "0"))
                key = json.dumps({k: v for k, v in q.items() if k != "page_token"}, sort_keys=True)
                pages = self.by_query.get(key, [[]])
                result: dict = {}
                if pages[page]:
                    result["vulns"] = [{"id": i, "modified": "2026-01-01T00:00:00Z"} for i in pages[page]]
                if page + 1 < len(pages):
                    result["next_page_token"] = str(page + 1)
                results.append(result)
            return {"results": results}
        vuln_id = url.rsplit("/", 1)[-1]
        return self.records[vuln_id]


def key(query: dict) -> str:
    return json.dumps(query, sort_keys=True)


def test_osv_scan_follows_pages_and_turns_records_into_findings():
    fake = FakeOsv(
        by_query={
            key({"commit": "7" * 40}): [["OSV-2026-1"], ["CVE-2026-0002"]],
            key({"package": {"ecosystem": "GIT", "name": "https://github.com/facebook/zstd"},
                 "version": "v1.5.6"}): [["OSV-2026-1"]],
        },
        records={
            "OSV-2026-1": {"id": "OSV-2026-1", "summary": "Heap overflow in ZSTD_decompress"},
            "CVE-2026-0002": {"id": "CVE-2026-0002", "aliases": ["GHSA-aaaa-bbbb-cccc"],
                              "details": "Integer overflow.",
                              "severity": [{"type": "CVSS_V3", "score": V31_CRITICAL}]},
        })
    found = sorted(vs.scan_osv(bom(), fake), key=lambda f: f.id)
    assert [(f.ref, f.id, f.severity, f.score) for f in found] == [
        ("cpm:zstd", "CVE-2026-0002", "CRITICAL", 9.8),
        ("cpm:zstd", "OSV-2026-1", "UNKNOWN", None),
    ]
    assert found[0].aliases == {"GHSA-aaaa-bbbb-cccc"}
    assert found[1].summary == "Heap overflow in ZSTD_decompress"
    # CVE-2026-0002 is only on the second page; OSV-2026-1 is returned by both
    # queries but fetched once
    assert fake.calls.count(("GET", "https://api.osv.dev/v1/vulns/OSV-2026-1")) == 1


def test_an_osv_error_is_a_tooling_error():
    def broken(method, url, body):
        raise OSError("connection reset")

    with pytest.raises(vs.VulnScanError, match="OSV"):
        vs.scan_osv(bom(), broken)


# ── severity ────────────────────────────────────────────────────────────────


@pytest.mark.parametrize("vectors, labels, expected", [
    ([V31_CRITICAL], [], ("CRITICAL", 9.8)),
    ([V40_CRITICAL], [], ("CRITICAL", 9.3)),
    ([V31_HIGH], [], ("HIGH", 7.3)),
    # a CVSS v2 score of 10 is not critical: v2 over-rates, NVD no longer scores it
    ([V31_HIGH, V2_TEN], [], ("HIGH", 7.3)),
    ([V2_TEN], [], ("UNKNOWN", None)),
    # an advisory rated critical is critical whatever its vector says
    ([V31_HIGH], ["critical"], ("CRITICAL", 7.3)),
    ([], ["Medium"], ("MEDIUM", None)),
    ([], ["Negligible"], ("LOW", None)),
    ([], [], ("UNKNOWN", None)),
    (["CVSS:3.1/garbage"], [], ("UNKNOWN", None)),
])
def test_severity_is_the_worst_of_cvss_v3_v4_scores_and_labels(vectors, labels, expected):
    assert vs.severity(vectors, labels) == expected


# ── grype ───────────────────────────────────────────────────────────────────


def grype_match(ref: str, name: str, version: str, vuln_id: str, severity: str, cvss: list[tuple[str, float]],
                related: tuple[str, ...] = ()) -> dict:
    return {
        "vulnerability": {"id": vuln_id, "severity": severity, "description": f"{vuln_id} in {name}",
                          "dataSource": f"https://nvd.nist.gov/vuln/detail/{vuln_id}",
                          "cvss": [{"version": v.split("/")[0].removeprefix("CVSS:") if v.startswith("CVSS:")
                                    else "2.0", "vector": v, "metrics": {"baseScore": s}} for v, s in cvss]},
        "relatedVulnerabilities": [{"id": r} for r in related],
        "artifact": {"id": ref, "name": name, "version": version},
    }


def test_grype_matches_become_findings_of_shipped_components():
    report = {"matches": [
        grype_match("platform:qt", "qt", "6.10.3", "CVE-2026-1000", "High", [(V31_HIGH, 7.3), (V2_TEN, 10.0)]),
        grype_match("platform:openssl@3.6.2", "openssl", "3.6.2", "GHSA-xxxx-yyyy-zzzz", "Critical", [],
                    related=("CVE-2026-2000",)),
        grype_match("cpm:Catch2", "Catch2", "2.13.8", "CVE-2026-3000", "Critical", []),
    ]}
    found = vs.grype_findings(bom(), report)
    assert [(f.ref, f.id, f.severity, f.score, f.aliases, f.sources) for f in found] == [
        ("platform:qt", "CVE-2026-1000", "HIGH", 7.3, frozenset(), frozenset({"grype"})),
        ("platform:openssl@3.6.2", "GHSA-xxxx-yyyy-zzzz", "CRITICAL", None, frozenset({"CVE-2026-2000"}),
         frozenset({"grype"})),
    ]


def test_the_same_vulnerability_from_both_scanners_is_one_finding():
    merged = vs.merge_findings([
        finding(id="CVE-2026-0002", severity="HIGH", score=7.3, sources=frozenset({"osv"})),
        finding(id="GHSA-aaaa-bbbb-cccc", aliases=frozenset({"CVE-2026-0002"}), severity="CRITICAL", score=None,
                summary="from grype", sources=frozenset({"grype"})),
        finding(id="CVE-2026-0002", ref="cpm:lz4", component="lz4"),
    ])
    assert len(merged) == 2
    zstd = next(f for f in merged if f.ref == "cpm:zstd")
    assert (zstd.id, zstd.severity, zstd.score, zstd.sources) == ("CVE-2026-0002", "CRITICAL", 7.3,
                                                                 {"osv", "grype"})
    assert zstd.aliases == {"GHSA-aaaa-bbbb-cccc"}


# ── ignore file ─────────────────────────────────────────────────────────────


def test_the_repository_ignore_file_is_valid():
    vs.parse_ignore_file((REPO / "scripts/sbom/vuln-ignore.yml").read_text())


def test_ignore_entries_are_parsed():
    entries = vs.parse_ignore_file("""
ignore:
  - id: CVE-2026-0002
    component: zstd
    reason: only reachable through the dictionary builder, which LogSquirl does not call
    expires: 2026-12-31
""")
    assert entries == [vs.IgnoreEntry("CVE-2026-0002", "zstd", "only reachable through the dictionary builder, "
                                      "which LogSquirl does not call", dt.date(2026, 12, 31))]


@pytest.mark.parametrize("text, message", [
    ("ignore:\n  - {id: CVE-1, component: zstd, expires: 2026-12-31}\n", "reason"),
    ("ignore:\n  - {id: CVE-1, component: zstd, reason: '  ', expires: 2026-12-31}\n", "reason"),
    ("ignore:\n  - {id: CVE-1, component: zstd, reason: why}\n", "expires"),
    ("ignore:\n  - {id: CVE-1, component: zstd, reason: why, expires: soon}\n", "expires"),
    ("ignore:\n  - {id: CVE-1, reason: why, expires: 2026-12-31}\n", "component"),
    ("ignore:\n  - {component: zstd, reason: why, expires: 2026-12-31}\n", "id"),
    ("ignore:\n  - {id: CVE-1, component: zstd, reason: why, expires: 2026-12-31, until: x}\n", "until"),
    ("ignores: []\n", "ignore"),
    ("ignore: [\n", "YAML"),
])
def test_an_incomplete_ignore_entry_is_an_error(text, message):
    with pytest.raises(vs.VulnScanError, match=message):
        vs.parse_ignore_file(text)


def test_an_empty_ignore_list_is_fine():
    assert vs.parse_ignore_file("ignore: []\n") == []


def test_ignores_suppress_matching_findings_until_they_expire():
    findings = [
        finding(id="CVE-2026-0001"),
        finding(id="GHSA-aaaa-bbbb-cccc", aliases=frozenset({"CVE-2026-0002"})),
        finding(id="CVE-2026-0003"),
        finding(id="CVE-2026-0001", ref="cpm:lz4", component="lz4"),
    ]
    entries = [
        vs.IgnoreEntry("CVE-2026-0001", "ZSTD", "not reachable", TODAY),  # expires today: still valid
        vs.IgnoreEntry("CVE-2026-0002", "zstd", "matched by alias", dt.date(2027, 1, 1)),
        vs.IgnoreEntry("CVE-2026-0003", "zstd", "fix is being rolled out", dt.date(2026, 9, 15)),
    ]
    result, warnings = vs.apply_ignores(findings, entries, TODAY)
    assert [f.suppressed for f in result] == ["not reachable", "matched by alias", None, None]
    assert warnings == ["vuln-ignore entry CVE-2026-0003 (zstd) expired on 2026-09-15 and no longer suppresses "
                        "it; fix the component or renew the entry with a new reason"]


def test_only_unsuppressed_critical_findings_block():
    findings = [
        finding(id="A", severity="CRITICAL"),
        finding(id="B", severity="CRITICAL", suppressed="accepted"),
        finding(id="C", severity="HIGH", score=8.9),
        finding(id="D", severity="UNKNOWN", score=None),
    ]
    assert [f.id for f in vs.blocking(findings, "critical")] == ["A"]
    assert vs.blocking(findings, "none") == []


# ── SARIF ───────────────────────────────────────────────────────────────────


def test_sarif_points_at_the_pin_and_carries_suppressions(tmp_path):
    (tmp_path / "3rdparty").mkdir()
    (tmp_path / "3rdparty/CMakeLists.txt").write_text("# x\n\nCPMAddPackage(\n  NAME zstd\n  GIT_TAG abc\n)\n")
    log = vs.to_sarif([
        finding(id="CVE-2026-0002", severity="CRITICAL", score=9.8, summary="Integer overflow"),
        finding(id="CVE-2026-0003", severity="MEDIUM", score=5.0, suppressed="not reachable"),
        finding(id="CVE-2026-0004", ref="platform:qt", component="qt", version="6.10.3", severity="UNKNOWN",
                score=None),
    ], tmp_path)
    assert log["version"] == "2.1.0"
    run = log["runs"][0]
    rules = {r["id"]: r for r in run["tool"]["driver"]["rules"]}
    assert rules["CVE-2026-0002"]["properties"]["security-severity"] == "9.8"
    assert rules["CVE-2026-0003"]["properties"]["security-severity"] == "5.0"
    critical, medium, unknown = run["results"]
    assert critical["level"] == "error"
    loc = critical["locations"][0]["physicalLocation"]
    assert loc["artifactLocation"]["uri"] == "3rdparty/CMakeLists.txt"
    assert loc["region"]["startLine"] == 4
    assert "zstd 1.5.6" in critical["message"]["text"]
    assert medium["suppressions"] == [{"kind": "external", "status": "accepted", "justification": "not reachable"}]
    assert "suppressions" not in critical
    assert unknown["level"] == "warning"
    # a stable fingerprint keeps one alert per component and vulnerability across runs
    assert critical["partialFingerprints"] != medium["partialFingerprints"]


@pytest.mark.parametrize("component, ref, path, pin", [
    ("qt", "platform:qt", ".github/workflows/ci-build.yml", "qt_version:"),
    # OpenSSL's pin moved into a composite action, where the Renovate Checksums
    # workflow may push its hash (#211).
    ("openssl", "platform:openssl@3.5.8", ".github/actions/windows-openssl/action.yml", "OPENSSL_VERSION:"),
    ("boost", "platform:boost", ".github/actions/agent-setup/action.yml", "BOOST_VERSION="),
])
def test_sarif_of_a_platform_component_points_at_its_pin_in_the_repository(component, ref, path, pin):
    log = vs.to_sarif([finding(ref=ref, component=component)], REPO)
    loc = log["runs"][0]["results"][0]["locations"][0]["physicalLocation"]
    assert loc["artifactLocation"]["uri"] == path
    assert pin in (REPO / path).read_text().splitlines()[loc["region"]["startLine"] - 1]


# ── CLI ─────────────────────────────────────────────────────────────────────


@pytest.fixture
def scan_inputs(tmp_path):
    sbom = tmp_path / "sbom.cdx.json"
    sbom.write_text(json.dumps(bom()))
    grype = tmp_path / "grype.json"
    grype.write_text(json.dumps({"matches": [
        grype_match("platform:qt", "qt", "6.10.3", "CVE-2026-1000", "Critical", [(V31_CRITICAL, 9.8)])]}))
    ignore = tmp_path / "vuln-ignore.yml"
    ignore.write_text("ignore: []\n")
    return tmp_path, sbom, grype, ignore


def run_cli(tmp_path, sbom, grype, ignore, fail_on, http=None, qt_page_file=QT_PAGE, nvd=None):
    # NVD scores every Qt advisory low unless a test says otherwise, so an
    # unscored advisory does not block the tests about something else (#252).
    sarif = tmp_path / "out/vulns.sarif"
    code = vs.main(["scan", "--sbom", str(sbom), "--grype", str(grype), "--ignore", str(ignore),
                    "--sarif", str(sarif), "--fail-on", fail_on, "--repo-root", str(REPO),
                    "--qt-advisories-html", str(qt_page_file)],
                   http=http or FakeOsv({}, {}), nvd=nvd or FakeNvd(default=V31_LOW), today=TODAY,
                   sleep=lambda s: None)
    return code, sarif


def test_cli_fails_on_a_critical_finding_and_still_writes_sarif(scan_inputs, capsys):
    code, sarif = run_cli(*scan_inputs, "critical")
    assert code == 1
    assert json.loads(sarif.read_text())["runs"][0]["results"][0]["ruleId"] == "CVE-2026-1000"
    assert "CVE-2026-1000" in capsys.readouterr().out


def test_cli_passes_when_the_critical_finding_is_ignored(scan_inputs):
    tmp_path, sbom, grype, ignore = scan_inputs
    ignore.write_text("ignore:\n  - {id: CVE-2026-1000, component: qt, reason: not used, expires: 2026-10-01}\n")
    assert run_cli(tmp_path, sbom, grype, ignore, "critical")[0] == 0


def test_cli_reports_only_when_not_gating(scan_inputs):
    assert run_cli(*scan_inputs, "none")[0] == 0


def test_cli_tooling_errors_fail_even_when_not_gating(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs

    def broken(method, url, body):
        raise OSError("timeout")

    assert run_cli(tmp_path, sbom, grype, ignore, "none", http=broken)[0] == 2
    ignore.write_text("ignore:\n  - {id: CVE-2026-1000, component: qt}\n")
    assert run_cli(tmp_path, sbom, grype, ignore, "none")[0] == 2
    assert "::error::" in capsys.readouterr().err


# ── Qt advisories (#252) ────────────────────────────────────────────────────


@pytest.mark.parametrize("text, ranges", [
    # "to" and "through" include the upper bound, "before" excludes it
    ("From Qt 6.0.0 to 6.8.9, From 6.9.0 to 6.11.1", ["6.0.0 to 6.8.9", "6.9.0 to 6.11.1"]),
    ("from Qt 2.2.0 to Qt 6.8.8, from Qt 6.9.0 to Qt 6.11.1", ["2.2.0 to 6.8.8", "6.9.0 to 6.11.1"]),
    ("From Qt 6.7.0 before 6.8.8, from 6.9.0 before 6.11.1.", ["6.7.0 before 6.8.8", "6.9.0 before 6.11.1"]),
    ("From Qt 4.0.0 before Qt 6.12.0", ["4.0.0 before 6.12.0"]),
    ("from 2.2.0 to 6.8.1", ["2.2.0 to 6.8.1"]),
    ("All version of Qt up to and including 5.15.18, from 6.0.0 through 6.5.8, from 6.6.0 through 6.8.3 and 6.9.0.",
     ["up to 5.15.18", "6.0.0 to 6.5.8", "6.6.0 to 6.8.3", "6.9.0"]),
    ("All versions of Qt from versions 6.3.0 through 6.5.9, from 6.6.0 through 6.8.4, 6.9.0.",
     ["6.3.0 to 6.5.9", "6.6.0 to 6.8.4", "6.9.0"]),
    (": Up to 5.15.18, 6.0.0 to 6.5.8, and 6.6.0 to 6.7.3.", ["up to 5.15.18", "6.0.0 to 6.5.8", "6.6.0 to 6.7.3"]),
    ("From Qt 5.0.0 to 6.5.9 and from 6.6.0 to 6.8.3 and from 6.9.0 to 6.9.1",
     ["5.0.0 to 6.5.9", "6.6.0 to 6.8.3", "6.9.0 to 6.9.1"]),
    ("Qt 6.9.0", ["6.9.0"]),
    ("Qt from 6.8.0 through 6.8.3, from 6.9.0 through 6.9.1.", ["6.8.0 to 6.8.3", "6.9.0 to 6.9.1"]),
    # the second sentence narrows nothing the first does not already say
    ("From 6.8.0 up to 6.8.3. Versions before 6.6.0 are known to be unaffected.", ["6.8.0 to 6.8.3"]),
    ("before 5.15.17", ["before 5.15.17"]),
    # a two-part version is its release's first patch where that is exact:
    # as a lower bound and as an excluded upper bound (#513)
    ("From Qt 5.10 to Qt 6.8.8, from Qt 6.9.0 to Qt 6.11.1", ["5.10.0 to 6.8.8", "6.9.0 to 6.11.1"]),
    ("from 6.8 through 6.9.2", ["6.8.0 to 6.9.2"]),
    ("From Qt 6.7 before 6.9", ["6.7.0 before 6.9.0"]),
    ("before 6.2", ["before 6.2.0"]),
    ("From 6.8.0 up to 6.8.3. Versions before 6.6 are known to be unaffected.", ["6.8.0 to 6.8.3"]),
])
def test_affected_version_texts_are_read_as_ranges(text, ranges):
    assert [str(r) for r in vs.parse_affected_versions(text)] == ranges


@pytest.mark.parametrize("text", [
    "This issue affects only the Schannel functionality on Windows if it is turned on in Qt 5.15 and from Qt 6.2 "
    "when it is the default.",
    "",
    "From Qt 6.8 to 6.9",  # an included two-part version leaves open which patch releases are meant
    "from 6.0.0 through 6.9",
    "up to and including 5.15",
    "Qt 6.9",
    "From Qt 6 to 6.9",
    "From 6.0.0 to 6.8.9 on Windows",
    "6.9.x",
])
def test_affected_version_texts_that_say_something_else_are_unreadable(text):
    with pytest.raises(vs.UnreadableVersions):
        vs.parse_affected_versions(text)


@pytest.mark.parametrize("text, inside, outside", [
    ("From Qt 6.0.0 to 6.8.9, From 6.9.0 to 6.11.1", ["6.0.0", "6.8.9", "6.10.3", "6.11.1"],
     ["5.15.18", "6.8.10", "6.11.2", "6.12.0"]),
    ("From Qt 6.7.0 before 6.8.8, from 6.9.0 before 6.11.1", ["6.7.0", "6.8.7", "6.10.3"],
     ["6.6.9", "6.8.8", "6.8.9", "6.11.1"]),
    ("Up to 5.15.18, 6.9.0", ["0.1.0", "5.15.18", "6.9.0"], ["5.15.19", "6.0.0", "6.9.1"]),
    # CVE-2026-79616 and CVE-2026-78253, both fixed in 6.11.2 (#513)
    ("From Qt 5.10 to Qt 6.8.8, from Qt 6.9.0 to Qt 6.11.1", ["5.10.0", "6.8.8", "6.9.0", "6.11.1"],
     ["5.9.9", "6.8.9", "6.11.2", "6.12.0"]),
])
def test_a_version_is_affected_when_any_range_holds_it(text, inside, outside):
    ranges = vs.parse_affected_versions(text)
    assert [v for v in inside if vs.affects(ranges, v)] == inside
    assert [v for v in outside if vs.affects(ranges, v)] == []


def qt_page() -> str:
    return QT_PAGE.read_text(encoding="utf-8")


def test_only_the_qt_framework_section_of_the_page_is_read():
    advisories = vs.parse_qt_advisories(qt_page())
    ids = [a.id for a in advisories]
    assert len(ids) == 35
    assert ids[:3] == ["CVE-2026-76151", "CVE-2026-19248", "CVE-2026-13326"]
    assert ids[-1] == "CVE-2023-32762"
    assert "CVE-2026-12593" not in ids  # Axivion
    network = advisories[0]
    assert network == vs.QtAdvisory(
        id="CVE-2026-76151",
        title="Out-of-bounds read (buffer over-read) vulnerability in HTTP Cache-Control response header parsing "
              "impacts Qt Framework (QtNetwork module)",
        module="Qt Network", affected="From Qt 6.0.0 to 6.8.9, From 6.9.0 to 6.11.1",
        fixed=("6.8.9", "6.11.2"))


@pytest.mark.parametrize("cve, module", [
    ("CVE-2026-19248", "Qt XML"), ("CVE-2026-13326", "Qt NFC"), ("CVE-2026-11573", "Qt XML"),
    ("CVE-2026-6210", "Qt SVG"), ("CVE-2025-14576", "Qt Declarative"), ("CVE-2025-4211", "Qt Core"),
    ("CVE-2024-36048", "Qt Network Authorization"), ("CVE-2026-9499", None),
])
def test_the_module_is_taken_from_the_title(cve, module):
    assert next(a for a in vs.parse_qt_advisories(qt_page()) if a.id == cve).module == module


def test_advisories_written_as_prose_keep_their_text_and_fixed_versions():
    advisory = next(a for a in vs.parse_qt_advisories(qt_page()) if a.id == "CVE-2024-25580")
    assert advisory.affected.startswith("An issue was discovered in gui/util/qktxhandler.cpp in Qt before 5.15.17")
    assert advisory.fixed == ("5.15.18", "6.2.13", "6.5.6", "6.7.0")


def test_every_advisory_on_the_saved_page_has_version_ranges():
    # Prose the parser cannot read is transcribed in the scanner; this fails
    # when the saved page gains an advisory neither covers.
    unreadable = []
    for advisory in vs.parse_qt_advisories(qt_page()):
        try:
            vs.qt_advisory_ranges(advisory)
        except vs.UnreadableVersions:
            unreadable.append(advisory.id)
    assert unreadable == []


def test_a_transcription_only_applies_while_the_page_says_what_was_transcribed():
    schannel = next(a for a in vs.parse_qt_advisories(qt_page()) if a.id == "CVE-2025-6338")
    assert vs.affects(vs.qt_advisory_ranges(schannel), "6.8.3")
    assert not vs.affects(vs.qt_advisory_ranges(schannel), "6.10.3")
    edited = dataclasses.replace(schannel, affected=schannel.affected.replace("Qt 6.2", "Qt 6.1"))
    with pytest.raises(vs.UnreadableVersions):
        vs.qt_advisory_ranges(edited)


@pytest.mark.parametrize("edit, message", [
    (lambda page: page.replace('id="Qt_Framework">Qt Framework<', 'id="Qt">Qt<'), "Qt Framework"),
    (lambda page: re.sub(r'(id="Qt_Framework">Qt Framework</span></h2>).*?(</div></div>)', r"\1\2", page,
                         flags=re.S), "no advisories"),
    (lambda page: page.replace("<b>Affected versions:</b>", "<b>Affects:</b>"), "Affected versions"),
    (lambda page: page.replace('id="CVE-2026-13326">CVE-2026-13326<', 'id="Qt_NFC">Qt NFC<'), "Qt NFC"),
    (lambda page: "<html><body>Service unavailable</body></html>", "Qt Framework"),
])
def test_a_changed_page_format_is_a_tooling_error(edit, message):
    with pytest.raises(vs.VulnScanError, match=message):
        vs.parse_qt_advisories(edit(qt_page()))


# The advisories #251 found on the page for Qt 6.10.3, with their fixes.
QT_6_10_3_ADVISORIES = {"CVE-2026-9499", "CVE-2026-19248", "CVE-2026-76151", "CVE-2026-6210", "CVE-2026-15037",
                        "CVE-2026-13326"}


def nvd_record(cve: str, *metrics: tuple[str, str, str]) -> dict:
    """An NVD API 2.0 response for one CVE; metrics are (key, vector, NVD's
    severity label)."""
    by_key: dict[str, list] = {}
    for key_, vector, label in metrics:
        by_key.setdefault(key_, []).append({"source": "nvd@nist.gov", "type": "Primary",
                                            "cvssData": {"vectorString": vector, "baseSeverity": label}})
    return {"totalResults": 1, "vulnerabilities": [{"cve": {"id": cve, "metrics": by_key}}]}


class FakeNvd:
    """NVD answering with the given records; any other CVE it knows but has not
    scored ("Awaiting Analysis"), or scores with the default vector."""

    def __init__(self, records: dict[str, dict] | None = None, fail: bool = False, default: str | None = None):
        self.records = records or {}
        self.fail = fail
        self.default = default
        self.asked: list[str] = []

    def __call__(self, cve: str) -> dict:
        self.asked.append(cve)
        if self.fail:
            raise OSError("HTTP 503")
        if cve in self.records:
            return self.records[cve]
        if self.default:
            return nvd_record(cve, ("cvssMetricV31", self.default, "LOW"))
        return {"totalResults": 1, "vulnerabilities": [{"cve": {"id": cve, "vulnStatus": "Awaiting Analysis",
                                                                "metrics": {}}}]}


def qt_bom(*versions: str) -> dict:
    doc = bom()
    doc["components"] = [c for c in doc["components"] if c["name"] != "qt"] + [
        {"bom-ref": f"platform:qt@{v}", "name": "qt", "version": v, "scope": "required",
         "purl": f"pkg:generic/qt@{v}", "properties": props(source="platform")} for v in versions]
    return doc


def scan_qt(doc: dict, page: str | None = None, nvd: FakeNvd | None = None, nvd_required: bool = False):
    return vs.scan_qt_advisories(doc, qt_page() if page is None else page, nvd or FakeNvd(), sleep=lambda s: None,
                                 nvd_required=nvd_required)


def test_qt_6_10_3_is_affected_by_the_advisories_of_the_page():
    findings, warnings = scan_qt(qt_bom("6.10.3"))
    assert {f.id for f in findings} == QT_6_10_3_ADVISORIES
    assert warnings == []
    xml = next(f for f in findings if f.id == "CVE-2026-19248")
    assert (xml.ref, xml.component, xml.version, xml.sources) == ("platform:qt@6.10.3", "qt", "6.10.3",
                                                                  {"qt-advisories"})
    assert xml.affected == "2.2.0 to 6.8.8, 6.9.0 to 6.11.1"
    assert xml.fixed == ("6.8.9", "6.11.2")
    assert xml.summary == "Qt XML: Unbounded recursion vulnerability in the QDomNode destructor of Qt XML impacts Qt"


@pytest.mark.parametrize("version, expected", [
    ("6.11.2", {"CVE-2026-15037"}),  # fixed only in 6.12.0
    ("6.12.0", set()),
    ("6.9.0", {"CVE-2026-76151", "CVE-2026-19248", "CVE-2026-13326", "CVE-2026-15037", "CVE-2026-9499",
               "CVE-2026-6210", "CVE-2025-14575", "CVE-2025-12385", "CVE-2025-10729", "CVE-2025-10728",
               "CVE-2025-6338", "CVE-2025-5992", "CVE-2025-5991", "CVE-2025-5683", "CVE-2025-5455"}),
])
def test_only_advisories_whose_ranges_hold_the_version_are_reported(version, expected):
    assert {f.id for f in scan_qt(qt_bom(version))[0]} == expected


def test_a_bom_without_qt_asks_neither_the_page_nor_nvd():
    doc = bom()
    doc["components"] = [c for c in doc["components"] if c["name"] != "qt"]
    nvd = FakeNvd()
    assert scan_qt(doc, page="not even html", nvd=nvd) == ([], [])
    assert nvd.asked == []


def test_severity_comes_from_nvd_once_per_cve():
    nvd = FakeNvd({"CVE-2026-19248": nvd_record("CVE-2026-19248", ("cvssMetricV31", V31_HIGH, "HIGH"),
                                                ("cvssMetricV40", V40_CRITICAL, "CRITICAL"))})
    findings, warnings = scan_qt(qt_bom("6.10.3", "6.10.2"), nvd=nvd)
    xml = [f for f in findings if f.id == "CVE-2026-19248"]
    assert [(f.version, f.severity, f.score) for f in xml] == [("6.10.3", "CRITICAL", 9.3),
                                                               ("6.10.2", "CRITICAL", 9.3)]
    assert next(f for f in findings if f.id == "CVE-2026-9499").severity == "UNSCORED"  # NVD has not scored it
    assert sorted(nvd.asked) == sorted(set(nvd.asked))
    assert warnings == []


def test_nvd_being_unreachable_leaves_the_severity_unknown_when_not_gating():
    findings, warnings = scan_qt(qt_bom("6.10.3"), nvd=FakeNvd(fail=True))
    assert {f.id for f in findings} == QT_6_10_3_ADVISORIES
    assert {f.severity for f in findings} == {"UNKNOWN"}
    assert len(warnings) == 1 and "NVD" in warnings[0] and "HTTP 503" in warnings[0]


def test_nvd_being_unreachable_when_gating_is_a_tooling_error():
    # Without NVD's score a critical Qt CVE would pass the release as unknown.
    with pytest.raises(vs.VulnScanError, match=r"NVD.*CVE-2026-.*HTTP 503"):
        scan_qt(qt_bom("6.10.3"), nvd=FakeNvd(fail=True), nvd_required=True)


def test_a_qt_advisory_nvd_has_not_scored_is_unscored():
    findings, warnings = scan_qt(qt_bom("6.11.2"), nvd=FakeNvd(), nvd_required=True)
    assert [(f.id, f.severity, f.score) for f in findings] == [("CVE-2026-15037", "UNSCORED", None)]
    assert warnings == []


def test_unscored_findings_block_when_gating_unless_ignored():
    unscored = finding(ref="platform:qt", component="qt", id="CVE-2026-15037", severity="UNSCORED", score=None)
    assert vs.blocking([unscored], "critical") == [unscored]
    assert vs.blocking([unscored], "none") == []
    assert vs.blocking([dataclasses.replace(unscored, suppressed="assessed: not reachable")], "critical") == []


def test_a_score_from_another_source_replaces_unscored():
    unscored = finding(ref="platform:qt", component="qt", id="CVE-2026-6210", severity="UNSCORED", score=None,
                       sources=frozenset({"qt-advisories"}))
    scored = finding(ref="platform:qt", component="qt", id="CVE-2026-6210", severity="LOW", score=2.9,
                     sources=frozenset({"grype"}))
    unknown = dataclasses.replace(scored, severity="UNKNOWN", score=None)
    assert [f.severity for f in vs.merge_findings([unscored, scored])] == ["LOW"]
    assert [f.severity for f in vs.merge_findings([unknown, unscored])] == ["UNSCORED"]


class FakeResponse:
    def __init__(self, body: bytes):
        self.body = body

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self) -> bytes:
        return self.body


def http_error(code: int, retry_after: str | None = None) -> vs.urllib.error.HTTPError:
    import email.message
    headers = email.message.Message()
    if retry_after is not None:
        headers["Retry-After"] = retry_after
    return vs.urllib.error.HTTPError(vs.NVD_API, code, "error", headers, None)


def fake_urlopen(monkeypatch, outcomes: list):
    slept: list[float] = []
    monkeypatch.setattr(vs.time, "sleep", slept.append)

    def urlopen(request, timeout):
        outcome = outcomes.pop(0)
        if isinstance(outcome, BaseException):
            raise outcome
        return FakeResponse(outcome)

    monkeypatch.setattr(vs.urllib.request, "urlopen", urlopen)
    return slept


def test_nvd_requests_are_retried_on_rate_limits_server_errors_and_timeouts(monkeypatch):
    # NVD answers an exhausted rate limit with 403 or 429 (#252).
    record = nvd_record("CVE-2026-15037", ("cvssMetricV40", V40_LOW_15037, "LOW"))
    slept = fake_urlopen(monkeypatch, [http_error(403), http_error(429, retry_after="17"), http_error(503),
                                       TimeoutError("read timed out"), json.dumps(record).encode()])
    assert vs.urllib_nvd("CVE-2026-15037") == record
    assert slept[1] == 17.0  # Retry-After is honoured
    assert len(slept) == 4 and all(s > 0 for s in slept)


@pytest.mark.parametrize("key, header", [("secret-key", "secret-key"), ("", None)])
def test_the_nvd_api_key_is_sent_only_when_the_secret_is_set(monkeypatch, key, header):
    # The workflows pass secrets.NVD_API_KEY, which is empty where the
    # repository has no such secret (#252).
    monkeypatch.setenv("NVD_API_KEY", key)
    requests = []

    def urlopen(request, timeout):
        requests.append(request)
        return FakeResponse(b"{}")

    monkeypatch.setattr(vs.urllib.request, "urlopen", urlopen)
    vs.urllib_nvd("CVE-2026-15037")
    assert requests[0].get_header("Apikey") == header


def test_nvd_requests_give_up_after_a_few_attempts(monkeypatch):
    fake_urlopen(monkeypatch, [http_error(429)] * 10)
    with pytest.raises(OSError, match="HTTP 429"):
        vs.urllib_nvd("CVE-2026-15037")


def test_an_nvd_request_that_is_refused_for_good_is_not_retried(monkeypatch):
    slept = fake_urlopen(monkeypatch, [http_error(404)])
    with pytest.raises(OSError, match="HTTP 404"):
        vs.urllib_nvd("CVE-2026-15037")
    assert slept == []


def test_an_advisory_whose_versions_cannot_be_read_is_reported_unconfirmed():
    page = qt_page().replace("From Qt 4.0.0 to 6.8.7, from 6.9.0 to 6.11.0.", "Qt 6 on Windows")
    findings, warnings = scan_qt(qt_bom("6.12.0"), page=page)
    assert [(f.id, f.severity, f.affected) for f in findings] == [("CVE-2026-9499", "UNKNOWN", "Qt 6 on Windows")]
    assert "cannot read" in findings[0].unconfirmed
    assert warnings == ["Qt advisory CVE-2026-9499: cannot read the affected versions 'Qt 6 on Windows' "
                        f"({vs.QT_ADVISORIES_URL}); check whether qt 6.12.0 is affected and teach the scanner "
                        "the wording"]
    assert vs.blocking([dataclasses.replace(findings[0], severity="CRITICAL")], "critical") == []


def test_a_qt_advisory_grype_also_found_is_one_finding():
    grype = vs.grype_findings(qt_bom("6.10.3"), {"matches": [
        grype_match("platform:qt@6.10.3", "qt", "6.10.3", "CVE-2026-6210", "High", [(V31_HIGH, 7.3)])]})
    merged = vs.merge_findings(grype + scan_qt(qt_bom("6.10.3"))[0])
    svg = [f for f in merged if f.id == "CVE-2026-6210"]
    assert [(f.sources, f.severity, f.affected, f.fixed) for f in svg] == [
        ({"grype", "qt-advisories"}, "HIGH", "6.7.0 before 6.8.8, 6.9.0 before 6.11.1", ("6.8.8", "6.11.1"))]


def test_sarif_of_a_qt_advisory_names_the_affected_and_fixed_versions(tmp_path):
    log = vs.to_sarif([
        finding(id="CVE-2026-19248", ref="platform:qt", component="qt", version="6.10.3", severity="UNKNOWN",
                score=None, sources=frozenset({"qt-advisories"}), affected="2.2.0 to 6.8.8, 6.9.0 to 6.11.1",
                fixed=("6.8.9", "6.11.2")),
        finding(id="CVE-2026-9499", ref="platform:qt", component="qt", version="6.10.3", severity="UNKNOWN",
                score=None, sources=frozenset({"qt-advisories"}), affected="Qt 6 on Windows",
                unconfirmed="Qt's advisory page gives affected versions the scanner cannot read"),
    ], tmp_path)
    xml, codec = log["runs"][0]["results"]
    assert xml["level"] == "warning"
    assert xml["message"]["text"] == ("qt 6.10.3 is affected by CVE-2026-19248, severity unknown, found by "
                                      "qt-advisories. Affected: 2.2.0 to 6.8.8, 6.9.0 to 6.11.1; fixed in 6.8.9, "
                                      "6.11.2.")
    assert codec["level"] == "note"
    assert codec["message"]["text"] == ("qt 6.10.3 may be affected by CVE-2026-9499, severity unknown, found by "
                                        "qt-advisories. Affected: Qt 6 on Windows. Unconfirmed: Qt's advisory page "
                                        "gives affected versions the scanner cannot read.")


def test_cli_reports_the_qt_advisories_of_the_sboms_qt(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    grype.write_text(json.dumps({"matches": []}))
    code, sarif = run_cli(tmp_path, sbom, grype, ignore, "critical")
    assert code == 0
    assert {r["ruleId"] for r in json.loads(sarif.read_text())["runs"][0]["results"]} == QT_6_10_3_ADVISORIES
    assert "| LOW | qt 6.10.3 | CVE-2026-15037 | 2.9 | qt-advisories |" in capsys.readouterr().out


def test_cli_blocks_on_an_unscored_qt_advisory_when_gating(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    grype.write_text(json.dumps({"matches": []}))
    code, sarif = run_cli(tmp_path, sbom, grype, ignore, "critical", nvd=FakeNvd())
    assert code == 1
    out, err = capsys.readouterr()
    assert "| UNSCORED | qt 6.10.3 | CVE-2026-15037 | - | qt-advisories |" in out
    assert "::error::qt 6.10.3: CVE-2026-15037 has no CVSS score in NVD yet" in out
    assert "assess it and record the decision in scripts/sbom/vuln-ignore.yml" in out
    result = next(r for r in json.loads(sarif.read_text())["runs"][0]["results"] if r["ruleId"] == "CVE-2026-15037")
    assert "severity unscored" in result["message"]["text"]


def test_cli_passes_an_unscored_qt_advisory_the_ignore_file_accepts(scan_inputs):
    tmp_path, sbom, grype, ignore = scan_inputs
    grype.write_text(json.dumps({"matches": []}))
    ignore.write_text("ignore:\n" + "".join(
        f"  - {{id: {cve}, component: qt, reason: assessed, expires: 2026-10-01}}\n" for cve in QT_6_10_3_ADVISORIES))
    assert run_cli(tmp_path, sbom, grype, ignore, "critical", nvd=FakeNvd())[0] == 0


def test_cli_only_warns_about_unscored_qt_advisories_when_not_gating(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    assert run_cli(tmp_path, sbom, grype, ignore, "none", nvd=FakeNvd())[0] == 0
    assert "::warning::qt 6.10.3: CVE-2026-15037 has no CVSS score in NVD yet" in capsys.readouterr().out


def test_cli_fails_as_a_tooling_error_when_nvd_is_unreachable_while_gating(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    assert run_cli(tmp_path, sbom, grype, ignore, "critical", nvd=FakeNvd(fail=True))[0] == 2
    assert "::error::NVD" in capsys.readouterr().err


def test_the_accepted_qt_6_11_2_advisory_passes_the_release_gate(tmp_path, capsys):
    # CVE-2026-15037 is fixed only in Qt 6.12; NVD scores it 2.9 and the
    # repository's ignore file accepts it (#251, #252).
    sbom = tmp_path / "sbom.json"
    sbom.write_text(json.dumps(qt_bom("6.11.2")))
    grype = tmp_path / "grype.json"
    grype.write_text(json.dumps({"matches": []}))
    nvd = FakeNvd({"CVE-2026-15037": nvd_record("CVE-2026-15037", ("cvssMetricV40", V40_LOW_15037, "LOW"))})
    code, _ = run_cli(tmp_path, sbom, grype, REPO / "scripts/sbom/vuln-ignore.yml", "critical", nvd=nvd)
    assert code == 0
    assert "| LOW | qt 6.11.2 | CVE-2026-15037 | 2.9 | qt-advisories | Fixed only in Qt 6.12.0" in capsys.readouterr().out


def test_cli_fetches_the_qt_advisory_page_when_none_is_given(scan_inputs):
    tmp_path, sbom, grype, ignore = scan_inputs
    fetched = []

    def fetch(url):
        fetched.append(url)
        return qt_page()

    code = vs.main(["scan", "--sbom", str(sbom), "--grype", str(grype), "--ignore", str(ignore),
                    "--sarif", str(tmp_path / "out.sarif"), "--fail-on", "none"],
                   http=FakeOsv({}, {}), nvd=FakeNvd(), fetch_page=fetch, today=TODAY, sleep=lambda s: None)
    assert (code, fetched) == (0, [vs.QT_ADVISORIES_URL])


def test_cli_fails_as_a_tooling_error_when_the_qt_page_cannot_be_read(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    changed = tmp_path / "page.html"
    changed.write_text(qt_page().replace('id="Qt_Framework">Qt Framework<', 'id="Qt">Qt<'))
    assert run_cli(tmp_path, sbom, grype, ignore, "none", qt_page_file=changed)[0] == 2
    assert "::error::Qt advisory page: no 'Qt Framework' section" in capsys.readouterr().err

    def unreachable(url):
        raise OSError("HTTP 503")

    code = vs.main(["scan", "--sbom", str(sbom), "--grype", str(grype), "--ignore", str(ignore),
                    "--sarif", str(tmp_path / "out.sarif"), "--fail-on", "none"],
                   http=FakeOsv({}, {}), nvd=FakeNvd(), fetch_page=unreachable, today=TODAY, sleep=lambda s: None)
    assert code == 2
    assert "Qt advisory page" in capsys.readouterr().err


def test_cli_warns_when_nvd_cannot_rate_the_qt_advisories(scan_inputs, capsys):
    tmp_path, sbom, grype, ignore = scan_inputs
    assert run_cli(tmp_path, sbom, grype, ignore, "none", nvd=FakeNvd(fail=True))[0] == 0
    assert "::warning::NVD severity unavailable" in capsys.readouterr().out


def test_sarif_of_a_bundled_ubuntu_package_points_at_the_appimage_build_image():
    log = vs.to_sarif([finding(ref="deb:ubuntu-22.04/libssl3@3.0.2-0ubuntu1.18", component="libssl3")], REPO)
    loc = log["runs"][0]["results"][0]["locations"][0]["physicalLocation"]
    assert loc["artifactLocation"]["uri"] == "docker/ubuntu22.04/Dockerfile"
    assert (REPO / "docker/ubuntu22.04/Dockerfile").read_text().splitlines()[loc["region"]["startLine"] - 1].startswith("FROM ")


# ── code scanning alerts ────────────────────────────────────────────────────


def alert(f: vs.Finding, number: int = 1, **kw) -> dict:
    """An alert as code scanning reports it for a finding: the message is the
    one the SARIF upload carried, which is all the alert keeps of it."""
    result = vs.to_sarif([f], REPO)["runs"][0]["results"][0]
    return {"number": number, "state": "open", "dismissed_comment": None,
            "rule": {"id": result["ruleId"]},
            "most_recent_instance": {"message": {"text": result["message"]["text"]}}} | kw


class FakeAlerts:
    """GitHub's code scanning API: alerts in pages, PATCHes recorded."""

    def __init__(self, *pages: list[dict]):
        self.pages = list(pages) or [[]]
        self.calls: list[tuple[str, str, dict | None]] = []

    def __call__(self, method: str, path: str, body: dict | None):
        self.calls.append((method, path, body))
        if method != "GET":
            return None
        page = int(re.search(r"[?&]page=(\d+)", path).group(1))
        return self.pages[page - 1] if page <= len(self.pages) else []

    @property
    def patches(self) -> list[tuple[str, dict | None]]:
        return [(path, body) for method, path, body in self.calls if method == "PATCH"]


ACCEPTED = [vs.IgnoreEntry("CVE-2026-0001", "zstd", "not reachable from LogSquirl", dt.date(2027, 1, 1))]


def test_an_alert_an_accepted_risk_covers_is_dismissed_with_its_reason():
    assert vs.alert_updates([alert(finding(id="CVE-2026-0001"), number=7)], ACCEPTED, TODAY) == [
        (7, {"state": "dismissed", "dismissed_reason": "won't fix",
             "dismissed_comment": "Accepted in scripts/sbom/vuln-ignore.yml: not reachable from LogSquirl"})]


def test_an_accepted_risk_matches_an_alert_by_alias_and_regardless_of_case():
    alerts = [alert(finding(id="GHSA-aaaa-bbbb-cccc", aliases=frozenset({"CVE-2026-0001"})), number=7)]
    entries = [dataclasses.replace(ACCEPTED[0], component="ZSTD", id="cve-2026-0001")]
    assert [n for n, _ in vs.alert_updates(alerts, entries, TODAY)] == [7]


@pytest.mark.parametrize("f", [
    finding(id="CVE-2026-0009"),  # no entry for this vulnerability
    finding(id="CVE-2026-0001", ref="cpm:lz4", component="lz4"),  # the entry is for another component
])
def test_an_alert_no_accepted_risk_covers_stays_open(f):
    assert vs.alert_updates([alert(f)], ACCEPTED, TODAY) == []


def test_an_alert_is_reopened_once_the_accepted_risk_expires_or_goes_away():
    dismissed = alert(finding(id="CVE-2026-0001"), number=7, state="dismissed",
                      dismissed_comment="Accepted in scripts/sbom/vuln-ignore.yml: not reachable from LogSquirl")
    expired = [dataclasses.replace(ACCEPTED[0], expires=dt.date(2026, 9, 15))]
    assert vs.alert_updates([dismissed], expired, TODAY) == [(7, {"state": "open"})]
    assert vs.alert_updates([dismissed], [], TODAY) == [(7, {"state": "open"})]


def test_an_alert_already_dismissed_for_the_accepted_risk_is_left_untouched():
    dismissed = alert(finding(id="CVE-2026-0001"), state="dismissed",
                      dismissed_comment="Accepted in scripts/sbom/vuln-ignore.yml: not reachable from LogSquirl")
    assert vs.alert_updates([dismissed], ACCEPTED, TODAY) == []


def test_a_dismissal_made_by_a_person_is_neither_reopened_nor_overwritten():
    by_hand = alert(finding(id="CVE-2026-0001"), state="dismissed", dismissed_comment="looked at it, not us")
    assert vs.alert_updates([by_hand], ACCEPTED, TODAY) == []
    assert vs.alert_updates([by_hand], [], TODAY) == []


def test_a_rewritten_reason_updates_the_dismissal_it_made():
    stale = alert(finding(id="CVE-2026-0001"), number=7, state="dismissed",
                  dismissed_comment="Accepted in scripts/sbom/vuln-ignore.yml: the old reason")
    assert vs.alert_updates([stale], ACCEPTED, TODAY) == [
        (7, {"state": "dismissed", "dismissed_reason": "won't fix",
             "dismissed_comment": "Accepted in scripts/sbom/vuln-ignore.yml: not reachable from LogSquirl"})]


def test_a_long_reason_is_cut_to_the_comment_length_the_api_takes():
    long = [dataclasses.replace(ACCEPTED[0], reason="word " * 200)]
    comment = vs.alert_updates([alert(finding(id="CVE-2026-0001"))], long, TODAY)[0][1]["dismissed_comment"]
    assert len(comment) == vs.MAX_COMMENT
    assert comment.startswith(vs.DISMISS_MARKER) and comment.endswith("…")


def test_an_alert_of_another_tool_is_left_alone():
    foreign = {"number": 7, "state": "open", "rule": {"id": "CVE-2026-0001"},
               "most_recent_instance": {"message": {"text": "zstd is vulnerable"}}}
    assert vs.alert_updates([foreign], ACCEPTED, TODAY) == []


def test_every_page_of_alerts_is_read_and_the_accepted_ones_dismissed():
    api = FakeAlerts([alert(finding(id=f"CVE-2026-{n:04d}"), number=n) for n in range(100)],
                     [alert(finding(id="CVE-2026-0001"), number=200)])
    assert vs.reconcile_alerts(api, "64x-lunicorn/LogSquirl", ACCEPTED, TODAY) == ["alert 1: dismissed",
                                                                                  "alert 200: dismissed"]
    assert [path for method, path, _ in api.calls if method == "GET"] == [
        "/repos/64x-lunicorn/LogSquirl/code-scanning/alerts?tool_name=logsquirl-vulns&per_page=100&page=1",
        "/repos/64x-lunicorn/LogSquirl/code-scanning/alerts?tool_name=logsquirl-vulns&per_page=100&page=2"]
    assert [path for path, _ in api.patches] == ["/repos/64x-lunicorn/LogSquirl/code-scanning/alerts/1",
                                                 "/repos/64x-lunicorn/LogSquirl/code-scanning/alerts/200"]


def test_an_unreadable_alert_list_is_a_tooling_error():
    def wrong_shape(method, path, body):
        return {"message": "Not Found"}

    with pytest.raises(vs.VulnScanError, match="expected a list"):
        vs.reconcile_alerts(wrong_shape, "64x-lunicorn/LogSquirl", ACCEPTED, TODAY)


def test_the_repository_ignore_file_dismisses_the_accepted_qt_advisory(tmp_path, capsys):
    """The accepted Qt advisory (#251) must not sit on the board as an open
    alert, which is what code scanning does with a SARIF suppression (#415)."""
    qt = finding(ref="platform:qt@6.11.2", component="qt", version="6.11.2", id="CVE-2026-15037",
                 severity="LOW", score=2.9, sources=frozenset({"qt-advisories"}),
                 affected="4.0.0 before 6.12.0", fixed=("6.12.0",))
    api = FakeAlerts([alert(qt, number=57)])
    code = vs.main(["dismiss", "--ignore", str(REPO / "scripts/sbom/vuln-ignore.yml"),
                    "--repo", "64x-lunicorn/LogSquirl"], api=api, today=TODAY)
    assert code == 0
    assert "alert 57: dismissed" in capsys.readouterr().out
    path, body = api.patches[0]
    assert path == "/repos/64x-lunicorn/LogSquirl/code-scanning/alerts/57"
    assert body["state"] == "dismissed" and body["dismissed_reason"] == "won't fix"
    assert body["dismissed_comment"].startswith(vs.DISMISS_MARKER + "Fixed only in Qt 6.12.0")


def test_the_dismiss_command_reports_when_nothing_has_to_change(tmp_path, capsys):
    ignore = tmp_path / "vuln-ignore.yml"
    ignore.write_text("ignore: []\n")
    api = FakeAlerts([alert(finding(id="CVE-2026-0009"))])
    assert vs.main(["dismiss", "--ignore", str(ignore), "--repo", "o/r"], api=api, today=TODAY) == 0
    assert "already match the accepted risks" in capsys.readouterr().out
    assert api.patches == []


def test_the_dismiss_command_warns_about_an_expired_entry(tmp_path, capsys):
    ignore = tmp_path / "vuln-ignore.yml"
    ignore.write_text("ignore:\n  - {id: CVE-2026-0001, component: zstd, reason: why, expires: 2026-09-15}\n")
    assert vs.main(["dismiss", "--ignore", str(ignore), "--repo", "o/r"], api=FakeAlerts(), today=TODAY) == 0
    assert "::warning file=" in capsys.readouterr().out


@pytest.mark.parametrize("api, message", [
    (lambda method, path, body: (_ for _ in ()).throw(OSError("GET /alerts: HTTP 403")), "HTTP 403"),
    (None, "cannot read the ignore file"),
])
def test_the_dismiss_command_fails_as_a_tooling_error(tmp_path, capsys, api, message):
    ignore = tmp_path / "vuln-ignore.yml"
    if api is None:
        api = FakeAlerts()
    else:
        ignore.write_text("ignore: []\n")
    assert vs.main(["dismiss", "--ignore", str(ignore), "--repo", "o/r"], api=api, today=TODAY) == 2
    assert message in capsys.readouterr().err


def test_the_github_transport_sends_the_workflow_token(monkeypatch):
    seen = {}

    def fake_fetch(method, url, data=None, headers=None, **kw):
        seen.update(method=method, url=url, data=data, headers=headers)
        return b'{"number": 7}'

    monkeypatch.setattr(vs, "_fetch", fake_fetch)
    monkeypatch.setenv("GITHUB_TOKEN", "t0ken")
    assert vs.urllib_github("PATCH", "/repos/o/r/code-scanning/alerts/7", {"state": "open"}) == {"number": 7}
    assert seen["url"] == "https://api.github.com/repos/o/r/code-scanning/alerts/7"
    assert seen["headers"]["Authorization"] == "Bearer t0ken"
    assert json.loads(seen["data"]) == {"state": "open"}

    monkeypatch.delenv("GITHUB_TOKEN")
    with pytest.raises(vs.VulnScanError, match="GITHUB_TOKEN"):
        vs.urllib_github("GET", "/repos/o/r/code-scanning/alerts", None)
