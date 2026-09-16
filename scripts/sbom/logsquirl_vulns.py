#!/usr/bin/env python3
"""Known vulnerabilities in the components of a LogSquirl SBOM (#213).

Two sources, because neither covers the whole SBOM:

grype (run by the workflow, its JSON report passed in with ``--grype``)
    matches the components that carry a CPE against NVD and the other grype
    databases: Qt, Boost and the Qt, OpenSSL and ICU builds detected in the
    packages. grype cannot match a ``pkg:github`` or ``pkg:generic`` purl, so
    it finds nothing for the CPM packages.

OSV (queried here, https://api.osv.dev)
    covers the CPM packages. Each is pinned to a commit (#209), and OSV
    resolves the affected commit ranges of its C/C++ ``GIT`` entries (OSS-Fuzz
    reports and CVEs converted from NVD) to the commits in between, so a
    commit query finds them without a version scheme or CPE. A second query by
    the upstream tag catches the entries that list affected tags but whose
    commits OSV has not resolved. osv-scanner itself is not used: it only
    queries purls of package ecosystems, which the CPM packages do not have.

Findings of both are merged per component, matched against the versioned
ignore file (id, component, reason, expiry), written as one SARIF run for code
scanning and, with ``--fail-on critical``, fail the scan when a critical
finding is not ignored.

Exit codes: 0 no blocking finding, 1 blocking findings, 2 the scan itself
failed (OSV unreachable, unreadable input, invalid ignore file), so a report
never looks clean because a source was missing.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from collections.abc import Callable, Iterable
from pathlib import Path

OSV_API = "https://api.osv.dev"
PROP = "logsquirl:"
# Ordered from worst to least severe.
SEVERITIES = ("CRITICAL", "HIGH", "MEDIUM", "LOW", "UNKNOWN")
CRITICAL_SCORE = 9.0
OSV_BATCH_SIZE = 1000  # querybatch limit

Http = Callable[[str, str, "dict | None"], dict]


class VulnScanError(Exception):
    """The scan could not run to completion; its result would be incomplete."""


@dataclasses.dataclass(frozen=True)
class Finding:
    ref: str  # bom-ref of the component
    component: str
    version: str | None
    id: str
    aliases: frozenset[str]
    severity: str  # one of SEVERITIES
    score: float | None  # highest CVSS v3/v4 base score
    summary: str
    sources: frozenset[str]  # osv, grype
    suppressed: str | None = None  # reason from the ignore file


# ── SBOM ────────────────────────────────────────────────────────────────────


def _prop(comp: dict, key: str) -> str | None:
    return next((p["value"] for p in comp.get("properties", []) if p["name"] == PROP + key), None)


def shipped_components(bom: dict) -> list[dict]:
    # Test-only and build-time packages (scope "excluded", #212) are not in the
    # released binaries, so a vulnerability in them cannot reach a user.
    return [c for c in bom.get("components", []) if c.get("scope") != "excluded"]


# ── OSV ─────────────────────────────────────────────────────────────────────


def osv_queries(bom: dict) -> list[tuple[str, dict]]:
    queries = []
    for comp in shipped_components(bom):
        commit = _prop(comp, "git-commit")
        if not commit:
            continue
        queries.append((comp["bom-ref"], {"commit": commit}))
        tag = _prop(comp, "git-tag")
        repo = next((r["url"] for r in comp.get("externalReferences", []) if r.get("type") == "vcs"), None)
        if tag and repo:
            queries.append((comp["bom-ref"], {"package": {"ecosystem": "GIT", "name": repo}, "version": tag}))
    return queries


def _batch_ids(http: Http, queries: list[dict]) -> list[set[str]]:
    """Vulnerability ids per query, following next_page_token until every
    query is exhausted."""
    ids: list[set[str]] = [set() for _ in queries]
    pending = [(i, q) for i, q in enumerate(queries)]
    while pending:
        chunk, pending = pending[:OSV_BATCH_SIZE], pending[OSV_BATCH_SIZE:]
        response = http("POST", f"{OSV_API}/v1/querybatch", {"queries": [q for _, q in chunk]})
        results = response.get("results")
        if not isinstance(results, list) or len(results) != len(chunk):
            raise VulnScanError(f"OSV querybatch returned {len(results or [])} results for {len(chunk)} queries")
        for (i, q), result in zip(chunk, results):
            ids[i].update(v["id"] for v in result.get("vulns", []))
            if result.get("next_page_token"):
                pending.append((i, q | {"page_token": result["next_page_token"]}))
    return ids


def scan_osv(bom: dict, http: Http) -> list[Finding]:
    comps = {c["bom-ref"]: c for c in shipped_components(bom)}
    queries = osv_queries(bom)
    try:
        per_query = _batch_ids(http, [q for _, q in queries])
        records: dict[str, dict] = {}
        findings = []
        for (ref, _), ids in zip(queries, per_query):
            for vuln_id in sorted(ids):
                if vuln_id not in records:
                    records[vuln_id] = http("GET", f"{OSV_API}/v1/vulns/{vuln_id}", None)
                findings.append(_osv_finding(comps[ref], records[vuln_id]))
    except VulnScanError:
        raise
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise VulnScanError(f"OSV query failed: {e}") from e
    return merge_findings(findings)


def _osv_finding(comp: dict, record: dict) -> Finding:
    vectors = [s["score"] for s in record.get("severity", []) if s.get("type", "").startswith("CVSS_")]
    # GitHub advisories carry their rating in database_specific.
    labels = [record.get("database_specific", {}).get("severity") or ""]
    for affected in record.get("affected", []):
        vectors += [s["score"] for s in affected.get("severity", []) if s.get("type", "").startswith("CVSS_")]
        labels.append((affected.get("ecosystem_specific") or {}).get("severity") or "")
    sev, score = severity(vectors, [label for label in labels if isinstance(label, str)])
    return Finding(ref=comp["bom-ref"], component=comp["name"], version=comp.get("version"), id=record["id"],
                   aliases=frozenset(record.get("aliases", [])), severity=sev, score=score,
                   summary=record.get("summary") or _first_sentence(record.get("details", "")),
                   sources=frozenset({"osv"}))


def _first_sentence(text: str) -> str:
    text = " ".join(text.split())
    m = re.match(r"(.{1,200}?[.!?])(\s|$)", text)
    return m.group(1) if m else text[:200]


def urllib_http(method: str, url: str, body: dict | None) -> dict:
    """OSV transport: JSON over HTTPS, retried with backoff on transient errors."""
    data = json.dumps(body).encode() if body is not None else None
    for attempt in range(4):
        request = urllib.request.Request(url, data=data, method=method,
                                         headers={"Content-Type": "application/json",
                                                  "User-Agent": "logsquirl-vuln-scan (#213)"})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as e:
            if e.code < 500 and e.code != 429 or attempt == 3:
                raise OSError(f"{method} {url}: HTTP {e.code}") from e
        except (urllib.error.URLError, TimeoutError):
            if attempt == 3:
                raise
        time.sleep(2 ** attempt * 5)
    raise AssertionError("unreachable")


# ── severity ────────────────────────────────────────────────────────────────


def _score(vector: str) -> float | None:
    from cvss import CVSS3, CVSS4
    from cvss.exceptions import CVSSError

    # CVSS v2 is left out: it rates many issues 10.0 that v3 rates high, and
    # NVD stopped scoring with it; every current record has a v3 or v4 vector.
    try:
        if vector.startswith("CVSS:3."):
            return float(CVSS3(vector).base_score)
        if vector.startswith("CVSS:4.0/"):
            return float(CVSS4(vector).base_score)
    except CVSSError:
        return None
    return None


def _band(score: float) -> str:
    if score >= CRITICAL_SCORE:
        return "CRITICAL"
    if score >= 7.0:
        return "HIGH"
    if score >= 4.0:
        return "MEDIUM"
    return "LOW"


_LABELS = {"critical": "CRITICAL", "high": "HIGH", "moderate": "MEDIUM", "medium": "MEDIUM", "low": "LOW",
           "negligible": "LOW"}


def severity(vectors: Iterable[str], labels: Iterable[str]) -> tuple[str, float | None]:
    """The worst of what the vectors score and the sources label: a finding is
    critical when any CVSS v3/v4 base score is at least 9.0 or a source rates
    it critical."""
    scores = [s for s in map(_score, vectors) if s is not None]
    score = max(scores) if scores else None
    candidates = [_band(score)] if score is not None else []
    candidates += [_LABELS[label.lower()] for label in labels if label.lower() in _LABELS]
    return min(candidates, key=SEVERITIES.index, default="UNKNOWN"), score


def _worse(a: str, b: str) -> str:
    return min(a, b, key=SEVERITIES.index)


# ── grype ───────────────────────────────────────────────────────────────────


def grype_findings(bom: dict, report: dict) -> list[Finding]:
    shipped = {c["bom-ref"] for c in shipped_components(bom)}
    findings = []
    for match in report.get("matches", []):
        artifact, vuln = match["artifact"], match["vulnerability"]
        # grype keeps the bom-ref as the artifact id of an SBOM input.
        if artifact.get("id") not in shipped:
            continue
        vectors = [c["vector"] for c in vuln.get("cvss", []) if c.get("vector")]
        sev, score = severity(vectors, [vuln.get("severity", "")])
        findings.append(Finding(
            ref=artifact["id"], component=artifact["name"], version=artifact.get("version") or None, id=vuln["id"],
            aliases=frozenset(r["id"] for r in match.get("relatedVulnerabilities", []) if r["id"] != vuln["id"]),
            severity=sev, score=score, summary=_first_sentence(vuln.get("description", "")),
            sources=frozenset({"grype"})))
    return merge_findings(findings)


# ── merge ───────────────────────────────────────────────────────────────────


def _id_rank(vuln_id: str) -> tuple[int, str]:
    # A CVE id is what advisories, the ignore file and people refer to.
    return (0 if vuln_id.startswith("CVE-") else 1, vuln_id)


def merge_findings(findings: Iterable[Finding]) -> list[Finding]:
    """One finding per component and vulnerability: the same issue reported
    under different ids (CVE, GHSA, OSV) by either source is merged when the
    ids or aliases overlap."""
    merged: list[Finding] = []
    for f in findings:
        names = {f.id} | f.aliases
        same = [m for m in merged if m.ref == f.ref and names & ({m.id} | m.aliases)]
        for m in same:
            merged.remove(m)
            names |= {m.id} | m.aliases
        group = [f, *same]
        primary = min((g.id for g in group), key=_id_rank)
        scores = [g.score for g in group if g.score is not None]
        sev = "UNKNOWN"
        for g in group:
            sev = _worse(sev, g.severity)
        merged.append(dataclasses.replace(
            f, id=primary, aliases=frozenset(names - {primary}), severity=sev,
            score=max(scores) if scores else None,
            summary=next((g.summary for g in group if g.summary), ""),
            sources=frozenset().union(*(g.sources for g in group))))
    return merged


# ── ignore file ─────────────────────────────────────────────────────────────


@dataclasses.dataclass(frozen=True)
class IgnoreEntry:
    id: str
    component: str
    reason: str
    expires: _dt.date


_IGNORE_KEYS = ("id", "component", "reason", "expires")


def parse_ignore_file(text: str) -> list[IgnoreEntry]:
    """Every accepted risk names the vulnerability, the component, why it is
    accepted and until when; an entry missing any of them is refused rather
    than suppressing something forever or for no stated reason."""
    import yaml

    try:
        doc = yaml.safe_load(text)
    except yaml.YAMLError as e:
        raise VulnScanError(f"vuln-ignore: invalid YAML: {e}") from e
    if not isinstance(doc, dict) or set(doc) != {"ignore"} or not isinstance(doc["ignore"] or [], list):
        raise VulnScanError("vuln-ignore: expected a top-level 'ignore:' list and nothing else")
    entries = []
    for n, raw in enumerate(doc["ignore"] or [], start=1):
        where = f"vuln-ignore entry {n}"
        if not isinstance(raw, dict):
            raise VulnScanError(f"{where}: expected a mapping with {', '.join(_IGNORE_KEYS)}")
        unknown = sorted(set(map(str, raw)) - set(_IGNORE_KEYS))
        if unknown:
            raise VulnScanError(f"{where}: unknown keys {', '.join(unknown)}")
        for k in ("id", "component", "reason"):
            if not isinstance(raw.get(k), str) or not raw[k].strip():
                raise VulnScanError(f"{where}: '{k}' is required and must be non-empty text")
        expires = raw.get("expires")
        if isinstance(expires, str):
            try:
                expires = _dt.date.fromisoformat(expires)
            except ValueError:
                expires = None
        if not isinstance(expires, _dt.date) or isinstance(expires, _dt.datetime):
            raise VulnScanError(f"{where} ({raw['id']}): 'expires' is required as a YYYY-MM-DD date")
        entries.append(IgnoreEntry(raw["id"].strip(), raw["component"].strip(), " ".join(raw["reason"].split()),
                                   expires))
    return entries


def apply_ignores(findings: list[Finding], entries: list[IgnoreEntry],
                  today: _dt.date) -> tuple[list[Finding], list[str]]:
    warnings = [f"vuln-ignore entry {e.id} ({e.component}) expired on {e.expires.isoformat()} and no longer "
                "suppresses it; fix the component or renew the entry with a new reason"
                for e in entries if e.expires < today]
    active = [e for e in entries if e.expires >= today]
    result = []
    for f in findings:
        names = {n.upper() for n in {f.id} | f.aliases}
        entry = next((e for e in active if e.id.upper() in names and e.component.lower() == f.component.lower()),
                     None)
        result.append(dataclasses.replace(f, suppressed=entry.reason) if entry else f)
    return result, warnings


def blocking(findings: Iterable[Finding], fail_on: str) -> list[Finding]:
    if fail_on == "none":
        return []
    return [f for f in findings if f.severity == "CRITICAL" and not f.suppressed]


# ── SARIF ───────────────────────────────────────────────────────────────────

# Where a platform component's version is pinned; a CPM package points at its
# CPMAddPackage. Components found only in the built packages have no pin in the
# repository and point at the SBOM generator that describes them.
_PIN_LOCATIONS = {
    "qt": (".github/workflows/ci-build.yml", r"qt_version:"),
    "icu": (".github/workflows/ci-build.yml", r"qt_version:"),  # ICU comes with the Qt install
    "openssl": (".github/workflows/ci-build.yml", r"OPENSSL_VERSION:"),
    "boost": (".github/actions/agent-setup/action.yml", r"BOOST_VERSION="),
}
_FALLBACK_LOCATION = "scripts/sbom/logsquirl_sbom.py"


def _locate(repo_root: Path, finding: Finding) -> tuple[str, int]:
    if finding.ref.startswith("cpm:"):
        path, pattern = "3rdparty/CMakeLists.txt", rf"\bNAME\s+{re.escape(finding.component)}\b"
    else:
        path, pattern = _PIN_LOCATIONS.get(finding.component.lower(), (_FALLBACK_LOCATION, r"^"))
    try:
        for n, line in enumerate((repo_root / path).read_text(encoding="utf-8").splitlines(), start=1):
            if re.search(pattern, line):
                return path, n
    except OSError:
        pass
    return path, 1


def _url(vuln_id: str) -> str:
    if vuln_id.startswith("CVE-"):
        return f"https://nvd.nist.gov/vuln/detail/{vuln_id}"
    return f"https://osv.dev/vulnerability/{vuln_id}"


_LEVEL = {"CRITICAL": "error", "HIGH": "error", "MEDIUM": "warning", "LOW": "note", "UNKNOWN": "warning"}
# code scanning's security-severity for a finding without a CVSS score, taken
# from the middle of its band so the alert lands at the matching severity.
_BAND_SCORE = {"CRITICAL": "9.5", "HIGH": "8.0", "MEDIUM": "5.5", "LOW": "2.0", "UNKNOWN": "5.5"}


def to_sarif(findings: list[Finding], repo_root: Path) -> dict:
    rules: dict[str, dict] = {}
    results = []
    for f in sorted(findings, key=lambda f: (SEVERITIES.index(f.severity), f.component, f.id)):
        rule = rules.setdefault(f.id, {
            "id": f.id,
            "shortDescription": {"text": f"{f.id}: {f.summary}"[:1000] if f.summary else f.id},
            "helpUri": _url(f.id),
            "help": {"text": f"{f.id} affects a component LogSquirl ships. Update the component, or record an "
                             "accepted risk with reason and expiry in scripts/sbom/vuln-ignore.yml (#213).",
                     "markdown": f"[{f.id}]({_url(f.id)}) affects a component LogSquirl ships. Update the "
                                 "component, or record an accepted risk with reason and expiry in "
                                 "`scripts/sbom/vuln-ignore.yml` (#213)."},
            "properties": {"tags": ["security", "vulnerability", "sbom"], "security-severity": "0.0"},
        })
        sev_score = f"{f.score:.1f}" if f.score is not None else _BAND_SCORE[f.severity]
        rule["properties"]["security-severity"] = max(rule["properties"]["security-severity"], sev_score,
                                                      key=float)
        path, line = _locate(repo_root, f)
        also = f" (also {', '.join(sorted(f.aliases))})" if f.aliases else ""
        result = {
            "ruleId": f.id,
            "level": _LEVEL[f.severity],
            "message": {"text": f"{f.component} {f.version or '(unversioned)'} is affected by {f.id}{also}, "
                                f"severity {f.severity.lower()}"
                                f"{f' (CVSS {f.score:.1f})' if f.score is not None else ''}, "
                                f"found by {' and '.join(sorted(f.sources))}."},
            "locations": [{"physicalLocation": {"artifactLocation": {"uri": path},
                                                "region": {"startLine": line}}}],
            "partialFingerprints": {
                "logsquirlVulnerability/v1": hashlib.sha256(f"{f.ref}\0{f.id}".encode()).hexdigest()},
        }
        if f.suppressed:
            result["suppressions"] = [{"kind": "external", "status": "accepted", "justification": f.suppressed}]
        results.append(result)
    return {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [{
            "tool": {"driver": {"name": "logsquirl-vulns", "informationUri":
                                "https://github.com/64x-lunicorn/LogSquirl/blob/master/scripts/sbom/logsquirl_vulns.py",
                                "rules": list(rules.values())}},
            "results": results,
        }],
    }


# ── report ──────────────────────────────────────────────────────────────────


def _table(findings: list[Finding]) -> list[str]:
    rows = ["| Severity | Component | Vulnerability | CVSS | Found by | Ignored because |",
            "|---|---|---|---|---|---|"]
    for f in sorted(findings, key=lambda f: (SEVERITIES.index(f.severity), f.component, f.id)):
        rows.append(f"| {f.severity} | {f.component} {f.version or ''} | {f.id} | "
                    f"{f'{f.score:.1f}' if f.score is not None else '-'} | {', '.join(sorted(f.sources))} | "
                    f"{f.suppressed or ''} |")
    return rows


def main(argv: list[str] | None = None, *, http: Http = urllib_http, today: _dt.date | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    scan = sub.add_parser("scan", help="report and gate the known vulnerabilities of an SBOM")
    scan.add_argument("--sbom", type=Path, required=True)
    scan.add_argument("--grype", type=Path, required=True, help="grype JSON report of the same SBOM")
    scan.add_argument("--ignore", type=Path, required=True, help="accepted risks (vuln-ignore.yml)")
    scan.add_argument("--sarif", type=Path, required=True)
    scan.add_argument("--fail-on", choices=("critical", "none"), required=True)
    scan.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    today = today or _dt.datetime.now(_dt.timezone.utc).date()

    try:
        try:
            bom = json.loads(args.sbom.read_text(encoding="utf-8"))
            report = json.loads(args.grype.read_text(encoding="utf-8"))
            ignore_text = args.ignore.read_text(encoding="utf-8")
        except (OSError, ValueError) as e:
            raise VulnScanError(f"cannot read the scan input: {e}") from e
        entries = parse_ignore_file(ignore_text)
        findings = merge_findings(grype_findings(bom, report) + scan_osv(bom, http))
        findings, warnings = apply_ignores(findings, entries, today)
    except VulnScanError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 2

    for w in warnings:
        print(f"::warning file={args.ignore}::{w}")
    args.sarif.parent.mkdir(parents=True, exist_ok=True)
    args.sarif.write_text(json.dumps(to_sarif(findings, args.repo_root), indent=2) + "\n", encoding="utf-8")

    scanned = len(shipped_components(bom))
    open_ = [f for f in findings if not f.suppressed]
    headline = (f"{len(findings)} known vulnerabilities ({len(open_)} open, {len(findings) - len(open_)} ignored) "
                f"in {scanned} shipped components")
    lines = [f"### SBOM vulnerability scan (#213)", "", headline, ""]
    if findings:
        lines += _table(findings)
    print("\n".join(lines))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as out:
            out.write("\n".join(lines) + "\n")

    blocked = blocking(findings, args.fail_on)
    for f in blocked:
        print(f"::error::{f.component} {f.version or ''}: critical {f.id} "
              f"{f'(CVSS {f.score:.1f}) ' if f.score is not None else ''}blocks the release; update the "
              "component or record an accepted risk in scripts/sbom/vuln-ignore.yml")
    return 1 if blocked else 0


if __name__ == "__main__":
    sys.exit(main())
