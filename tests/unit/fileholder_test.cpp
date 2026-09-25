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

// The File Holder keeps the file a Log Data reads: open for as long as it
// lives, or closed while no reader is attached (#444).

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include "fileholder.h"

namespace {

QString writeFile( const QTemporaryDir& directory, const QString& name, const QByteArray& content )
{
    const auto path = directory.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( content );
    return path;
}

} // namespace

TEST_CASE( "A File Holder that keeps the file open reads it from the first call", "[fileholder]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = writeFile( directory, "held.log", "one\ntwo\n" );

    FileHolder holder( false );
    CHECK_FALSE( holder.isOpen() );
    CHECK( holder.size() == 0 );

    holder.open( path );
    CHECK( holder.isOpen() );
    CHECK( holder.size() == 8 );

    SECTION( "a scoped reader reads through the file and leaves it open" )
    {
        {
            ScopedFileHolder<FileHolder> reader( &holder );
            REQUIRE( reader.getFile() != nullptr );
            CHECK( reader.getFile()->readAll() == "one\ntwo\n" );
        }
        CHECK( holder.isOpen() );
    }

    SECTION( "the size follows the file as it grows" )
    {
        QFile appended( path );
        REQUIRE( appended.open( QIODevice::WriteOnly | QIODevice::Append ) );
        appended.write( "three\n" );
        appended.close();
        CHECK( holder.size() == 14 );
    }
}

TEST_CASE( "A File Holder that keeps the file closed opens it for the readers only",
           "[fileholder]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = writeFile( directory, "closed.log", "one\ntwo\n" );

    FileHolder holder( true );
    holder.open( path );
    CHECK_FALSE( holder.isOpen() );

    {
        ScopedFileHolder<FileHolder> first( &holder );
        CHECK( holder.isOpen() );
        REQUIRE( first.getFile() != nullptr );
        CHECK( first.getFile()->readAll() == "one\ntwo\n" );

        {
            // A second reader shares the open file.
            ScopedFileHolder<FileHolder> second( &holder );
            CHECK( second.getFile() == first.getFile() );
        }
        // The first is still reading.
        CHECK( holder.isOpen() );
    }
    // The last reader closed it.
    CHECK_FALSE( holder.isOpen() );

    {
        // And the next one opens it again.
        ScopedFileHolder<FileHolder> again( &holder );
        CHECK( holder.isOpen() );
        CHECK( again.getFile()->readAll() == "one\ntwo\n" );
    }
    CHECK_FALSE( holder.isOpen() );
}

TEST_CASE( "A File Holder that cannot read the file is not open", "[fileholder]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );

    FileHolder holder( false );
    holder.open( directory.filePath( "missing.log" ) );
    CHECK_FALSE( holder.isOpen() );
    CHECK( holder.size() == 0 );
}

TEST_CASE( "Opening the file again follows a file that was replaced", "[fileholder]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = writeFile( directory, "replaced.log", "old\n" );

    FileHolder holder( false );
    holder.open( path );
    const auto before = holder.getFileId();
    REQUIRE( holder.isOpen() );

    // A rotation: the name now points to another file.
    const auto moved = directory.filePath( "replaced.log.1" );
    REQUIRE( QFile::rename( path, moved ) );
    writeFile( directory, "replaced.log", "a new and longer file\n" );

    // Until it is opened again the holder still has the old file.
    CHECK( holder.getFileId().fileIndex == before.fileIndex );
    CHECK( holder.size() == 4 );

    holder.reOpenFile();
    CHECK( holder.getFileId() != before );
    CHECK( holder.size() == 22 );
}

TEST_CASE( "The identity of a file is the same for the same file and differs between files",
           "[fileholder]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto first = writeFile( directory, "first.log", "1\n" );
    const auto second = writeFile( directory, "second.log", "2\n" );

    const auto id = FileId::getFileId( first );
    CHECK_FALSE( id != FileId::getFileId( first ) );
    CHECK( id != FileId::getFileId( second ) );
    // A file that is not there has the empty identity.
    const auto none = FileId::getFileId( directory.filePath( "none.log" ) );
    CHECK( none.fileIndex == 0 );
    CHECK( none.volumeIndex == 0 );
}
