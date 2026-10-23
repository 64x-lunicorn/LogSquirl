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
// instance hand over (#827). The socket's path fits the platform's limit,
// from a shorter directory when the first choice is too deep (#841).

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
#include <QStringList>
#include <QTemporaryDir>

#include <chrono>
#include <future>

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

// Sends from a thread of its own while this one runs the event loop, as a
// secondary instance in a process of its own would. On Windows a write to the
// pipe completes only once the listener reads it: Qt creates the pipe without
// a buffer, and the listener's socket reads in this thread's event loop. A
// send from this thread would wait for itself until its timeout and report
// NotSent (#849).
KDSingleApplication::SendResult sendFromAnotherThread( KDSingleApplication& secondary,
                                                       const QByteArray& message, int timeout )
{
    auto result = std::async(
        std::launch::async, [ & ] { return secondary.sendMessageWithResult( message, timeout ); } );
    while ( result.wait_for( std::chrono::milliseconds( 10 ) ) != std::future_status::ready ) {
        QCoreApplication::processEvents();
    }
    return result.get();
}

// The lock file a guard of `name` in `directory` takes, learnt from one.
QString lockFileOf( const QString& name, const QString& directory )
{
    QString socketName;
    {
        const KDSingleApplication guard( name, directory );
        socketName = guard.socketName();
    }
    // The guard took the directory with it, being the last in it (#827).
    QDir().mkpath( directory );
    return directory + '/' + QFileInfo( socketName ).fileName() + ".lock";
}

// A limit no path of these tests reaches: what the directory tests of #827
// are not about.
constexpr int NoLimit = 1 << 20;

// The bytes QLocalServer::listen counts for a socket at `path`: its local
// 8-bit encoding and the terminating NUL (#841).
int socketPathBytes( const QString& path )
{
    return static_cast<int>( QFile::encodeName( path ).size() ) + 1;
}

// A directory under `base` whose path has at least `bytes` bytes, owner-only,
// like the temporary directory of a deep scratch folder (#841).
QString deepDirectoryUnder( const QString& base, int bytes )
{
    auto path = base;
    while ( socketPathBytes( path ) < bytes ) {
        path += QStringLiteral( "/%1" ).arg( QString( 32, 'd' ) );
        REQUIRE( QDir().mkpath( path ) );
        REQUIRE(
            QFile::setPermissions( path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner ) );
    }
    return path;
}

} // namespace

TEST_CASE( "A local socket's path fits the limit with its NUL, in bytes", "[singleinstance]" )
{
    // What a socket path's bytes are is the encoded path and its NUL: at the
    // limit it fits, one byte over it does not, whatever the characters.
    const QString directory = "/d";
    constexpr int Limit = 104;
    const auto nameOf
        = [ & ]( int bytes ) { return QString( bytes - socketPathBytes( directory + '/' ), 'n' ); };

    SECTION( "exactly at the limit" )
    {
        const auto name = nameOf( Limit );
        REQUIRE( socketPathBytes( directory + '/' + name ) == Limit );
        REQUIRE( localSocketPathFits( directory, name, Limit ) );
    }

    SECTION( "one byte over" )
    {
        const auto name = nameOf( Limit + 1 );
        REQUIRE( socketPathBytes( directory + '/' + name ) == Limit + 1 );
        REQUIRE_FALSE( localSocketPathFits( directory, name, Limit ) );
    }

#ifndef Q_OS_WIN
    // The local 8-bit encoding is UTF-8 on Unix; on Windows it is the ANSI
    // code page, where the umlaut is one byte, and no pipe asks.
    SECTION( "a character of more than one byte counts its bytes, not one" )
    {
        // Two bytes in UTF-8; three on macOS, where QFile::encodeName
        // decomposes it. The bytes count, as many as the encoding makes.
        const QChar umlautU( 0x00FC );
        const auto extraBytes
            = static_cast<int>( QFile::encodeName( QString( umlautU ) ).size() ) - 1;
        REQUIRE( extraBytes >= 1 );

        auto name = nameOf( Limit );
        REQUIRE( localSocketPathFits( directory, name, Limit ) );
        name[ 0 ] = umlautU;
        REQUIRE( name.size() == nameOf( Limit ).size() );
        REQUIRE( socketPathBytes( directory + '/' + name ) == Limit + extraBytes );
        REQUIRE_FALSE( localSocketPathFits( directory, name, Limit ) );
        REQUIRE_FALSE( localSocketPathFits( directory, name, Limit + extraBytes - 1 ) );
        REQUIRE( localSocketPathFits( directory, name, Limit + extraBytes ) );
    }
#endif

    SECTION( "the platform's limit is sun_path on Unix" )
    {
#if defined( Q_OS_MACOS )
        REQUIRE( localSocketPathLimit() == 104 );
#elif defined( Q_OS_UNIX )
        REQUIRE( localSocketPathLimit() == 108 );
#else
        REQUIRE( localSocketPathLimit() == 0 );
#endif
    }
}

#ifndef Q_OS_WIN
TEST_CASE( "The single-instance directory is the user's own under the runtime directory, else "
           "under the temporary one",
           "[singleinstance]" )
{
    QTemporaryDir runtime;
    QTemporaryDir temporary;
    REQUIRE( runtime.isValid() );
    REQUIRE( temporary.isValid() );
    const auto name = guardName();

    SECTION( "under the runtime directory when there is one" )
    {
        const auto location
            = singleInstanceLocation( name, {}, { runtime.path(), temporary.path() }, NoLimit );
        REQUIRE( location.directory == ownDirectoryUnder( runtime.path() ) );
        REQUIRE( location.socketName == name );
        REQUIRE_FALSE( location.fallback );
        REQUIRE( isOwnerOnlyDirectory( location.directory ) );
        REQUIRE_FALSE( QFileInfo::exists( ownDirectoryUnder( temporary.path() ) ) );
    }

    SECTION( "under the temporary directory when there is none" )
    {
        const auto location = singleInstanceLocation( name, {}, { {}, temporary.path() }, NoLimit );
        REQUIRE( location.directory == ownDirectoryUnder( temporary.path() ) );
        REQUIRE_FALSE( location.fallback );
        REQUIRE( isOwnerOnlyDirectory( location.directory ) );
    }

    SECTION( "the session id scopes the socket's name, not the directory" )
    {
        const auto location
            = singleInstanceLocation( name, "7", { runtime.path(), temporary.path() }, NoLimit );
        REQUIRE( location.directory == ownDirectoryUnder( runtime.path() ) );
        REQUIRE( location.socketName == name + "-7" );
    }

    SECTION( "an existing directory of the user's is made owner-only again" )
    {
        const auto path = ownDirectoryUnder( temporary.path() );
        REQUIRE( QDir().mkdir( path ) );
        REQUIRE( QFile::setPermissions( path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                  | QFile::ReadGroup | QFile::ExeGroup
                                                  | QFile::ReadOther | QFile::ExeOther ) );
        REQUIRE_FALSE( isOwnerOnlyDirectory( path ) );

        REQUIRE( singleInstanceLocation( name, {}, { temporary.path() }, NoLimit ).directory
                 == path );
        REQUIRE( isOwnerOnlyDirectory( path ) );
    }

    SECTION( "a file in its place is not used, and the next directory is" )
    {
        QFile file( ownDirectoryUnder( runtime.path() ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.close();

        const auto location
            = singleInstanceLocation( name, {}, { runtime.path(), temporary.path() }, NoLimit );
        REQUIRE( location.directory == ownDirectoryUnder( temporary.path() ) );
        REQUIRE( location.fallback );
        REQUIRE(
            singleInstanceLocation( name, {}, { runtime.path() }, NoLimit ).directory.isEmpty() );
    }

    SECTION( "a symbolic link in its place is not followed" )
    {
        REQUIRE( QFile::link( runtime.path(), ownDirectoryUnder( temporary.path() ) ) );

        REQUIRE(
            singleInstanceLocation( name, {}, { temporary.path() }, NoLimit ).directory.isEmpty() );
    }

    SECTION( "no directory at all gives none" )
    {
        REQUIRE( singleInstanceLocation( name, {}, {}, NoLimit ).directory.isEmpty() );
        REQUIRE( singleInstanceLocation( name, {}, { {} }, NoLimit ).directory.isEmpty() );
    }
}

TEST_CASE( "The single-instance directory is a shorter one when the socket's path does not fit "
           "the first",
           "[singleinstance]" )
{
    // A deep temporary directory, like TMPDIR inside a scratch folder, and a
    // short one, like /tmp: the socket goes where its path fits (#841).
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto name = guardName();
    const QString sessionId = "12";
    const auto shortBase = scratch.path();
    const auto deepBase = deepDirectoryUnder( shortBase, 120 );
    const auto deepSocket = ownDirectoryUnder( deepBase ) + '/' + name + '-' + sessionId;
    const auto shortSocket = ownDirectoryUnder( shortBase ) + '/' + name + '-' + sessionId;
    REQUIRE( socketPathBytes( deepSocket ) > socketPathBytes( shortSocket ) );

    SECTION( "the deep directory when it fits" )
    {
        const auto location = singleInstanceLocation( name, sessionId, { deepBase, shortBase },
                                                      socketPathBytes( deepSocket ) );
        REQUIRE( location.directory == ownDirectoryUnder( deepBase ) );
        REQUIRE( location.socketName == name + '-' + sessionId );
        REQUIRE_FALSE( location.fallback );
    }

    SECTION( "the short directory, owner-only, when the deep one is a byte too deep" )
    {
        const auto location = singleInstanceLocation( name, sessionId, { deepBase, shortBase },
                                                      socketPathBytes( deepSocket ) - 1 );
        REQUIRE( location.directory == ownDirectoryUnder( shortBase ) );
        REQUIRE( location.socketName == name + '-' + sessionId );
        REQUIRE( location.fallback );
        REQUIRE( isOwnerOnlyDirectory( location.directory ) );
        REQUIRE_FALSE( QFileInfo::exists( ownDirectoryUnder( deepBase ) ) );
    }

    SECTION( "the session id goes when the socket fits no directory with it" )
    {
        const auto location = singleInstanceLocation( name, sessionId, { deepBase, shortBase },
                                                      socketPathBytes( shortSocket ) - 1 );
        REQUIRE( location.directory == ownDirectoryUnder( shortBase ) );
        REQUIRE( location.socketName == name );
        REQUIRE( location.fallback );
        REQUIRE( localSocketPathFits( location.directory, location.socketName,
                                      socketPathBytes( shortSocket ) - 1 ) );
    }

    SECTION( "a shorter directory comes before a shorter socket name" )
    {
        // The session id is worth more than the first directory: one primary
        // instance per login session stays, the socket moves.
        const auto deepWithoutSession = ownDirectoryUnder( deepBase ) + '/' + name;
        const auto location = singleInstanceLocation( name, sessionId, { deepBase, shortBase },
                                                      socketPathBytes( deepWithoutSession ) );
        REQUIRE( location.directory == ownDirectoryUnder( shortBase ) );
        REQUIRE( location.socketName == name + '-' + sessionId );
        REQUIRE( location.fallback );
    }

    SECTION( "a socket that fits nowhere is placed in the last directory the user can have" )
    {
        const auto location = singleInstanceLocation( name, sessionId, { deepBase, shortBase }, 1 );
        REQUIRE( location.directory == ownDirectoryUnder( shortBase ) );
        REQUIRE( location.socketName == name );
        REQUIRE( location.fallback );
    }
}

TEST_CASE( "A primary instance listens from the fallback directory of a deep temporary "
           "directory, and a second instance hands over",
           "[singleinstance]" )
{
    // TMPDIR (and the runtime directory on Linux) too deep for the socket's
    // path: the location is the user's own under /tmp, and a guard there
    // listens, where one under TMPDIR could not (#841).
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto deep = deepDirectoryUnder( scratch.path(), localSocketPathLimit() );
    const ScopedEnvironment temporary( "TMPDIR", deep );
    const ScopedEnvironment runtime( "XDG_RUNTIME_DIR", deep );
    const auto name = guardName();

    const auto location = singleInstanceLocation( name );
    REQUIRE( location.directory == ownDirectoryUnder( "/tmp" ) );
    REQUIRE( location.socketName.startsWith( name ) );
    REQUIRE( location.fallback );
    REQUIRE( isOwnerOnlyDirectory( location.directory ) );
    REQUIRE(
        localSocketPathFits( location.directory, location.socketName, localSocketPathLimit() ) );

    KDSingleApplication primary( location.socketName, location.directory );
    REQUIRE( primary.isPrimaryInstance() );
    REQUIRE( primary.isListening() );
    REQUIRE( primary.socketName() == location.directory + '/' + location.socketName );
    REQUIRE( isOwnerOnlySocket( primary.socketName() ) );
    QSignalSpy received( &primary, &KDSingleApplication::messageReceived );

    KDSingleApplication secondary( location.socketName, location.directory );
    REQUIRE_FALSE( secondary.isPrimaryInstance() );
    REQUIRE( secondary.sendMessageWithResult( "hello", 2000 )
             == KDSingleApplication::SendResult::Sent );
    REQUIRE( received.wait( 5000 ) );
    REQUIRE( received.first().first().toByteArray() == QByteArray( "hello" ) );
}
#else
TEST_CASE( "The single-instance directory is the temporary directory on Windows",
           "[singleinstance]" )
{
    const auto location = singleInstanceLocation( "n", "7", { "C:/temporary" }, 0 );
    REQUIRE( location.directory == "C:/temporary" );
    REQUIRE( location.socketName == "n" );
    REQUIRE_FALSE( location.fallback );
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

    const auto location = singleInstanceLocation( guardName() );
    REQUIRE_FALSE( location.directory.isEmpty() );
    REQUIRE_FALSE( location.fallback );
#ifdef Q_OS_WIN
    REQUIRE( sameSpelling( location.directory ) == sameSpelling( isolated.path() ) );
#else
    REQUIRE( sameSpelling( QFileInfo( location.directory ).absolutePath() )
             == sameSpelling( isolated.path() ) );
    REQUIRE( isOwnerOnlyDirectory( location.directory ) );
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
    // The name as given: the session id is the caller's to add (#841).
    REQUIRE( primary.socketName() == directory.path() + '/' + name );
    REQUIRE( isOwnerOnlySocket( primary.socketName() ) );
#endif
}

#ifndef Q_OS_WIN
// On Windows the lock file is let go after its unlock, so the folder stays.
TEST_CASE( "The directory of the lock and the socket goes with the primary instance once it is "
           "empty",
           "[singleinstance]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto directory = root.filePath( "instance" );
    REQUIRE( QDir().mkpath( directory ) );
    const auto name = guardName();

    {
        const KDSingleApplication primary( name, directory );
        REQUIRE( primary.isPrimaryInstance() );
        REQUIRE( QDir( directory ).exists() );
    }
    // A run leaves nothing of its own behind (#827).
    REQUIRE_FALSE( QDir( directory ).exists() );

    SECTION( "unless something else is in it" )
    {
        REQUIRE( QDir().mkpath( directory ) );
        QFile other( QDir( directory ).filePath( "other" ) );
        REQUIRE( other.open( QIODevice::WriteOnly ) );
        other.close();
        {
            const KDSingleApplication primary( name, directory );
            REQUIRE( primary.isPrimaryInstance() );
        }
        REQUIRE( QDir( directory ).exists() );
        REQUIRE( QDir( directory ).entryList( QDir::Files | QDir::System )
                 == QStringList{ "other" } );
    }
}
#endif

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

    REQUIRE( sendFromAnotherThread( secondary, "hello", 2000 )
             == KDSingleApplication::SendResult::Sent );
    // The message may have arrived while the send ran the event loop.
    REQUIRE( ( !received.isEmpty() || received.wait( 5000 ) ) );
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
#ifndef Q_OS_WIN
        listener.setSocketOptions( QLocalServer::UserAccessOption );
#endif
        REQUIRE( listener.listen( secondary.socketName() ) );

        REQUIRE( sendFromAnotherThread( secondary, "hello", 2000 )
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
