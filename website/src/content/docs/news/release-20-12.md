---
title: Version 20.12
description: Reworked highlighters, dark theme support, crash reporting via Sentry, signed macOS DMG.
---

## Version 20.12

This release has several major new features.

First of all highlighters have been reworked. Now it is possible to create several sets of highlighting rules and choose active set to apply at the moment. Highlight rules have become more flexible. It is possible to colorize only matching part of line (with support for regex capture groups if present). And finally, highlighters configuration can be exported to a file and shared with collaborators.

Another big feature is support for different window styles. LogSquirl can be configured to use one of the default styles provided by Qt or use a separate dark theme based on [QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet). If default styles are used then LogSquirl will adjust its palette to current OS theme.

This release also introduces new error reporting features to help with bugfix. Using service and SDK provided by [Sentry](https://sentry.io) LogSquirl will now collect minidumps when something crashes and send them to developers after asking user to review crash report. More about this is covered in [Automatic crash reporting](/articles/crash-reporting).

DMG packages for Mac are now properly signed to make Gatekeeper happy.

### Bug fixes and minor improvements

- Fixed RPM and DEB packaging
- Fixed bug with case-insensitive search autocomplete
- Fixed excessive reloading when follow mode is enabled
- Made some shortcut behavior more user-friendly
- Use DejaVu fonts by default
- Updated 3rdparty libraries

Download on GitHub: [LogSquirl 20.12](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v20.12)
