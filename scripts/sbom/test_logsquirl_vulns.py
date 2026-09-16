"""Tests of the SBOM vulnerability scan (#213). Run: python -m pytest scripts/sbom

The OSV API is replaced by a fake transport; grype's output by a hand-written
report in grype's JSON shape. CVSS scores in the assertions are the NVD
published base scores of those vectors."""

from __future__ import annotations

import datetime as dt
import json
from pathlib import Path

import pytest

import logsquirl_vulns as vs

REPO = Path(__file__).resolve().parents[2]
TODAY = dt.date(2026, 9, 16)

V31_CRITICAL = "CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H"  # 9.8
V31_HIGH = "CVSS:3.1/AV:L/AC:L/PR:L/UI:R/S:U/C:H/I:H/A:H"  # 7.3
V40_CRITICAL = "CVSS:4.0/AV:N/AC:L/AT:N/PR:N/UI:N/VC:H/VI:H/VA:H/SC:N/SI:N/SA:N"  # 9.3
V2_TEN = "AV:N/AC:L/Au:N/C:C/I:C/A:C"  # 10.0, CVSS v2


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


def run_cli(tmp_path, sbom, grype, ignore, fail_on, http=None):
    sarif = tmp_path / "out/vulns.sarif"
    code = vs.main(["scan", "--sbom", str(sbom), "--grype", str(grype), "--ignore", str(ignore),
                    "--sarif", str(sarif), "--fail-on", fail_on, "--repo-root", str(REPO)],
                   http=http or FakeOsv({}, {}), today=TODAY)
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
