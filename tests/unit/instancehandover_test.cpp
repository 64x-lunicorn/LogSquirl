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

// A secondary instance hands its Log Files and standard input over to the
// primary instance in one message; standard input goes as a spool file the
// secondary keeps writing and the primary owns (#623).

#include <catch2/catch_test_macros.hpp>

#include "instancehandover.h"
#include "streamwriter.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using logsquirl::plugins::StreamWriter;

namespace {

const auto Version = QStringLiteral( "26.10.1" );

QByteArray contentOf( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return file.readAll();
}

// Removes a spool file the test was handed, and its folder.
void removeSpool( const QString& path )
{
    QFile::remove( path );
    QDir().rmdir( QFileInfo( path ).absolutePath() );
}

template <typename Condition>
bool eventually( Condition&& condition )
{
    for ( int attempt = 0; attempt < 500; ++attempt ) {
        if ( condition() ) {
            return true;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    return false;
}

} // namespace

TEST_CASE( "A hand-over message carries the Log Files and the spool of standard input",
           "[handover]" )
{
    const HandOver sent{ { "/logs/a.log", "/logs/b.log" }, "/tmp/logsquirl-x/stream.log" };

    const auto received = readHandOverMessage( handOverMessage( sent, Version ), Version );

    REQUIRE( received.has_value() );
    CHECK( received->files == sent.files );
    CHECK( received->standardInputSpool == sent.standardInputSpool );
}

TEST_CASE( "A hand-over without standard input is the message of before #623", "[handover]" )
{
    const auto message = handOverMessage( HandOver{ { "/logs/a.log" }, {} }, Version );
    const auto map = QCborValue::fromCbor( message ).toMap();

    CHECK( map.size() == 2 );
    CHECK( map.value( QStringLiteral( "version" ) ).toString() == Version );
    CHECK( map.value( QStringLiteral( "files" ) ).toArray().size() == 1 );
}

TEST_CASE( "A hand-over message from before #623 has no standard input", "[handover]" )
{
    QCborMap old;
    old.insert( QStringLiteral( "version" ), Version );
    old.insert( QStringLiteral( "files" ),
                QCborValue::fromVariant( QStringList{ "/logs/a.log" } ) );

    const auto received = readHandOverMessage( old.toCborValue().toCbor(), Version );

    REQUIRE( received.has_value() );
    CHECK( received->files == std::vector<QString>{ "/logs/a.log" } );
    CHECK( received->standardInputSpool.isEmpty() );
}

TEST_CASE( "A hand-over message from another version is not read", "[handover]" )
{
    const auto message = handOverMessage( HandOver{ { "/logs/a.log" }, "/tmp/x/stream.log" },
                                          QStringLiteral( "26.10.0" ) );

    CHECK_FALSE( readHandOverMessage( message, Version ).has_value() );
    CHECK_FALSE( readHandOverMessage( QByteArray( "not cbor" ), Version ).has_value() );
}

TEST_CASE( "Only a spool file of standard input in the temporary folder counts as one",
           "[handover]" )
{
    StreamWriter writer( "stdin" );
    REQUIRE_FALSE( writer.filePath().isEmpty() );
    CHECK( isStandardInputSpool( writer.filePath() ) );

    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto other = directory.filePath( "other.log" );
    QFile file( other );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.close();
    CHECK_FALSE( isStandardInputSpool( other ) );
    CHECK_FALSE( isStandardInputSpool( directory.filePath( "stream.log" ) ) );
    CHECK_FALSE( isStandardInputSpool( QString{} ) );
}

#ifndef Q_OS_WIN

TEST_CASE( "A secondary instance hands standard input over and writes it as it arrives",
           "[handover]" )
{
    int fds[ 2 ];
    REQUIRE( ::pipe( fds ) == 0 );

    QString spool;
    bool sentBeforeInput = false;
    std::atomic_bool firstLineArrived = false;

    // Writes one line, waits for it to reach the spool file, then writes the
    // rest and closes the pipe. No Catch2 assertion runs on this thread.
    std::atomic_bool written = true;
    std::atomic<bool> spoolKnown = false;
    std::thread writer( [ & ] {
        written = written && ::write( fds[ 1 ], "1\n", 2 ) == 2;
        firstLineArrived
            = eventually( [ & ] { return spoolKnown && contentOf( spool ) == "1\n"; } );
        written = written && ::write( fds[ 1 ], "2\n3\n4\n5\n", 8 ) == 8;
        ::close( fds[ 1 ] );
    } );

    std::ostringstream errors;
    const auto exitCode = handOverStandardInput(
        { "/logs/a.log" }, fds[ 0 ], Version,
        [ & ]( const QByteArray& message ) {
            const auto handOver = readHandOverMessage( message, Version );
            if ( handOver ) {
                spool = handOver->standardInputSpool;
                sentBeforeInput = handOver->files == std::vector<QString>{ "/logs/a.log" }
                                  && QFileInfo::exists( spool );
                spoolKnown = true;
            }
            return true;
        },
        errors );
    writer.join();
    ::close( fds[ 0 ] );

    CHECK( written );
    CHECK( exitCode == EXIT_SUCCESS );
    CHECK( errors.str().empty() );
    CHECK( sentBeforeInput );
    CHECK( firstLineArrived );
    CHECK( isStandardInputSpool( spool ) );
    // The primary instance owns the file now: it stays.
    CHECK( contentOf( spool ) == "1\n2\n3\n4\n5\n" );

    removeSpool( spool );
}

TEST_CASE( "A secondary instance that cannot hand standard input over says so and leaves "
           "no spool file",
           "[handover]" )
{
    int fds[ 2 ];
    REQUIRE( ::pipe( fds ) == 0 );
    REQUIRE( ::write( fds[ 1 ], "1\n", 2 ) == 2 );
    ::close( fds[ 1 ] );

    QString spool;
    std::ostringstream errors;
    const auto exitCode = handOverStandardInput(
        {}, fds[ 0 ], Version,
        [ & ]( const QByteArray& message ) {
            if ( const auto handOver = readHandOverMessage( message, Version ) ) {
                spool = handOver->standardInputSpool;
            }
            return false;
        },
        errors );
    ::close( fds[ 0 ] );

    CHECK( exitCode != EXIT_SUCCESS );
    CHECK_FALSE( errors.str().empty() );
    REQUIRE_FALSE( spool.isEmpty() );
    CHECK_FALSE( QFileInfo::exists( spool ) );
    CHECK_FALSE( QFileInfo::exists( QFileInfo( spool ).absolutePath() ) );
}

#endif
