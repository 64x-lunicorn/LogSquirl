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
#include <QStringList>

// Where the single-instance lock file and local socket of a guard live
// (#827, #841).
struct SingleInstanceLocation {
    // A directory that only the user can reach, created with owner-only
    // permissions and kept that way: `logsquirl-<uid>` under one of the base
    // directories on Unix; the temporary directory itself on Windows, where
    // it is the user's own and the named pipe is not in a directory at all.
    //
    // Empty when no such directory can be had: each exists but belongs to
    // someone else, or cannot be created. The instance then runs without a
    // single-instance guard, rather than with a lock anyone can hold.
    QString directory;

    // The file name of the socket inside it, `<name>[-<session id>]` on Unix:
    // one primary instance per login session. The lock file is this name
    // with `.lock` appended, in the same directory. On Windows the name as
    // given: the pipe carries a session id of its own.
    QString socketName;

    // Whether the lock and socket are not where they would be by choice: the
    // socket's path did not fit the platform's limit there, or the directory
    // was not the user's to have (#841). For the log.
    bool fallback = false;
};

// The size of a local socket's path on this platform, the terminating NUL
// included: sun_path, 104 bytes on macOS and 108 on Linux (#841). Zero on
// Windows, where a pipe's name has no such limit.
int localSocketPathLimit();

// Whether a local socket at `<directory>/<socketName>` fits `limit` bytes:
// its path in the local 8-bit encoding plus the terminating NUL, as
// QLocalServer::listen measures it (#841).
bool localSocketPathFits( const QString& directory, const QString& socketName, int limit );

// The location of the guard named `name` under this platform's directories:
//
// - the runtime directory ($XDG_RUNTIME_DIR, QStandardPaths::RuntimeLocation)
//   on Linux and the other Unixes, when Qt vouches for one, then the
//   temporary directory (QDir::tempPath()), then `/tmp`;
// - the temporary directory on macOS, which is the user's own there, and
//   which an isolated E2E instance redirects (#328), then `/tmp`;
// - the temporary directory on Windows.
//
// With the session id from XDG_SESSION_ID on Unix.
SingleInstanceLocation singleInstanceLocation( const QString& name );

// The same under `baseDirectories`, tried in order, with `sessionId` (empty
// for none) and the socket path limit given (#841): the first directory the
// user can have whose socket path fits is taken. When none fits with the
// session id in the socket's name, the session id goes and the directories
// are tried again; a socket that fits nowhere is placed in the last directory
// the user can have, and listening fails there as it would anywhere. On
// Windows the first base directory, as it is, with `name` as it is.
SingleInstanceLocation singleInstanceLocation( const QString& name, const QString& sessionId,
                                               const QStringList& baseDirectories, int limit );
