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

// What the Logger writes to its file, and what it leaves out (#444), and
// where it puts that file (#829).

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include "logger.h"

namespace {

// The log file of this process under the data folder given: the Logger names
// it after the process and keeps it in `logs` there.
QStringList logFilesOfThisProcess( const QString& dataDirectory )
{
    const auto suffix = QStringLiteral( "_%1.log" ).arg( QCoreApplication::applicationPid() );
    QStringList found;
    const QDir logs( QDir( dataDirectory ).filePath( "logs" ) );
    for ( const auto& name : logs.entryList( { "logsquirl_*" + suffix }, QDir::Files ) ) {
        found << logs.filePath( name );
    }
    return found;
}

QString readAll( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() );
}

// Puts the Logger back to what the test binary starts with: the console at
// the default level, no file. The data folder it logged under goes with the
// test's QTemporaryDir.
struct RestoreLogging {
    ~RestoreLogging()
    {
        logging::enableFileLogging( false );
        logging::enableLogging();
    }
};

} // namespace

TEST_CASE( "File logging writes the messages of its level and above to a log file", "[logger]" )
{
    const RestoreLogging restore;
    const QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );

    logging::enableFileLogging( true, logging::LogLevel::Warning, dataDir.path() );
    LOG_DEBUG << "logger test debug marker";
    LOG_INFO << "logger test info marker";
    LOG_WARNING << "logger test warning marker";
    LOG_ERROR << "logger test error marker";
    // Closing the file flushes what was buffered.
    logging::enableFileLogging( false );

    const auto files = logFilesOfThisProcess( dataDir.path() );
    REQUIRE( files.size() == 1 );
    const auto written = readAll( files[ 0 ] );
    CHECK_FALSE( written.contains( "logger test debug marker" ) );
    CHECK_FALSE( written.contains( "logger test info marker" ) );
    CHECK( written.contains( "logger test warning marker" ) );
    CHECK( written.contains( "logger test error marker" ) );
}

TEST_CASE( "A lower level lets more messages through", "[logger]" )
{
    const RestoreLogging restore;
    const QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );

    logging::enableFileLogging( true, logging::LogLevel::Debug, dataDir.path() );
    LOG_DEBUG << "logger test debug marker";
    LOG_INFO << "logger test info marker";
    logging::enableFileLogging( false );

    const auto files = logFilesOfThisProcess( dataDir.path() );
    REQUIRE( files.size() == 1 );
    const auto written = readAll( files[ 0 ] );
    CHECK( written.contains( "logger test debug marker" ) );
    CHECK( written.contains( "logger test info marker" ) );
    // The line carries its time, its level and where it was logged.
    CHECK( written.contains( "debug" ) );
    CHECK( written.contains( "info" ) );
}

TEST_CASE( "Messages after file logging is turned off do not reach the file", "[logger]" )
{
    const RestoreLogging restore;
    const QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );

    logging::enableFileLogging( true, logging::LogLevel::Info, dataDir.path() );
    LOG_INFO << "logger test before marker";
    logging::enableFileLogging( false );
    LOG_INFO << "logger test after marker";

    const auto files = logFilesOfThisProcess( dataDir.path() );
    REQUIRE( files.size() == 1 );
    const auto written = readAll( files[ 0 ] );
    CHECK( written.contains( "logger test before marker" ) );
    CHECK_FALSE( written.contains( "logger test after marker" ) );
}

TEST_CASE( "With no logging enabled, a message is not even formatted", "[logger]" )
{
    const RestoreLogging restore;
    logging::enableFileLogging( false );
    logging::enableLogging( false );

    int formatted = 0;
    const auto counted = [ &formatted ] {
        ++formatted;
        return "counted";
    };
    LOG_INFO << counted();
    LOG_ERROR << counted();
    CHECK( formatted == 0 );

    logging::enableLogging( true, logging::LogLevel::Info );
    LOG_INFO << counted();
    CHECK( formatted == 1 );
}

TEST_CASE( "The log file lies in the logs folder of the data folder and is the user's own",
           "[logger]" )
{
    const RestoreLogging restore;
    const QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );

    logging::enableFileLogging( true, logging::LogLevel::Info, dataDir.path() );
    LOG_INFO << "logger test place marker";
    logging::enableFileLogging( false );

    // Not the shared temporary folder, where another user could read what
    // the log says about the user's files and searches (#829).
    const auto files = logFilesOfThisProcess( dataDir.path() );
    REQUIRE( files.size() == 1 );
    CHECK( readAll( files[ 0 ] ).contains( "logger test place marker" ) );
    CHECK( QFileInfo( files[ 0 ] ).fileName().startsWith( "logsquirl_" ) );

#ifndef Q_OS_WIN
    const auto othersBits = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                            | QFileDevice::ReadOther | QFileDevice::WriteOther
                            | QFileDevice::ExeOther;
    CHECK( ( QFile::permissions( files[ 0 ] ) & othersBits ) == 0 );
    const auto logsFolder = QDir( dataDir.path() ).filePath( "logs" );
    CHECK( ( QFile::permissions( logsFolder ) & othersBits ) == 0 );
#endif
}

#ifndef Q_OS_WIN
TEST_CASE( "A symbolic link at the log file's path is not followed", "[logger]" )
{
    const RestoreLogging restore;
    const QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );
    const QDir logs( QDir( dataDir.path() ).filePath( "logs" ) );
    REQUIRE( logs.mkpath( "." ) );

    // The name is the second logging is enabled in and the pid: planted for
    // this second and the next, so the link is there whichever it is.
    const auto target = QDir( dataDir.path() ).filePath( "elsewhere.log" );
    const auto now = QDateTime::currentDateTime();
    for ( const auto& when : { now, now.addSecs( 1 ) } ) {
        const auto name = QStringLiteral( "logsquirl_%1_%2.log" )
                              .arg( when.toString( "yyyy-MM-dd_HH-mm-ss" ) )
                              .arg( QCoreApplication::applicationPid() );
        REQUIRE( QFile::link( target, logs.filePath( name ) ) );
    }

    logging::enableFileLogging( true, logging::LogLevel::Warning, dataDir.path() );
    LOG_WARNING << "logger test link marker";
    logging::enableFileLogging( false );

    CHECK_FALSE( QFileInfo::exists( target ) );
    for ( const auto& path : logFilesOfThisProcess( dataDir.path() ) ) {
        CHECK( QFileInfo( path ).isSymLink() );
    }
}
#endif

TEST_CASE( "Without a data folder the log file goes to a folder of the user's own under the "
           "temporary directory",
           "[logger]" )
{
    const RestoreLogging restore;

    logging::enableFileLogging( true, logging::LogLevel::Info, {} );
    LOG_INFO << "logger test fallback marker";
    logging::enableFileLogging( false );

    const auto suffix = QStringLiteral( "_%1.log" ).arg( QCoreApplication::applicationPid() );
    QStringList found;
    for ( const auto& folder :
          QDir::temp().entryList( { "logsquirl_logs*" }, QDir::Dirs | QDir::NoDotAndDotDot ) ) {
        const QDir logs( QDir::temp().filePath( folder ) );
        for ( const auto& name : logs.entryList( { "logsquirl_*" + suffix }, QDir::Files ) ) {
            found << logs.filePath( name );
        }
    }
    REQUIRE( found.size() == 1 );
    CHECK( readAll( found[ 0 ] ).contains( "logger test fallback marker" ) );
#ifndef Q_OS_WIN
    const auto othersBits = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                            | QFileDevice::ReadOther | QFileDevice::WriteOther
                            | QFileDevice::ExeOther;
    CHECK( ( QFile::permissions( found[ 0 ] ) & othersBits ) == 0 );
    CHECK( ( QFile::permissions( QFileInfo( found[ 0 ] ).path() ) & othersBits ) == 0 );
#endif
    QFile::remove( found[ 0 ] );
}
