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

// What the Logger writes to its file, and what it leaves out (#444).

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>

#include "logger.h"

namespace {

// The log file of this process: the Logger names it after the process.
QStringList logFilesOfThisProcess()
{
    const auto suffix = QStringLiteral( "_%1.log" ).arg( QCoreApplication::applicationPid() );
    QStringList found;
    const QDir temp = QDir::temp();
    for ( const auto& name : temp.entryList( { "logsquirl_*" + suffix }, QDir::Files ) ) {
        found << temp.filePath( name );
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
// the default level, no file.
struct RestoreLogging {
    ~RestoreLogging()
    {
        logging::enableFileLogging( false );
        logging::enableLogging();
        for ( const auto& path : logFilesOfThisProcess() ) {
            QFile::remove( path );
        }
    }
};

} // namespace

TEST_CASE( "File logging writes the messages of its level and above to a log file", "[logger]" )
{
    const RestoreLogging restore;
    for ( const auto& path : logFilesOfThisProcess() ) {
        QFile::remove( path );
    }

    logging::enableFileLogging( true, logging::LogLevel::Warning );
    LOG_DEBUG << "logger test debug marker";
    LOG_INFO << "logger test info marker";
    LOG_WARNING << "logger test warning marker";
    LOG_ERROR << "logger test error marker";
    // Closing the file flushes what was buffered.
    logging::enableFileLogging( false );

    const auto files = logFilesOfThisProcess();
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
    for ( const auto& path : logFilesOfThisProcess() ) {
        QFile::remove( path );
    }

    logging::enableFileLogging( true, logging::LogLevel::Debug );
    LOG_DEBUG << "logger test debug marker";
    LOG_INFO << "logger test info marker";
    logging::enableFileLogging( false );

    const auto files = logFilesOfThisProcess();
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
    for ( const auto& path : logFilesOfThisProcess() ) {
        QFile::remove( path );
    }

    logging::enableFileLogging( true, logging::LogLevel::Info );
    LOG_INFO << "logger test before marker";
    logging::enableFileLogging( false );
    LOG_INFO << "logger test after marker";

    const auto files = logFilesOfThisProcess();
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
