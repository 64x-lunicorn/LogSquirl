/*
 * Copyright (C) 2026 LogSquirl Contributors
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

#pragma once

#include <QString>

// Where the single-instance lock file and local socket live (#827): a
// directory `logsquirl-<uid>` that only the user can reach, created with
// owner-only permissions and kept that way, under
//
// - the runtime directory ($XDG_RUNTIME_DIR, QStandardPaths::RuntimeLocation)
//   on Linux and the other Unixes, falling back to the temporary directory
//   when Qt reports none it can vouch for;
// - the temporary directory (QDir::tempPath()) on macOS, which is the user's
//   own there, and which an isolated E2E instance redirects (#328).
//
// On Windows, the temporary directory itself: it is the user's own, and the
// named pipe is not in a directory at all.
//
// Empty when no such directory can be had: it exists but belongs to someone
// else, or cannot be created. The instance then runs without a single-instance
// guard, rather than with a lock anyone can hold.
QString singleInstanceDirectory();

// The same, under the directories given: `runtimeDirectory` first, empty for
// none, then `temporaryDirectory`.
QString singleInstanceDirectory( const QString& runtimeDirectory,
                                 const QString& temporaryDirectory );
