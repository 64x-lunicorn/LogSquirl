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

#include <catch2/catch.hpp>

#include <QByteArray>
#include <QTemporaryFile>

#include <lz4frame.h>

#include "lz4device.h"

namespace {

/// Compress raw data with LZ4 frame format and write to a temporary file.
/// Returns true on success; the QTemporaryFile remains open for reading the name.
bool writeLz4File( QTemporaryFile& file, const QByteArray& rawData )
{
    if ( !file.open() ) {
        return false;
    }

    const auto maxDst = LZ4F_compressFrameBound(
        static_cast<std::size_t>( rawData.size() ), nullptr );

    QByteArray compressed( static_cast<int>( maxDst ), Qt::Uninitialized );

    const auto compressedSize = LZ4F_compressFrame(
        compressed.data(), maxDst,
        rawData.constData(), static_cast<std::size_t>( rawData.size() ),
        nullptr );

    if ( LZ4F_isError( compressedSize ) ) {
        return false;
    }

    file.write( compressed.constData(), static_cast<qint64>( compressedSize ) );
    file.close();
    return true;
}

} // namespace

SCENARIO( "Lz4Device decompresses a single-frame LZ4 stream", "[lz4device]" )
{
    GIVEN( "A temporary file containing LZ4-compressed text" )
    {
        const QByteArray original = "Hello, LogSquirl!\nLine two\nLine three\n";

        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "lz4_test_XXXXXX.lz4" );
        REQUIRE( writeLz4File( tmpFile, original ) );

        WHEN( "Lz4Device reads the file" )
        {
            Lz4Device device( tmpFile.fileName() );
            REQUIRE( device.open( QIODevice::ReadOnly ) );

            QByteArray result;
            while ( !device.atEnd() ) {
                const auto chunk = device.read( 16 );
                REQUIRE( chunk.size() >= 0 );
                result.append( chunk );
            }
            device.close();

            THEN( "The decompressed output matches the original" )
            {
                REQUIRE( result == original );
            }
        }
    }
}

SCENARIO( "Lz4Device handles a large payload", "[lz4device]" )
{
    GIVEN( "A 256 KiB repeated-pattern payload" )
    {
        QByteArray original;
        original.reserve( 256 * 1024 );
        for ( int i = 0; i < 256 * 1024 / 64; ++i ) {
            original.append( "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB" );
        }

        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "lz4_large_XXXXXX.lz4" );
        REQUIRE( writeLz4File( tmpFile, original ) );

        WHEN( "Lz4Device reads the entire file at once" )
        {
            Lz4Device device( tmpFile.fileName() );
            REQUIRE( device.open( QIODevice::ReadOnly ) );

            const auto result = device.readAll();
            device.close();

            THEN( "All bytes match" )
            {
                REQUIRE( result.size() == original.size() );
                REQUIRE( result == original );
            }
        }
    }
}

SCENARIO( "Lz4Device rejects write-only mode", "[lz4device]" )
{
    GIVEN( "A valid LZ4 file" )
    {
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "lz4_rej_XXXXXX.lz4" );
        REQUIRE( writeLz4File( tmpFile, "test" ) );

        WHEN( "Opened in WriteOnly mode" )
        {
            Lz4Device device( tmpFile.fileName() );

            THEN( "open() returns false" )
            {
                REQUIRE_FALSE( device.open( QIODevice::WriteOnly ) );
            }
        }
    }
}

SCENARIO( "Lz4Device reports sequential", "[lz4device]" )
{
    GIVEN( "An Lz4Device instance" )
    {
        Lz4Device device( "/nonexistent" );

        THEN( "isSequential returns true" )
        {
            REQUIRE( device.isSequential() );
        }
    }
}
