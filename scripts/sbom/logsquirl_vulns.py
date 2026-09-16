#!/usr/bin/env python3
"""Known vulnerabilities in the components of a LogSquirl SBOM (#213).

Three sources, because none covers the whole SBOM:

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

Qt's advisories (read here, https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products)
    cover Qt itself (#252): NVD, and so grype, lacks the recent Qt CVEs as
    upstream Qt version ranges, and OSV has no Qt entries. The "Qt Framework"
    section of the page is matched against the SBOM's Qt versions; NVD
    (``NVD_API_KEY`` raises its rate limit) supplies the severity. A page the
    scanner can no longer read is a tooling error; a single advisory whose
    affected versions it cannot read is reported as unconfirmed. When gating,
    NVD being unreachable for a matched advisory is a tooling error too, and an
    advisory NVD has not scored yet is "unscored" and blocks until the ignore
    file records a decision: either would otherwise let a critical Qt CVE pass
    as unknown.

Findings of all three are merged per component, matched against the versioned
ignore file (id, component, reason, expiry), written as one SARIF run for code
scanning and, with ``--fail-on critical``, fail the scan when a critical
finding is not ignored.

Exit codes: 0 no blocking finding, 1 blocking findings, 2 the scan itself
failed (OSV or Qt's advisory page unreachable, NVD unreachable while gating, a
changed page format, unreadable input, invalid ignore file), so a report never
looks clean because a source was missing.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import hashlib
import html.parser
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Callable, Iterable
from pathlib import Path

OSV_API = "https://api.osv.dev"
QT_ADVISORIES_URL = "https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products"
NVD_API = "https://services.nvd.nist.gov/rest/json/cves/2.0"
PROP = "logsquirl:"
# Ordered from worst to least severe. UNSCORED: NVD knows the CVE of a matched
# Qt advisory but has no CVSS score for it yet (#252); any score another
# source gives wins over it, and it wins over a source that says nothing.
SEVERITIES = ("CRITICAL", "HIGH", "MEDIUM", "LOW", "UNSCORED", "UNKNOWN")
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
    sources: frozenset[str]  # osv, grype, qt-advisories
    suppressed: str | None = None  # reason from the ignore file
    # What the advisory says, where the source states it (Qt's advisories).
    affected: str = ""
    fixed: tuple[str, ...] = ()
    # Why the finding may not apply: its source could not say whether the
    # version is affected. Reported, but never blocks (#252).
    unconfirmed: str | None = None


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


MAX_RETRY_AFTER = 120.0


def _retry_after(error: urllib.error.HTTPError) -> float | None:
    """The delay a Retry-After header in seconds asks for, capped; None
    without one (or in its HTTP-date form, which the APIs here do not send)."""
    value = (error.headers or {}).get("Retry-After", "")
    try:
        return min(max(float(value), 0.0), MAX_RETRY_AFTER)
    except ValueError:
        return None


def _fetch(method: str, url: str, data: bytes | None = None, headers: dict[str, str] | None = None,
           attempts: int = 4, retry_statuses: Callable[[int], bool] = lambda code: code >= 500 or code == 429,
           backoff: float = 5.0, honour_retry_after: bool = False) -> bytes:
    """HTTPS request retried with exponential backoff on transient errors."""
    for attempt in range(attempts):
        request = urllib.request.Request(url, data=data, method=method,
                                         headers={"User-Agent": "logsquirl-vuln-scan (#213)"} | (headers or {}))
        delay = 2 ** attempt * backoff
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as e:
            if not retry_statuses(e.code) or attempt == attempts - 1:
                raise OSError(f"{method} {url}: HTTP {e.code}") from e
            if honour_retry_after and (wait := _retry_after(e)) is not None:
                delay = wait
        except (urllib.error.URLError, TimeoutError):
            if attempt == attempts - 1:
                raise
        time.sleep(delay)
    raise AssertionError("unreachable")


def urllib_http(method: str, url: str, body: dict | None) -> dict:
    """OSV transport: JSON over HTTPS."""
    data = json.dumps(body).encode() if body is not None else None
    return json.loads(_fetch(method, url, data, {"Content-Type": "application/json"}))


def urllib_page(url: str) -> str:
    return _fetch("GET", url).decode("utf-8")


def urllib_nvd(cve: str) -> dict:
    """One CVE from the NVD API 2.0. NVD answers an exhausted rate limit with
    403 or 429 and is often slow, and the release gate needs its score (#252),
    so those are retried as long as a rolling rate-limit window lasts: 6.5, 13,
    26 and 52 seconds, or what Retry-After asks for."""
    key = os.environ.get("NVD_API_KEY")  # empty when the workflow secret is not set
    return json.loads(_fetch("GET", f"{NVD_API}?cveId={urllib.parse.quote(cve)}",
                             headers={"apiKey": key} if key else None, attempts=5,
                             retry_statuses=lambda code: code >= 500 or code in (403, 429),
                             backoff=NVD_INTERVAL, honour_retry_after=True))


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


# ── Qt advisories ───────────────────────────────────────────────────────────


class UnreadableVersions(ValueError):
    """An affected-versions text that says more, or something else, than
    version ranges; guessing at it could miss an affected version (#252)."""


Version = tuple[int, ...]


def _version(text: str) -> Version:
    return tuple(int(part) for part in text.split("."))


@dataclasses.dataclass(frozen=True)
class VersionRange:
    low: Version | None  # None: every earlier version
    high: Version
    high_included: bool

    def __contains__(self, version: Version) -> bool:
        if self.low is not None and version < self.low:
            return False
        return version <= self.high if self.high_included else version < self.high

    def __str__(self) -> str:
        high = ".".join(map(str, self.high))
        if self.low == self.high and self.high_included:
            return high
        if self.low is None:
            return f"{'up to' if self.high_included else 'before'} {high}"
        return f"{'.'.join(map(str, self.low))} {'to' if self.high_included else 'before'} {high}"


_V = r"(\d+\.\d+\.\d+)"
_RANGE_FORMS = [
    (re.compile(rf"(?:from )?{_V} (?:to|through) {_V}"), lambda a, b: VersionRange(_version(a), _version(b), True)),
    (re.compile(rf"(?:from )?{_V} before {_V}"), lambda a, b: VersionRange(_version(a), _version(b), False)),
    (re.compile(rf"through {_V}"), lambda b: VersionRange(None, _version(b), True)),
    (re.compile(rf"before {_V}"), lambda b: VersionRange(None, _version(b), False)),
    (re.compile(_V), lambda a: VersionRange(_version(a), _version(a), True)),
]


def parse_affected_versions(text: str) -> tuple[VersionRange, ...]:
    """The version ranges of an "Affected versions:" text on Qt's advisory
    page, which is written by hand: "From Qt 6.0.0 to 6.8.9, from 6.9.0 before
    6.11.1", "All version of Qt up to and including 5.15.18, ... and 6.9.0",
    "Qt 6.9.0". "to", "through" and "up to" include the version they name,
    "before" excludes it. Anything else in the text makes it unreadable as a
    whole: a partly read text could leave out the range LogSquirl's Qt is in."""
    t = " ".join(text.lower().split())
    # A statement about versions outside the ranges adds none.
    t = re.sub(rf"\bversions? before {_V} (?:are|is) known to be unaffected\.?", "", t)
    t = t.replace("up to and including", "through").replace("up to", "through")
    t = re.sub(r"\b(?:all|versions?|of|qt)\b", " ", t)
    t = " ".join(t.strip(" :.").split())
    ranges = []
    for clause in re.split(r",|\band\b", t):
        clause = clause.strip()
        if not clause:
            continue
        form = next(((m, make) for pattern, make in _RANGE_FORMS if (m := pattern.fullmatch(clause))), None)
        if form is None:
            raise UnreadableVersions(f"cannot read {clause!r} as a version range")
        m, make = form
        ranges.append(make(*m.groups()))
    if not ranges:
        raise UnreadableVersions("no version range given")
    return tuple(ranges)


def affects(ranges: Iterable[VersionRange], version: str) -> bool:
    v = _version(version)
    return any(v in r for r in ranges)


@dataclasses.dataclass(frozen=True)
class QtAdvisory:
    id: str  # CVE id
    title: str
    module: str | None
    # The "Affected versions:" text, or for the older advisories the page gives
    # only as prose, that prose.
    affected: str
    fixed: tuple[str, ...]


class _Blocks(html.parser.HTMLParser):
    """The headings and paragraphs of a MediaWiki page as (tag, text, text of
    the paragraph's first bold part)."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.blocks: list[tuple[str, str, str]] = []
        self._tag: str | None = None
        self._text: list[str] = []
        self._bold: list[str] | None = None
        self._bold_done = False

    def handle_starttag(self, tag: str, attrs: list) -> None:
        if tag in ("h1", "h2", "h3", "p"):
            self._close()
            self._tag, self._text, self._bold, self._bold_done = tag, [], None, False
        elif tag == "b" and self._tag == "p" and not self._bold_done and self._bold is None:
            self._bold = []

    def handle_endtag(self, tag: str) -> None:
        if tag == "b" and self._bold is not None:
            self._bold_done = True
        elif tag == self._tag:
            self._close()

    def handle_data(self, data: str) -> None:
        if self._tag:
            self._text.append(data)
            if self._bold is not None and not self._bold_done:
                self._bold.append(data)

    def _close(self) -> None:
        if self._tag:
            self.blocks.append((self._tag, " ".join("".join(self._text).split()),
                                " ".join("".join(self._bold or []).split())))
        self._tag = None


_QT_MODULES = {"networkauth": "Qt Network Authorization", "network": "Qt Network", "xml": "Qt XML", "svg": "Qt SVG",
               "nfc": "Qt NFC", "declarative": "Qt Declarative", "corelib": "Qt Core", "core": "Qt Core",
               "gui": "Qt GUI", "bluetooth": "Qt Bluetooth", "5compat": "Qt5Compat",
               "imageformats": "Qt Image Formats", "multimedia": "Qt Multimedia", "quick": "Qt Quick",
               "webengine": "Qt WebEngine"}
_MODULE_RE = re.compile(rf"\bQt ?({'|'.join(_QT_MODULES)})\b", re.I)
_CVE_RE = re.compile(r"\bCVE-\d{4}-\d{4,}\b")
_FIX_VERSIONS_RE = re.compile(r"\d+\.\d+(?:\.\d+)?")


def parse_qt_advisories(page: str) -> list[QtAdvisory]:
    """The advisories of the "Qt Framework" section of
    https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products; the other
    sections are Qt's other products (Axivion). A page this cannot read the
    way it was written when this parser was (a missing section, a heading
    without a CVE id, no "Affected versions:" anywhere) is a VulnScanError: an
    empty result would look like a Qt without advisories (#252)."""
    parser = _Blocks()
    parser.feed(page)
    parser.close()
    blocks = parser.blocks
    start = next((i for i, (tag, text, _) in enumerate(blocks) if tag == "h2" and text == "Qt Framework"), None)
    if start is None:
        raise VulnScanError(f"Qt advisory page: no 'Qt Framework' section; the page format changed, "
                            f"update parse_qt_advisories ({QT_ADVISORIES_URL})")
    section: list[list[tuple[str, str, str]]] = []
    for tag, text, bold in blocks[start + 1:]:
        if tag in ("h1", "h2"):
            break
        if tag == "h3":
            section.append([(tag, text, bold)])
        elif section:
            section[-1].append((tag, text, bold))
    if not section:
        raise VulnScanError(f"Qt advisory page: no advisories in the 'Qt Framework' section ({QT_ADVISORIES_URL})")

    advisories = []
    for (_, heading, _), *paragraphs in section:
        ids = _CVE_RE.findall(heading)
        if len(ids) != 1:
            raise VulnScanError(f"Qt advisory page: the entry {heading!r} is not headed by one CVE id; the page "
                                f"format changed, update parse_qt_advisories ({QT_ADVISORIES_URL})")
        fields = {bold.rstrip(":").lower(): text[len(bold):].strip() for _, text, bold in paragraphs
                  if bold.endswith(":")}
        untitled = [(text, bold) for _, text, bold in paragraphs if not bold.endswith(":")]
        title = next((bold for text, bold in untitled if bold and text == bold), "")
        prose = " ".join(text for text, bold in untitled if text != bold)
        if "affected versions" in fields:
            affected, fixed = fields["affected versions"], fields.get("fixed", "")
        else:
            affected = prose
            fixed_in = re.search(r"\bFixed in (.*?)\.(?:\s|$)", prose)
            fixed = fixed_in.group(1) if fixed_in else ""
        module = _MODULE_RE.search(title)
        advisories.append(QtAdvisory(id=ids[0], title=title,
                                     module=_QT_MODULES[module.group(1).lower()] if module else None,
                                     affected=affected, fixed=tuple(_FIX_VERSIONS_RE.findall(fixed))))
    if not any("affected versions" in {b.rstrip(":").lower() for _, _, b in paragraphs}
               for _, *paragraphs in section):
        raise VulnScanError(f"Qt advisory page: no entry of the 'Qt Framework' section has 'Affected versions:'; "
                            f"the page format changed, update parse_qt_advisories ({QT_ADVISORIES_URL})")
    return advisories


# Advisories whose affected versions the page states only in prose, read by a
# person: the words quoted from the page and the ranges they mean. A
# transcription applies only while the page still says those words, so an
# edited advisory becomes unreadable (and reported) instead of silently keeping
# old ranges (#252). "6.3.x through 6.5.x before 6.5.5" is 6.3.0 before 6.5.5.
_TRANSCRIBED = {
    # Fixed in 6.5.10, 6.8.4 and 6.9.2: every release from 6.2 up to those,
    # including the unsupported 6.6 and 6.7 branches that got no fix.
    "CVE-2025-6338": ("if it is turned on in Qt 5.15 and from Qt 6.2 when it is the default",
                      "5.15.0 before 5.16.0, 6.2.0 before 6.5.10, 6.6.0 before 6.8.4, 6.9.0 before 6.9.2"),
    "CVE-2024-39936": ("Qt before 5.15.18, 6.x before 6.2.13, 6.3.x through 6.5.x before 6.5.7, and 6.6.x through "
                       "6.7.x before 6.7.3", "before 5.15.18, 6.0.0 before 6.2.13, 6.3.0 before 6.5.7, "
                                            "6.6.0 before 6.7.3"),
    "CVE-2024-36048": ("Qt before 5.15.17, 6.x before 6.2.13, 6.3.x through 6.5.x before 6.5.6, and 6.6.x through "
                       "6.7.x before 6.7.1", "before 5.15.17, 6.0.0 before 6.2.13, 6.3.0 before 6.5.6, "
                                            "6.6.0 before 6.7.1"),
    "CVE-2024-33861": ("This affects Qt 6.5.0->6.5.5, 6.6.x and 6.7.0.", "6.5.0 to 6.5.5, 6.6.0 to 6.7.0"),
    "CVE-2024-30161": ("In Qt 6.5.4, 6.5.5, and 6.6.2,", "6.5.4, 6.5.5, 6.6.2"),
    "CVE-2024-25580": ("Qt before 5.15.17, 6.x before 6.2.12, 6.3.x through 6.5.x before 6.5.5, and 6.6.x before "
                       "6.6.2", "before 5.15.17, 6.0.0 before 6.2.12, 6.3.0 before 6.5.5, 6.6.0 before 6.6.2"),
    "CVE-2023-51714": ("Qt before 5.15.17, 6.x before 6.2.11, 6.3.x through 6.5.x before 6.5.4, and 6.6.x before "
                       "6.6.2", "before 5.15.17, 6.0.0 before 6.2.11, 6.3.0 before 6.5.4, 6.6.0 before 6.6.2"),
    "CVE-2023-38197": ("Qt before 5.15.15, 6.x before 6.2.10, and 6.3.x through 6.5.x before 6.5.3",
                       "before 5.15.15, 6.0.0 before 6.2.10, 6.3.0 before 6.5.3"),
    "CVE-2023-45872": ("Qt before 6.2.11 and 6.3.x through 6.6.x before 6.6.1", "before 6.2.11, 6.3.0 before 6.6.1"),
    "CVE-2023-43114": ("Qt before 5.15.16, 6.x before 6.2.10, and 6.3.x through 6.5.x before 6.5.3",
                       "before 5.15.16, 6.0.0 before 6.2.10, 6.3.0 before 6.5.3"),
    "CVE-2023-32763": ("Qt before 5.15.15, 6.x before 6.2.9, and 6.3.x through 6.5.x before 6.5.1",
                       "before 5.15.15, 6.0.0 before 6.2.9, 6.3.0 before 6.5.1"),
    "CVE-2023-37369": ("Qt before 5.15.15, 6.x before 6.2.9, and 6.3.x through 6.5.x before 6.5.2",
                       "before 5.15.15, 6.0.0 before 6.2.9, 6.3.0 before 6.5.2"),
    "CVE-2023-34410": ("Qt before 5.15.15, 6.x before 6.2.9, and 6.3.x through 6.5.x before 6.5.2",
                       "before 5.15.15, 6.0.0 before 6.2.9, 6.3.0 before 6.5.2"),
    "CVE-2023-33285": ("Qt 5.x before 5.15.14, 6.x before 6.2.9, and 6.3.x through 6.5.x before 6.5.1",
                       "5.0.0 before 5.15.14, 6.0.0 before 6.2.9, 6.3.0 before 6.5.1"),
    "CVE-2023-32762": ("Qt before 5.15.14, 6.x before 6.2.9, and 6.3.x through 6.5.x before 6.5.1",
                       "before 5.15.14, 6.0.0 before 6.2.9, 6.3.0 before 6.5.1"),
}


def qt_advisory_ranges(advisory: QtAdvisory) -> tuple[VersionRange, ...]:
    try:
        return parse_affected_versions(advisory.affected)
    except UnreadableVersions:
        quoted, ranges = _TRANSCRIBED.get(advisory.id, (None, ""))
        if quoted is None or quoted not in advisory.affected:
            raise
        return parse_affected_versions(ranges)


NvdFetch = Callable[[str], dict]
# NVD allows 5 requests in 30 seconds without an API key, 50 with one.
NVD_INTERVAL = 6.5
NVD_INTERVAL_WITH_KEY = 0.7


def nvd_severity(record: dict) -> tuple[str, float | None]:
    """The worst CVSS v3/v4 rating NVD holds for a CVE, its own or the CNA's."""
    vectors, labels = [], []
    for vuln in record.get("vulnerabilities", []):
        for key, metrics in vuln.get("cve", {}).get("metrics", {}).items():
            if not key.startswith(("cvssMetricV3", "cvssMetricV4")):
                continue
            for metric in metrics:
                data = metric.get("cvssData", {})
                vectors += [data["vectorString"]] if data.get("vectorString") else []
                labels += [data["baseSeverity"]] if isinstance(data.get("baseSeverity"), str) else []
    return severity(vectors, labels)


def scan_qt_advisories(bom: dict, page: str, nvd: NvdFetch, *, sleep: Callable[[float], None] = time.sleep,
                       nvd_interval: float = NVD_INTERVAL,
                       nvd_required: bool = False) -> tuple[list[Finding], list[str]]:
    """Findings of Qt's own advisories for the SBOM's Qt components, and
    warnings. grype matches Qt only by CPE against NVD, which lacks the recent
    Qt advisories as upstream Qt ranges, and OSV has no Qt entries at all, so
    Qt's list is the source that knows them (#252). The severity comes from NVD;
    a CVE NVD has not scored yet is UNSCORED. NVD failing leaves the severity
    unknown with a warning, or with nvd_required (a gating scan) is a
    VulnScanError: an unknown severity could hide a critical CVE."""
    qts = [c for c in shipped_components(bom) if c.get("name", "").lower() == "qt"]
    if not qts:
        return [], []
    for comp in qts:
        if not re.fullmatch(r"\d+\.\d+\.\d+", comp.get("version") or ""):
            raise VulnScanError(f"Qt advisories: {comp['bom-ref']} has no X.Y.Z version to match "
                                f"({comp.get('version')!r})")
    findings: list[Finding] = []
    warnings: list[str] = []
    for advisory in parse_qt_advisories(page):
        base = dict(id=advisory.id, aliases=frozenset(), severity="UNKNOWN", score=None,
                    summary=f"{advisory.module}: {advisory.title}" if advisory.module else advisory.title,
                    sources=frozenset({"qt-advisories"}), fixed=advisory.fixed)
        try:
            ranges = qt_advisory_ranges(advisory)
        except UnreadableVersions:
            versions = ", ".join(f"{c['name']} {c['version']}" for c in qts)
            warnings.append(f"Qt advisory {advisory.id}: cannot read the affected versions {advisory.affected!r} "
                            f"({QT_ADVISORIES_URL}); check whether {versions} is affected and teach the scanner "
                            "the wording")
            findings += [Finding(ref=c["bom-ref"], component=c["name"], version=c["version"],
                                 affected=advisory.affected, **base,
                                 unconfirmed="Qt's advisory page gives affected versions the scanner cannot read")
                         for c in qts]
            continue
        affected = ", ".join(map(str, ranges))
        findings += [Finding(ref=c["bom-ref"], component=c["name"], version=c["version"], affected=affected, **base)
                     for c in qts if affects(ranges, c["version"])]

    ratings: dict[str, tuple[str, float | None]] = {}
    failed: list[str] = []
    for cve in dict.fromkeys(f.id for f in findings if not f.unconfirmed):
        if ratings or failed:
            sleep(nvd_interval)
        try:
            sev, score = nvd_severity(nvd(cve))
            ratings[cve] = ("UNSCORED" if sev == "UNKNOWN" else sev, score)
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as e:
            failed.append(f"{cve} ({e})")
    if failed and nvd_required:
        raise VulnScanError(f"NVD could not be reached for the matched Qt advisories {', '.join(failed)}; their "
                            "severity decides whether the release may ship, so the scan cannot pass without it "
                            "(re-run the job; an NVD_API_KEY raises NVD's rate limit)")
    if failed:
        warnings.append(f"NVD severity unavailable, reported as unknown: {', '.join(failed)}")
    findings = [dataclasses.replace(f, severity=ratings[f.id][0], score=ratings[f.id][1]) if f.id in ratings else f
                for f in findings]
    return findings, warnings


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
            sources=frozenset().union(*(g.sources for g in group)),
            affected=next((g.affected for g in group if g.affected), ""),
            fixed=next((g.fixed for g in group if g.fixed), ()),
            # another source matching the version confirms it
            unconfirmed=None if any(not g.unconfirmed for g in group) else f.unconfirmed))
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


# What blocks a gating scan: a critical finding, and one NVD has not scored yet,
# which could be critical; a person assesses it and records the decision in the
# ignore file (#252).
BLOCKING_SEVERITIES = ("CRITICAL", "UNSCORED")


def blocking(findings: Iterable[Finding], fail_on: str) -> list[Finding]:
    if fail_on == "none":
        return []
    return [f for f in findings if f.severity in BLOCKING_SEVERITIES and not f.suppressed and not f.unconfirmed]


def unscored(findings: Iterable[Finding]) -> list[Finding]:
    return [f for f in findings if f.severity == "UNSCORED" and not f.suppressed and not f.unconfirmed]


def _unscored_message(f: Finding) -> str:
    return (f"{f.component} {f.version or ''}: {f.id} has no CVSS score in NVD yet ({_url(f.id)}); assess it and "
            "record the decision in scripts/sbom/vuln-ignore.yml")


# ── SARIF ───────────────────────────────────────────────────────────────────

# Where a platform component's version is pinned; a CPM package points at its
# CPMAddPackage. Components found only in the built packages have no pin in the
# repository and point at the SBOM generator that describes them.
_PIN_LOCATIONS = {
    "qt": (".github/workflows/ci-build.yml", r"qt_version:"),
    "icu": (".github/workflows/ci-build.yml", r"qt_version:"),  # ICU comes with the Qt install
    # The Windows package's OpenSSL; its pin is in a composite action (#211).
    "openssl": (".github/actions/windows-openssl/action.yml", r"OPENSSL_VERSION:"),
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


# An unscored finding blocks a release until someone assesses it, so it is an
# error; its security-severity stays in the middle, as nothing is known (#252).
_LEVEL = {"CRITICAL": "error", "HIGH": "error", "MEDIUM": "warning", "LOW": "note", "UNSCORED": "error",
          "UNKNOWN": "warning"}
# code scanning's security-severity for a finding without a CVSS score, taken
# from the middle of its band so the alert lands at the matching severity.
_BAND_SCORE = {"CRITICAL": "9.5", "HIGH": "8.0", "MEDIUM": "5.5", "LOW": "2.0", "UNSCORED": "5.5", "UNKNOWN": "5.5"}


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
        detail = f" Affected: {f.affected}" if f.affected else ""
        detail += f"; fixed in {', '.join(f.fixed)}." if f.fixed else "." if detail else ""
        detail += f" Unconfirmed: {f.unconfirmed}." if f.unconfirmed else ""
        result = {
            "ruleId": f.id,
            # an advisory that may not apply is a note, whatever its severity (#252)
            "level": "note" if f.unconfirmed else _LEVEL[f.severity],
            "message": {"text": f"{f.component} {f.version or '(unversioned)'} "
                                f"{'may be' if f.unconfirmed else 'is'} affected by {f.id}{also}, "
                                f"severity {f.severity.lower()}"
                                f"{f' (CVSS {f.score:.1f})' if f.score is not None else ''}, "
                                f"found by {' and '.join(sorted(f.sources))}.{detail}"},
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
                    f"{f'{f.score:.1f}' if f.score is not None else '-'} | {', '.join(sorted(f.sources))}"
                    f"{' (unconfirmed)' if f.unconfirmed else ''} | "
                    f"{f.suppressed or ''} |")
    return rows


def _qt_page(saved: Path | None, fetch_page: Callable[[str], str]) -> str:
    try:
        return saved.read_text(encoding="utf-8") if saved else fetch_page(QT_ADVISORIES_URL)
    except (OSError, ValueError) as e:
        raise VulnScanError(f"Qt advisory page could not be read: {e}") from e


def main(argv: list[str] | None = None, *, http: Http = urllib_http, nvd: NvdFetch = urllib_nvd,
         fetch_page: Callable[[str], str] = urllib_page, sleep: Callable[[float], None] = time.sleep,
         today: _dt.date | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    sub = parser.add_subparsers(dest="command", required=True)
    scan = sub.add_parser("scan", help="report and gate the known vulnerabilities of an SBOM")
    scan.add_argument("--sbom", type=Path, required=True)
    scan.add_argument("--grype", type=Path, required=True, help="grype JSON report of the same SBOM")
    scan.add_argument("--ignore", type=Path, required=True, help="accepted risks (vuln-ignore.yml)")
    scan.add_argument("--sarif", type=Path, required=True)
    scan.add_argument("--fail-on", choices=("critical", "none"), required=True)
    scan.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    scan.add_argument("--qt-advisories-html", type=Path,
                      help=f"a saved copy of {QT_ADVISORIES_URL} to read instead of fetching it")
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
        qt_findings, warnings = scan_qt_advisories(
            bom, _qt_page(args.qt_advisories_html, fetch_page), nvd, sleep=sleep,
            nvd_interval=NVD_INTERVAL_WITH_KEY if os.environ.get("NVD_API_KEY") else NVD_INTERVAL,
            nvd_required=args.fail_on != "none")
        findings = merge_findings(grype_findings(bom, report) + scan_osv(bom, http) + qt_findings)
        findings, ignore_warnings = apply_ignores(findings, entries, today)
    except VulnScanError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 2

    for w in warnings:
        print(f"::warning::{w}")
    for w in ignore_warnings:
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
        if f.severity == "UNSCORED":
            print(f"::error::{_unscored_message(f)}; it blocks the release until then")
            continue
        print(f"::error::{f.component} {f.version or ''}: critical {f.id} "
              f"{f'(CVSS {f.score:.1f}) ' if f.score is not None else ''}blocks the release; update the "
              "component or record an accepted risk in scripts/sbom/vuln-ignore.yml")
    if not blocked:
        for f in unscored(findings):
            print(f"::warning::{_unscored_message(f)}")
    return 1 if blocked else 0


if __name__ == "__main__":
    sys.exit(main())
