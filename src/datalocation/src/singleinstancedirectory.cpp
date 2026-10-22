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
#include <sys/un.h>
#include <unistd.h>
#endif

#include <utility>

namespace {

#ifndef Q_OS_WIN
// The path of the directory `logsquirl-<uid>` under `base`.
QString ownDirectoryUnder( const QString& base )
{
    return QStringLiteral( "%1/logsquirl-%2" ).arg( base ).arg( ::getuid() );
}

// The directory `logsquirl-<uid>` under `base`, made or kept the user's own:
// a directory, not a symbolic link, owned by the user, mode 0700. Empty when
// it cannot be (#827).
QString ownerOnlyDirectory( const QString& base )
{
    if ( base.isEmpty() ) {
        return {};
    }
    const auto uid = ::getuid();
    auto path = ownDirectoryUnder( base );
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

// The base directories worth trying, in order: the empty ones, which Qt
// reports for a runtime directory it cannot vouch for, and repeats, as when
// the temporary directory is /tmp, drop out.
QStringList candidateBases( const QStringList& baseDirectories )
{
    QStringList bases;
    for ( const auto& base : baseDirectories ) {
        if ( !base.isEmpty() && !bases.contains( base ) ) {
            bases.append( base );
        }
    }
    return bases;
}
#endif

} // namespace

int localSocketPathLimit()
{
#ifdef Q_OS_WIN
    return 0;
#else
    return static_cast<int>( sizeof( sockaddr_un::sun_path ) );
#endif
}

bool localSocketPathFits( const QString& directory, const QString& socketName, int limit )
{
    const auto path = QFile::encodeName( directory + QLatin1Char( '/' ) + socketName );
    return path.size() + 1 <= limit;
}

SingleInstanceLocation singleInstanceLocation( const QString& name, const QString& sessionId,
                                               const QStringList& baseDirectories, int limit )
{
#ifdef Q_OS_WIN
    Q_UNUSED( sessionId );
    Q_UNUSED( limit );
    return { baseDirectories.value( 0 ), name, false };
#else
    const auto bases = candidateBases( baseDirectories );

    // The socket's name with the session id, then without it (#841): a
    // shorter directory comes before a shorter name, so one primary instance
    // per login session holds as long as any directory fits.
    QStringList socketNames{ name };
    if ( !sessionId.isEmpty() ) {
        socketNames.prepend( name + QLatin1Char( '-' ) + sessionId );
    }

    bool firstChoice = true;
    for ( const auto& socketName : socketNames ) {
        for ( const auto& base : bases ) {
            // The fit is known before anything is created.
            if ( localSocketPathFits( ownDirectoryUnder( base ), socketName, limit ) ) {
                auto directory = ownerOnlyDirectory( base );
                if ( !directory.isEmpty() ) {
                    return { std::move( directory ), socketName, !firstChoice };
                }
            }
            firstChoice = false;
        }
    }

    // Nowhere does it fit: the last directory the user can have, with the
    // shortest name. Listening fails there as it would anywhere, and the
    // library says so; the lock still makes one instance primary.
    for ( auto base = bases.crbegin(); base != bases.crend(); ++base ) {
        auto directory = ownerOnlyDirectory( *base );
        if ( !directory.isEmpty() ) {
            return { std::move( directory ), socketNames.constLast(), true };
        }
    }
    return { {}, socketNames.constLast(), true };
#endif
}

SingleInstanceLocation singleInstanceLocation( const QString& name )
{
#if defined( Q_OS_WIN )
    return singleInstanceLocation( name, {}, { QDir::tempPath() }, localSocketPathLimit() );
#elif defined( Q_OS_MACOS )
    // macOS: QStandardPaths::RuntimeLocation is ~/Library/Application Support
    // there, a place for data, not sockets, and TMPDIR is the user's own;
    // /tmp when TMPDIR is too deep for a socket's path (#841).
    return singleInstanceLocation( name, qEnvironmentVariable( "XDG_SESSION_ID" ),
                                   { QDir::tempPath(), QStringLiteral( "/tmp" ) },
                                   localSocketPathLimit() );
#else
    // Qt vouches for the runtime directory: owned by the user, mode 0700, or
    // it reports none.
    return singleInstanceLocation(
        name, qEnvironmentVariable( "XDG_SESSION_ID" ),
        { QStandardPaths::writableLocation( QStandardPaths::RuntimeLocation ), QDir::tempPath(),
          QStringLiteral( "/tmp" ) },
        localSocketPathLimit() );
#endif
}
