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

#include <zstd.h>

#include "zstddevice.h"

namespace {

/// Compress raw data with Zstd and write to a temporary file.
/// Returns true on success; the QTemporaryFile remains open.
bool writeZstdFile( QTemporaryFile& file, const QByteArray& rawData )
{
    if ( !file.open() ) {
        return false;
    }

    const auto maxDst = ZSTD_compressBound( static_cast<std::size_t>( rawData.size() ) );
    QByteArray compressed( static_cast<int>( maxDst ), Qt::Uninitialized );

    const auto compressedSize = ZSTD_compress(
        compressed.data(), maxDst,
        rawData.constData(), static_cast<std::size_t>( rawData.size() ),
        1 /* compression level */ );

    if ( ZSTD_isError( compressedSize ) ) {
        return false;
    }

    file.write( compressed.constData(), static_cast<qint64>( compressedSize ) );
    file.close();
    return true;
}

} // namespace

SCENARIO( "ZstdDevice decompresses a single-frame Zstd stream", "[zstddevice]" )
{
    GIVEN( "A temporary file containing Zstd-compressed text" )
    {
        const QByteArray original = "Hello, LogSquirl!\nLine two\nLine three\n";

        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "zstd_test_XXXXXX.zst" );
        REQUIRE( writeZstdFile( tmpFile, original ) );

        WHEN( "ZstdDevice reads the file" )
        {
            ZstdDevice device( tmpFile.fileName() );
            REQUIRE( device.open( QIODevice::ReadOnly ) );

            QByteArray result;
            while ( !device.atEnd() ) {
                const auto chunk = device.read( 16 );
                if ( chunk.isEmpty() ) {
                    break; // safety: avoid infinite loop
                }
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

SCENARIO( "ZstdDevice handles a large payload", "[zstddevice]" )
{
    GIVEN( "A 256 KiB repeated-pattern payload" )
    {
        QByteArray original;
        original.reserve( 256 * 1024 );
        for ( int i = 0; i < 256 * 1024 / 64; ++i ) {
            original.append( "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" );
        }

        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "zstd_large_XXXXXX.zst" );
        REQUIRE( writeZstdFile( tmpFile, original ) );

        WHEN( "ZstdDevice reads the entire file at once" )
        {
            ZstdDevice device( tmpFile.fileName() );
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

SCENARIO( "ZstdDevice rejects write-only mode", "[zstddevice]" )
{
    GIVEN( "A valid Zstd file" )
    {
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate( "zstd_rej_XXXXXX.zst" );
        REQUIRE( writeZstdFile( tmpFile, "test" ) );

        WHEN( "Opened in WriteOnly mode" )
        {
            ZstdDevice device( tmpFile.fileName() );

            THEN( "open() returns false" )
            {
                REQUIRE_FALSE( device.open( QIODevice::WriteOnly ) );
            }
        }
    }
}

SCENARIO( "ZstdDevice reports sequential", "[zstddevice]" )
{
    GIVEN( "A ZstdDevice instance" )
    {
        ZstdDevice device( "/nonexistent" );

        THEN( "isSequential returns true" )
        {
            REQUIRE( device.isSequential() );
        }
    }
}
