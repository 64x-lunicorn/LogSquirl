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

// The single-instance lock and local socket live in a directory only the
// user can reach, and a held lock with nothing listening does not make an
// instance hand over (#827).

#include <catch2/catch_test_macros.hpp>

#include "singleinstancedirectory.h"

#include <kdsingleapplication.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

// Sets an environment variable for a scope and puts back what was there.
class ScopedEnvironment {
public:
    ScopedEnvironment( const char* name, const QString& value )
        : name_( name )
        , wasSet_( qEnvironmentVariableIsSet( name ) )
        , before_( qgetenv( name ) )
    {
        qputenv( name, value.toLocal8Bit() );
    }
    ~ScopedEnvironment()
    {
        if ( wasSet_ ) {
            qputenv( name_, before_ );
        }
        else {
            qunsetenv( name_ );
        }
    }
    ScopedEnvironment( const ScopedEnvironment& ) = delete;
    ScopedEnvironment& operator=( const ScopedEnvironment& ) = delete;

private:
    const char* name_;
    bool wasSet_;
    QByteArray before_;
};

// A name of this process alone, so parallel test runs never meet.
QString guardName()
{
    return QStringLiteral( "lsq827-%1" ).arg( QCoreApplication::applicationPid() );
}

QString sameSpelling( const QString& path )
{
    return QFileInfo( path ).canonicalFilePath();
}

#ifndef Q_OS_WIN
QString ownDirectoryUnder( const QString& base )
{
    return QStringLiteral( "%1/logsquirl-%2" ).arg( base ).arg( ::getuid() );
}

// A directory of the user's own: theirs, mode 0700, no symbolic link.
bool isOwnerOnlyDirectory( const QString& path )
{
    struct stat status{};
    if ( ::lstat( QFile::encodeName( path ).constData(), &status ) != 0 ) {
        return false;
    }
    return S_ISDIR( status.st_mode ) && status.st_uid == ::getuid()
           && ( status.st_mode & ( S_IRWXU | S_IRWXG | S_IRWXO ) ) == S_IRWXU;
}

// What the socket file of a listening guard is: a socket the user alone
// reaches.
bool isOwnerOnlySocket( const QString& path )
{
    struct stat status{};
    if ( ::lstat( QFile::encodeName( path ).constData(), &status ) != 0 ) {
        return false;
    }
    return S_ISSOCK( status.st_mode ) && ( status.st_mode & ( S_IRWXG | S_IRWXO ) ) == 0;
}
#endif

// The lock file a guard of `name` in `directory` takes, learnt from one.
QString lockFileOf( const QString& name, const QString& directory )
{
    const KDSingleApplication guard( name, directory );
    return directory + '/' + QFileInfo( guard.socketName() ).fileName() + ".lock";
}

} // namespace

#ifndef Q_OS_WIN
TEST_CASE( "The single-instance directory is the user's own under the runtime directory, else "
           "under the temporary one",
           "[singleinstance]" )
{
    QTemporaryDir runtime;
    QTemporaryDir temporary;
    REQUIRE( runtime.isValid() );
    REQUIRE( temporary.isValid() );

    SECTION( "under the runtime directory when there is one" )
    {
        const auto directory = singleInstanceDirectory( runtime.path(), temporary.path() );
        REQUIRE( directory == ownDirectoryUnder( runtime.path() ) );
        REQUIRE( isOwnerOnlyDirectory( directory ) );
        REQUIRE_FALSE( QFileInfo::exists( ownDirectoryUnder( temporary.path() ) ) );
    }

    SECTION( "under the temporary directory when there is none" )
    {
        const auto directory = singleInstanceDirectory( {}, temporary.path() );
        REQUIRE( directory == ownDirectoryUnder( temporary.path() ) );
        REQUIRE( isOwnerOnlyDirectory( directory ) );
    }

    SECTION( "an existing directory of the user's is made owner-only again" )
    {
        const auto path = ownDirectoryUnder( temporary.path() );
        REQUIRE( QDir().mkdir( path ) );
        REQUIRE( QFile::setPermissions( path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                  | QFile::ReadGroup | QFile::ExeGroup
                                                  | QFile::ReadOther | QFile::ExeOther ) );
        REQUIRE_FALSE( isOwnerOnlyDirectory( path ) );

        REQUIRE( singleInstanceDirectory( {}, temporary.path() ) == path );
        REQUIRE( isOwnerOnlyDirectory( path ) );
    }

    SECTION( "a file in its place is not used, and the next directory is" )
    {
        QFile file( ownDirectoryUnder( runtime.path() ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.close();

        REQUIRE( singleInstanceDirectory( runtime.path(), temporary.path() )
                 == ownDirectoryUnder( temporary.path() ) );
        REQUIRE( singleInstanceDirectory( runtime.path(), {} ).isEmpty() );
    }

    SECTION( "a symbolic link in its place is not followed" )
    {
        REQUIRE( QFile::link( runtime.path(), ownDirectoryUnder( temporary.path() ) ) );

        REQUIRE( singleInstanceDirectory( {}, temporary.path() ).isEmpty() );
    }

    SECTION( "no directory at all gives none" )
    {
        REQUIRE( singleInstanceDirectory( {}, {} ).isEmpty() );
    }
}
#else
TEST_CASE( "The single-instance directory is the temporary directory on Windows",
           "[singleinstance]" )
{
    REQUIRE( singleInstanceDirectory( "C:/runtime", "C:/temporary" ) == "C:/temporary" );
}
#endif

TEST_CASE( "The single-instance directory follows the isolation of an E2E instance",
           "[singleinstance]" )
{
    // tests/e2e/isolated_instance.py moves TMPDIR on macOS and the XDG
    // directories, the runtime one among them, on Linux (#328).
    QTemporaryDir isolated;
    REQUIRE( isolated.isValid() );
#if defined( Q_OS_MACOS )
    const ScopedEnvironment moved( "TMPDIR", isolated.path() );
#elif defined( Q_OS_UNIX )
    const ScopedEnvironment moved( "XDG_RUNTIME_DIR", isolated.path() );
#else
    const ScopedEnvironment moved( "TMP", isolated.path() );
#endif

    const auto directory = singleInstanceDirectory();
    REQUIRE_FALSE( directory.isEmpty() );
#ifdef Q_OS_WIN
    REQUIRE( sameSpelling( directory ) == sameSpelling( isolated.path() ) );
#else
    REQUIRE( sameSpelling( QFileInfo( directory ).absolutePath() )
             == sameSpelling( isolated.path() ) );
    REQUIRE( isOwnerOnlyDirectory( directory ) );
#endif
}

TEST_CASE( "The lock file and the socket of the primary instance are in the directory given",
           "[singleinstance]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto name = guardName();

    const KDSingleApplication primary( name, directory.path() );
    REQUIRE( primary.isPrimaryInstance() );
    REQUIRE( primary.isListening() );
    REQUIRE( primary.primaryPid() == QCoreApplication::applicationPid() );

    const auto entries = QDir( directory.path() ).entryList( QDir::Files | QDir::System );
    REQUIRE( entries.contains( QFileInfo( primary.socketName() ).fileName() + ".lock" ) );
#ifndef Q_OS_WIN
    REQUIRE( primary.socketName().startsWith( directory.path() + '/' ) );
    REQUIRE( QFileInfo( primary.socketName() ).fileName().startsWith( name ) );
    REQUIRE( isOwnerOnlySocket( primary.socketName() ) );
#endif
}

TEST_CASE( "A second instance of the name in the directory hands its message to the first",
           "[singleinstance]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto name = guardName();

    KDSingleApplication primary( name, directory.path() );
    REQUIRE( primary.isListening() );
    QSignalSpy received( &primary, &KDSingleApplication::messageReceived );

    KDSingleApplication secondary( name, directory.path() );
    REQUIRE_FALSE( secondary.isPrimaryInstance() );
    REQUIRE( secondary.primaryPid() == QCoreApplication::applicationPid() );

    REQUIRE( secondary.sendMessageWithResult( "hello", 2000 )
             == KDSingleApplication::SendResult::Sent );
    REQUIRE( received.wait( 5000 ) );
    REQUIRE( received.first().first().toByteArray() == QByteArray( "hello" ) );
}

#ifndef Q_OS_WIN
TEST_CASE( "A lock file or socket under the old name in the temporary directory does not "
           "prevent a start",
           "[singleinstance]" )
{
    // Before #827 the lock file was kdsingleapp-<uid>-<session>-<name>.lock
    // in the temporary directory and the socket next to it; on a shared /tmp
    // either could be anyone's.
    const auto name = guardName();
    const auto oldSocket = QStringLiteral( "%1/kdsingleapp-%2-%3-%4" )
                               .arg( QDir::tempPath() )
                               .arg( ::getuid() )
                               .arg( qEnvironmentVariable( "XDG_SESSION_ID" ), name );
    QLockFile oldLock( oldSocket + ".lock" );
    REQUIRE( oldLock.tryLock( 0 ) );
    QFile squatter( oldSocket );
    REQUIRE( squatter.open( QIODevice::WriteOnly ) );
    squatter.close();

    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const KDSingleApplication guard( name, directory.path() );
    REQUIRE( guard.isPrimaryInstance() );
    REQUIRE( guard.isListening() );
    REQUIRE( guard.socketName() != oldSocket );

    oldLock.unlock();
    QFile::remove( oldSocket );
}
#endif

TEST_CASE( "A held lock with nothing listening is told from a primary instance that is busy",
           "[singleinstance]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto name = guardName();
    const auto lockFile = lockFileOf( name, directory.path() );

    // Someone holds the lock: this process, which the lock never finds stale.
    QLockFile held( lockFile );
    REQUIRE( held.tryLock( 0 ) );

    KDSingleApplication secondary( name, directory.path() );
    REQUIRE_FALSE( secondary.isPrimaryInstance() );

    SECTION( "nothing listens: the message goes nowhere, and the caller knows" )
    {
        REQUIRE( secondary.sendMessageWithResult( "hello", 300 )
                 == KDSingleApplication::SendResult::NotConnected );
    }

    SECTION( "a listener that does not read gets the message: a busy primary instance "
             "looks like that, and so does another process of the user" )
    {
        // What the check does not cover: the socket's directory keeps other
        // users out, nothing tells this user's own processes apart.
        QLocalServer listener;
        listener.setSocketOptions( QLocalServer::UserAccessOption );
        REQUIRE( listener.listen( secondary.socketName() ) );

        REQUIRE( secondary.sendMessageWithResult( "hello", 2000 )
                 == KDSingleApplication::SendResult::Sent );
    }
}

TEST_CASE( "An instance without a directory of its own runs unguarded", "[singleinstance]" )
{
    const KDSingleApplication unguarded( guardName(), QString{} );
    REQUIRE( unguarded.isPrimaryInstance() );
    REQUIRE_FALSE( unguarded.isListening() );
    REQUIRE( unguarded.primaryPid() == QCoreApplication::applicationPid() );
}
