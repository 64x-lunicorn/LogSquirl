---
title: Version 22.06
description: Hyperscan regex engine, reworked highlighters, configurable shortcuts, predefined filters.
---

## Version 22.06

This release brings many new features, performance and usability improvements.

### New features

- Regular expression engine is switched to Hyperscan. This is several times faster than Qt regular expressions. Hyperscan pattern syntax is less expressive, and LogSquirl will automatically switch to Qt engine if needed. Moreover, LogSquirl now allows combining regular expressions with boolean operators (AND, OR, NOT). Hyperscan requires CPU with SSSE3 support. If CPU doesn't have these instructions, then LogSquirl will switch to Qt regular expression engine.
- Highlighters configuration has been reworked. Now it is possible to:
  - create several sets of highlighters and apply any number of them to current file
  - assign some text a color label (up to 9 different colors)
  - add variation to slightly change highlight color for a pattern based on actual matching text
  - use alpha-channel for highlight colors
- Keyboard shortcuts are now configurable via a special configuration dialog
- There is a predefined filters dialog. It allows creating sets of filters and quickly add them to search expression
- The limit of number of lines in the log file has been removed. It is now limited by available RAM and CPU resources. The single line length is still limited to around 2^30, this is limitation of QString in Qt5.
- Context menu for selected text now has more useful actions (replace search, add to scratchpad, etc.)
- Search history is now editable and includes more items

### Bug fixes

- Crash reports from many users helped to fix issues with putting too much data to clipboard, data races with multiple instances of LogSquirl and multiple windows.
- A fast file modification detection mode helps to improve performance in case when a large file is opened in follow file mode
- Dark mode is simplified and looks more solid
- LogSquirl no longer hangs in cases when large amount of data is rapidly added to a file
- Files with UTF-16 little and big endian are now displayed properly
- LogSquirl no longer leaks memory if file in follow mode is left open for days

Internal code structure has been improved to improve memory allocation and multithreading performance. LogSquirl uses less external dependencies. Build system now allows using more external dependencies from host build system.

### System requirements

LogSquirl requires CPU with at least SSE2 instruction set. For best performance CPU should have SSSE3 and POPCNT instructions.
Supported operating systems: Windows 7+, macOS 10.14+, Ubuntu LTS 18.04+, Oracle Linux 8+

### Precompiled binaries

- Windows x86 and x64, based on Qt 5.15.2, both installer and portable versions, now signed with valid codesign certificate
- Linux x64 DEB packages for Ubuntu 18.04, 20.04 and 22.04 as well as RPM package built on Oracle Linux 8
- Linux x64 AppImage package
- Mac x64 dmg image and pkg installer, based on Qt 5.15.2, for Intel CPUs
- **Experimental** packages for Windows built with Qt 6.2.4, requires Windows 10+.
- **Experimental** packages for Mac built with Qt 6.2.4, requires macOS 10.15+, for Intel CPUs

Download on GitHub: [LogSquirl 22.06](https://github.com/64x-lunicorn/LogSquirl/releases/tag/v22.06)
