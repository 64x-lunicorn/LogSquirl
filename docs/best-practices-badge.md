# OpenSSF Best Practices badge (passing level) — issue #478

Answers for the questionnaire at <https://www.bestpractices.dev>, with the evidence in this
repository. Base URL for evidence: `https://github.com/64x-lunicorn/LogSquirl` (`/blob/master/<path>`).
Status: everything is Met or N/A except the items under "Unmet or needs your confirmation".

## Steps for the maintainer

1. Sign in at <https://www.bestpractices.dev/en/login> with GitHub.
2. "Get Your Badge Now", pick `64x-lunicorn/LogSquirl` from the repository list (or paste
   `https://github.com/64x-lunicorn/LogSquirl`). Note the project id in the URL
   (`https://www.bestpractices.dev/en/projects/<PROJECT_ID>`).
3. Work through the "Passing" tab with the table below; the site auto-fills some answers.
   Paste the evidence as justification. Save; the badge turns "passing" at 100 %.
4. Decide the points under "Unmet or needs your confirmation" first (the SECURITY.md response
   times are a commitment only you can make).
5. Add the badge to README.md next to the Scorecard badge, replacing `PROJECT_ID`:

```markdown
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/PROJECT_ID/badge)](https://www.bestpractices.dev/projects/PROJECT_ID)
```

## Unmet or needs your confirmation

- **vulnerability_report_response / vulnerabilities_fixed_60_days**: SECURITY.md now states a
  14-day acknowledgement and 60-day fix target (added for #478). Keep it only if you can
  honour it; otherwise edit before answering.
- **report_responses / enhancement_responses**: "majority of reports in the last 2-12 months
  answered" is a fact of the issue history, not verifiable from files. Check the issue list.
- **SECURITY.md "Supported Versions"** still lists 26.03; latest stable tag is v26.07.0
  (v26.10.0-beta2 exists). Not a criterion, but update it.
- **know_secure_design / know_common_errors**: need the primary developer to attest; evidence
  below is supportive only.
- **Unmet outright: none found.** Suggested-level items not met: `test_most` (no coverage
  measurement in the repo; 90 % is not claimed). Fuzzing is tracked in #477 (not needed
  for passing).

## Criteria

| Criterion | Answer | Evidence |
|---|---|---|
| description_good | Met | README.md top: "A fast, open-source log explorer for Windows, macOS, and Linux." |
| interact | Met | README.md (download, issues link), CONTRIBUTING.md, `.github/ISSUE_TEMPLATE/` |
| contribution | Met | CONTRIBUTING.md |
| contribution_requirements | Met | CONTRIBUTING.md: ground rules, code style, tests, changelog, commit format |
| floss_license | Met | GPL-3.0-or-later, `COPYING` |
| floss_license_osi | Met | GPL-3.0-or-later is OSI approved |
| license_location | Met | `COPYING` at repo root; license badge in README.md |
| documentation_basics | Met | README.md (install), DOCUMENTATION.md (usage), SECURITY.md |
| documentation_interface | Met | DOCUMENTATION.md (user interface), docs/plugin-sdk.md (plugin API) |
| sites_https | Met | github.com, https://packages.lunicorn-lab.de, project website: all HTTPS |
| discussion | Met | GitHub Issues and Discussions (Q&A, Ideas), linked in CONTRIBUTING.md |
| english | Met | All docs, issues and code comments are English |
| maintained | Met | Recent commits and releases on master; CHANGELOG.md |
| repo_public | Met | https://github.com/64x-lunicorn/LogSquirl |
| repo_track | Met | git history with author and date |
| repo_interim | Met | Tags v26.06.1, v26.07.0, v26.10.0-beta2 ... (see `git tag`) |
| repo_distributed | Met | git |
| version_unique | Met | Unique release tags `vYY.MM.N` |
| version_semver | Met (suggested) | CalVer `YY.MM.patch`; answer as "other unique scheme" if SemVer is required |
| version_tags | Met | git tags per release |
| release_notes | Met | CHANGELOG.md becomes the GitHub release notes (CONTRIBUTING.md "Changelog entry"; `.github/workflows/changelog.yml`, `ci-release.yml`) |
| release_notes_vulns | Met | CHANGELOG.md `## Security` sections name CVE ids (e.g. v26.07.0 entry) |
| report_process | Met | CONTRIBUTING.md "How to report a bug"; `.github/ISSUE_TEMPLATE/bug_report.yml` |
| report_tracker | Met | GitHub Issues |
| report_responses | Confirm | Check issue history (see above) |
| enhancement_responses | Confirm | Check issue history; Ideas discussions |
| report_archive | Met | GitHub Issues are public and searchable |
| vulnerability_report_process | Met | SECURITY.md "Reporting a Vulnerability"; CONTRIBUTING.md |
| vulnerability_report_private | Met | GitHub private security advisories, link in SECURITY.md |
| vulnerability_report_response | Met once confirmed | SECURITY.md: acknowledge within 14 days (criterion requires <= 14) |
| build | Met | CMakeLists.txt, BUILD.md |
| build_common_tools | Met | CMake |
| build_floss_tools | Met | CMake, GCC/Clang, Qt (LGPL/GPL); MSVC on Windows is proprietary but free-of-charge, GCC/Clang builds exist on Linux/macOS |
| test | Met | Catch2 unit tests `tests/unit`, `tests/ui`, pytest e2e `tests/e2e`, run via ctest |
| test_invocation | Met | `ctest`, BUILD.md "Running tests" |
| test_most | Unmet (suggested) | No coverage measurement |
| test_continuous_integration | Met | `.github/workflows/ci-build.yml` |
| test_policy | Met | CONTRIBUTING.md "Tests for new functionality" and "Testing checklist" |
| tests_are_added | Met | Recent PRs add tests (e.g. #448 `translations_shipped`); verify on a few recent PRs |
| tests_documented_added | Met | CONTRIBUTING.md |
| warnings | Met | `cmake/CompilerWarnings.cmake` (-Wall -Wextra -Wpedantic, MSVC /W4-level) |
| warnings_fixed | Met | `WARNINGS_AS_ERRORS` default TRUE; tests/`project_warnings_cover_project_targets.cmake` |
| warnings_strict | Met | -Wextra -Wpedantic, -Werror |
| know_secure_design | Confirm | Verified release supply chain (ci-release.yml), ADR 0005 (update feed trust), least-privilege workflows, zizmor, Scorecard |
| know_common_errors | Confirm | ASan/UBSan and TSan CI jobs (ci-build.yml, ADR 0007), CodeQL |
| crypto_published | Met | Only SHA-256 via Qt (`src/plugins/src/pluginrepository.cpp`, `src/logdata/src/indexcache.cpp`) |
| crypto_call | Met | Qt `QCryptographicHash`, no hand-rolled crypto |
| crypto_floss | Met | Qt |
| crypto_keylength | N/A | No keys are generated or held by the application |
| crypto_working | Met | SHA-256 only for integrity; MD5 in `mergecontroller.cpp` is used for non-security bucketing of lines, not for security |
| crypto_weaknesses | Met | No SHA-1/MD5 for security purposes |
| crypto_pfs | N/A | The application implements no network protocol of its own; downloads use HTTPS via Qt |
| crypto_password_storage | N/A | The application stores no user passwords |
| crypto_random | N/A | No security-relevant random numbers generated |
| delivery_mitm | Met | Releases on https://github.com/64x-lunicorn/LogSquirl/releases, packages on https://packages.lunicorn-lab.de |
| delivery_unsigned | Met | SHA-256 checksum file signed with Sigstore, GitHub build provenance and SBOM attestations, signed APT repo (`ci-release.yml`, README.md "Verify") |
| vulnerabilities_fixed_60_days | Met once confirmed | `vuln-scan.yml`, Renovate/Dependabot, CHANGELOG `## Security`; check no open advisory older than 60 days |
| vulnerabilities_critical_fixed | Met | see above |
| no_leaked_credentials | Met | No secrets in repo; workflows use GitHub secrets; zizmor and Scorecard checks |
| static_analysis | Met | CodeQL `.github/workflows/codeql-analysis.yml`; clang-tidy config `.clang-tidy`, `cmake/StaticAnalyzers.cmake` |
| static_analysis_common_vulnerabilities | Met | CodeQL |
| static_analysis_fixed | Met | CodeQL alerts in the Security tab; check none is open and exploitable |
| static_analysis_often | Met | On every pull request |
| dynamic_analysis | Met | ASan/UBSan job, TSan builds, e2e tests (ci-build.yml) |
| dynamic_analysis_unsafe | Met | ASan/UBSan (memory-unsafe C++) |
| dynamic_analysis_enable_assertions | Met | Sanitizer job runs the test suites with assertions of the test build |
| dynamic_analysis_fixed | Met | Sanitizer job is red on any report (`halt_on_error`) |
