---
title: Version 26.07.0
description: Windows dark mode icon fixes, AppImage runs on Ubuntu 22.04, correct release version numbering.
release:
  version: 26.07.0
  date: 2026-07-13
  channel: stable
---

## Version 26.07.0 (July 2026)

A maintenance release: Windows dark mode controls render correctly again, the AppImage runs on older Linux distributions, and release binaries and update notifications carry the right version.

### Bug fixes

- **Windows dark mode: check box and combo box icons**: The check mark in check boxes and the drop-down arrow in combo boxes were invisible or drawn incorrectly under Windows dark mode. Indicators are now larger and render correctly at every DPI scale.
- **Spin box up arrow**: The up arrow of spin boxes showed the down-arrow icon.
- **AppImage runs on Ubuntu 22.04**: The AppImage needed glibc 2.39 and failed to start on Ubuntu 22.04 and other older distributions. It is now built on Ubuntu 22.04 and runs there and on every newer distribution.
- **Release binaries carry the correct version**: Every release since 26.03 attached binaries reporting version `26.03.0.<build>`. Binaries now report the version of their release.
- **Update notifications after a stable release**: Stable users were not notified of new stable versions; they are now.

**Download**: [GitHub Release v26.07.0](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.07.0)
