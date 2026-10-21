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

#include "singleinstancedirectory.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#ifndef Q_OS_WIN
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

#ifndef Q_OS_WIN
// The directory `logsquirl-<uid>` under `base`, made or kept the user's own:
// a directory, not a symbolic link, owned by the user, mode 0700. Empty when
// it cannot be (#827).
QString ownerOnlyDirectory( const QString& base )
{
    if ( base.isEmpty() ) {
        return {};
    }
    const auto uid = ::getuid();
    auto path = QStringLiteral( "%1/logsquirl-%2" ).arg( base ).arg( uid );
    const auto encoded = QFile::encodeName( path );

    // Made with its final mode: there is no moment it is open to others.
    if ( ::mkdir( encoded.constData(), S_IRWXU ) != 0 && errno != EEXIST ) {
        return {};
    }

    // lstat, not stat: a symbolic link someone else planted under a shared
    // base leads elsewhere, and is not followed.
    struct stat status{};
    if ( ::lstat( encoded.constData(), &status ) != 0 || !S_ISDIR( status.st_mode )
         || status.st_uid != uid ) {
        return {};
    }
    if ( ( status.st_mode & ( S_IRWXU | S_IRWXG | S_IRWXO ) ) != S_IRWXU
         && ::chmod( encoded.constData(), S_IRWXU ) != 0 ) {
        return {};
    }
    return path;
}
#endif

} // namespace

QString singleInstanceDirectory( const QString& runtimeDirectory,
                                 const QString& temporaryDirectory )
{
#ifdef Q_OS_WIN
    Q_UNUSED( runtimeDirectory );
    return temporaryDirectory;
#else
    auto underRuntime = ownerOnlyDirectory( runtimeDirectory );
    if ( !underRuntime.isEmpty() ) {
        return underRuntime;
    }
    return ownerOnlyDirectory( temporaryDirectory );
#endif
}

QString singleInstanceDirectory()
{
#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
    // macOS: QStandardPaths::RuntimeLocation is ~/Library/Application Support
    // there, a place for data, not sockets, and TMPDIR is the user's own.
    return singleInstanceDirectory( {}, QDir::tempPath() );
#else
    // Qt vouches for the runtime directory: owned by the user, mode 0700, or
    // it reports none.
    return singleInstanceDirectory(
        QStandardPaths::writableLocation( QStandardPaths::RuntimeLocation ), QDir::tempPath() );
#endif
}
