# LogSquirl — Copilot Instructions

## Project Overview

LogSquirl is a cross-platform log viewer built with C++17 and Qt (supports both Qt5 and Qt6).
It is a GPL-3.0-or-later licensed fork of [klogg](https://github.com/variar/klogg), which itself
is a fork of [glogg](https://github.com/nickbnf/glogg). The build system is CMake (minimum 3.12).
Dependencies are managed via [CPM](https://github.com/cpm-cmake/CPM.cmake).

- **Repository**: <https://github.com/64x-lunicorn/LogSquirl>
- **License**: GPL-3.0-or-later (see `COPYING`)
- **Bundle identifier**: `io.github.logsquirl`

## Language

**All code, comments, documentation, commit messages, issues, and pull requests must be written
in English.** No exceptions.

## Documentation

**Every change must be documented.** This includes:

- New or modified public functions/methods **must** have a short comment above the declaration
  explaining purpose, parameters, and return value.
- Non-trivial logic **must** have inline comments explaining *why*, not *what*.
- New files **must** include the GPL-3.0 license header (see "File Headers" below).
- User-facing changes **must** be noted in `CHANGELOG.md`.
- Build or dependency changes **must** be reflected in `BUILD.md`.
- Architecture or structural changes **must** be described in the commit message body.

## C++ Code Style

The project uses `.clang-format` and `.clang-tidy` at the repository root. Always format code
with clang-format before committing.

### Naming Conventions (enforced by `.clang-tidy`)

| Element              | Convention        | Example                              |
|----------------------|-------------------|--------------------------------------|
| Namespace            | `lower_case`      | `logsquirl::`                        |
| Class / Struct       | `CamelCase`       | `CrawlerWidget`, `LogData`           |
| Function / Method    | `camelBack`       | `getTopLine()`, `displayQuickFind()` |
| Variable / Parameter | `camelBack`       | `lineNumber`, `fileSize`             |
| Private member       | `camelBack_`      | `ignoreCase_`, `autoRefresh_`        |
| Global constant      | `CamelCase`       | `MaxRecentFiles`, `ErrorPalette`     |
| Macro / Define       | `UPPER_SNAKE_CASE`| `LOGSQUIRL_USE_LTO`                  |
| Enum value           | `CamelCase`       | `SearchRegexpType`                   |

### Formatting Highlights (from `.clang-format`)

- **Standard**: C++17
- **Column limit**: 100
- **Indent**: 4 spaces (no tabs)
- **Braces**: Custom — opening brace on next line for functions; `else` on new line
- **Pointer/Reference alignment**: Left (`int* ptr`, `const QString& str`)
- **Spaces in parentheses**: Yes — `if ( condition )`, `foo( arg )`
- **Spaces in square brackets**: Yes — `arr[ i ]`
- **Constructor initializers**: Break before comma
- **Template declarations**: Always break after template

### Header Guards

- Prefer `#pragma once` for new files.
- Existing `#ifndef` guards follow the pattern `LOGSQUIRL_FILENAME_H`.

### Include Order

1. Corresponding header for `.cpp` files
2. Project headers (`#include "log.h"`)
3. Qt headers (`#include <QApplication>`)
4. Standard library headers (`#include <algorithm>`)
5. Third-party headers

### Modern C++ Usage

- Use `auto` where the type is obvious from context.
- Use `const` and `constexpr` liberally — mark everything `const` that can be.
- Use range-based for loops: `for ( const auto& item : collection )`.
- Use `if constexpr` for compile-time branching.
- Use strong typedefs (`type_safe::strong_typedef`) for domain types like line numbers and offsets.
- Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw owning pointers.

## Qt Patterns

- **Signal/Slot connections**: Use new-style `connect()` with function pointers:
  ```cpp
  connect( sender, &Sender::signal, receiver, &Receiver::slot );
  ```
- **Cross-thread connections**: Explicitly specify `Qt::QueuedConnection`.
- **Meta-object macros**: Use `Q_OBJECT`, `Q_SIGNALS`, `Q_SLOTS` (not `signals:` / `slots:`).
- **Meta-type registration**: Register custom types used across threads with `qRegisterMetaType`.
- **Qt version compatibility**: The codebase supports both Qt5 and Qt6. Use conditional compilation
  (`QT_VERSION_MAJOR`) when APIs differ between versions.
- **UI files**: Use Qt Designer `.ui` files for dialog layouts. Build system has `AUTOUIC ON`.
- **Resources**: Use `.qrc` files with `AUTORCC ON`.

## CMake Conventions

### Variable and Option Naming

- Project options: `LOGSQUIRL_<FEATURE>` (e.g., `LOGSQUIRL_BUILD_TESTS`, `LOGSQUIRL_USE_LTO`)
- Internal variables: `UPPER_SNAKE_CASE`

### Target Naming

- Libraries: `logsquirl_<module>` (e.g., `logsquirl_ui`, `logsquirl_logdata`, `logsquirl_utils`)
- Executables: `logsquirl`, `logsquirl_portable`, `logsquirl_grep`
- Test targets: `logsquirl_tests`, `logsquirl_itests`

### Module Structure

Each module in `src/` follows this layout:

```
src/<module>/
├── CMakeLists.txt
├── include/       # Public headers (added via target_include_directories PUBLIC)
└── src/           # Implementation files
```

### Dependencies

- Use CPM (`CPMAddPackage`) for third-party dependencies.
- Use `find_package()` for Qt and system libraries.
- Custom Find modules live in `cmake/`.

## Project Structure

```
src/
├── app/                  # Application entry points (main, CLI, portable)
├── crash_handler/        # Crash handling and issue reporting
├── filewatch/            # File system watching
├── logdata/              # Core log data model
├── logging/              # Logging infrastructure
├── logsquirl_version/    # Version info generation
├── regex/                # Regular expression engine abstraction
├── settings/             # Configuration and persistence
├── ui/                   # Qt UI components (MainWindow, dialogs, views)
├── utils/                # Shared utilities and containers
└── versioncheck/         # Update checking
tests/
├── unit/                 # Unit tests (Catch2)
├── ui/                   # UI integration tests
└── helpers/              # Test utilities
```

## Testing

- **Framework**: [Catch2](https://github.com/catchorg/Catch2)
- **Test style**: Use `SCENARIO` / `GIVEN` / `WHEN` / `THEN` (BDD) for new tests.
- **Tags**: Categorize tests with descriptive tags: `[patternmatcher]`, `[linetypes]`.
- **Test file naming**: `<module>_test.cpp`
- **Assertions**: Use `REQUIRE()` and `REQUIRE_FALSE()` — avoid `CHECK()` unless intentional.
- **Build**: Tests are built when `LOGSQUIRL_BUILD_TESTS=ON` (default).
- **Test data**: Static test fixtures go in `test_data/` at the repository root.

## File Headers

Every source file must include the GPL-3.0 license header. New files should use:

```cpp
/*
 * Copyright (C) <year> LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */
```

Do **not** remove existing copyright headers from forked files. Add a new block above or below.

## Git and Contribution Workflow

- **Branch**: `master` is the main branch.
- **Commit messages**: Use imperative mood, concise subject line (≤72 chars). Add a body for
  non-trivial changes explaining *what* and *why*.
- **Skip CI**: Add `[skip ci]` to commit message to bypass CI builds.
- **Cross-platform**: Every change must work on Windows, macOS, and Linux.
- **Pull requests**: Keep PRs small and focused — one feature or fix per PR.
- **CI**: GitHub Actions runs builds on Linux (Ubuntu, Oracle Linux), macOS, and Windows.

## Build Commands

```bash
# Configure (Release, out-of-source)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run tests
cd build && ctest --output-on-failure
```

See `BUILD.md` for full dependency and platform-specific instructions.

## Platform Notes

- **Windows**: NSIS installer, Chocolatey, Scoop packages. Uses MSVC.
- **macOS**: `.pkg` installer via CPack. Supports `LOGSQUIRL_OSX_DEPLOYMENT_TARGET`.
- **Linux**: DEB, RPM, AppImage packages. CI uses Docker containers for reproducible builds.

## Attribution

LogSquirl is a fork of klogg by Anton Filimonov, which is a fork of glogg by Nicolas Bonnefon.
The full attribution chain is documented in the `NOTICE` file. Preserve existing copyright
headers and attribution when modifying files.
