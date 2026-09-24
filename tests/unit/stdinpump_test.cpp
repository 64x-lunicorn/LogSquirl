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

#include <catch2/catch_test_macros.hpp>

#include "stdinpump.h"
#include "streamwriter.h"

#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using logsquirl::plugins::StdinPump;
using logsquirl::plugins::StreamWriter;

namespace {

QByteArray contentOf( const StreamWriter& writer )
{
    QFile file( writer.filePath() );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
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

TEST_CASE( "A dash on the command line asks for standard input, not for a file", "[stdinpump]" )
{
    const auto arguments
        = logsquirl::plugins::splitPositionalArguments( { "a.log", "-", "b.log" } );
    CHECK( arguments.readStdin );
    REQUIRE( arguments.files.size() == 2 );
    CHECK( arguments.files[ 0 ] == "a.log" );
    CHECK( arguments.files[ 1 ] == "b.log" );

    const auto none = logsquirl::plugins::splitPositionalArguments( { "a.log" } );
    CHECK_FALSE( none.readStdin );
    CHECK( none.files.size() == 1 );
}

#ifndef Q_OS_WIN

TEST_CASE( "StdinPump copies every byte of a pipe into the stream file", "[stdinpump]" )
{
    int fds[ 2 ];
    REQUIRE( ::pipe( fds ) == 0 );

    StreamWriter writer( "stdin" );
    std::atomic_bool closed = false;
    auto pump = std::make_unique<StdinPump>( fds[ 0 ], writer, [ &closed ] { closed = true; } );

    SECTION( "the last line keeps its missing newline" )
    {
        REQUIRE( ::write( fds[ 1 ], "a\nb\nc", 5 ) == 5 );
        ::close( fds[ 1 ] );

        REQUIRE( eventually( [ &closed ] { return closed.load(); } ) );
        CHECK( contentOf( writer ) == QByteArray( "a\nb\nc" ) );
        CHECK( writer.isFinished() );
        fds[ 1 ] = -1;
    }

    SECTION( "data arriving later is appended while the pipe stays open" )
    {
        REQUIRE( ::write( fds[ 1 ], "one\n", 4 ) == 4 );
        REQUIRE( eventually( [ &writer ] { return QFileInfo( writer.filePath() ).size() == 4; } ) );
        CHECK_FALSE( closed.load() );
        CHECK_FALSE( writer.isFinished() );

        REQUIRE( ::write( fds[ 1 ], "two\n", 4 ) == 4 );
        ::close( fds[ 1 ] );
        REQUIRE( eventually( [ &closed ] { return closed.load(); } ) );
        CHECK( contentOf( writer ) == QByteArray( "one\ntwo\n" ) );
        fds[ 1 ] = -1;
    }

    SECTION( "destroying the pump while the pipe is open does not hang" )
    {
        REQUIRE( ::write( fds[ 1 ], "x", 1 ) == 1 );
        REQUIRE( eventually( [ &writer ] { return QFileInfo( writer.filePath() ).size() == 1; } ) );
        pump.reset();
        CHECK_FALSE( closed.load() );
    }

    pump.reset();
    ::close( fds[ 0 ] );
    if ( fds[ 1 ] >= 0 ) {
        ::close( fds[ 1 ] );
    }
}

TEST_CASE( "A pipe is not a terminal", "[stdinpump]" )
{
    int fds[ 2 ];
    REQUIRE( ::pipe( fds ) == 0 );
    CHECK_FALSE( logsquirl::plugins::isTerminal( fds[ 0 ] ) );
    ::close( fds[ 0 ] );
    ::close( fds[ 1 ] );
}

#endif
