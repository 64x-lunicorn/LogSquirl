---
title: "Version 26.03 (Beta)"
date: 2025-06-01T00:00:00+00:00
anchor: "v26_03"
weight: 26
---

## Version 26.03 (Beta)

This is the first release of **LogSquirl**, a GPL-3.0 fork of [klogg](https://github.com/variar/klogg). Since klogg is no longer actively maintained, LogSquirl continues development under a new name, building on the excellent foundation laid by both glogg and klogg.

### New features
 - **Rebranded** from klogg to LogSquirl with new bundle identifier `io.github.logsquirl`
 - **JWT token decoder** in Scratchpad: decodes Base64URL header and payload, formats JSON with indentation, and annotates epoch timestamps (`iat`, `exp`, `nbf`, `auth_time`) with human-readable UTC dates
 - **Filters Panel**: right sidebar dock with tabbed Filters and Scratchpad panels, toolbar filter icon, auto-search on toggle, and pinned filters that persist across sessions
 - **Chipmunk filter import**: import filters and highlighters from Chipmunk JSON export files via the Tools menu
 - **Beta update channel**: opt-in "Check for beta updates" checkbox in Settings > General. When enabled, the app checks for beta versions on every startup (bypassing the 7-day interval) and shows notifications with "(Beta)" label

### Bug fixes
 - Fixed crash on shutdown with Qt 6.10 on Windows (QThreadPool::waitForDone SEGFAULT)
 - Fixed BOM not written when saving search results to file for UTF-16 encoded logs
 - Upgraded OpenSSL from 1.1.x to 3.x (CVE-2022-1292)

### Build system
 - Upgraded to Qt 6.10.3 as primary build target (Qt 5 still supported)
 - Added CPM dependency management
 - Added Fedora 43 and Oracle Linux 10 build targets
 - Upgraded robin_hood to 3.11.5 with GCC 14 compatibility patch
 - Replaced unreliable AppleScript DMG layout with pre-built DS_Store file
 - Fixed macOS Gatekeeper rejection by signing each nested component individually with hardened-runtime entitlements

### CI/CD modernization
 - Pre-built Docker images on GHCR for reproducible builds
 - Tag-based releases (stable + beta), single release per tag with all platforms bundled
 - Upgraded CI runners: macOS 15 (ARM + Intel), Windows 2025, Ubuntu 24.04
 - Non-blocking Sentry/CodeQL jobs, reusable build workflow via `workflow_call`
 - Auto-changelog generation via `scripts/gen_changelog.py`

### System requirements
LogSquirl requires a CPU with at least SSE2 instruction set. For best performance, the CPU should have SSSE3 and POPCNT instructions.

Supported operating systems: Windows 10+, macOS 12+, Ubuntu 22.04+, Fedora 43+, Oracle Linux 10+
