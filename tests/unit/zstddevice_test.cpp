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
#include <QEventLoop>
#include <QTemporaryFile>
#include <QTimer>

#include <chrono>
#include <memory>
#include <vector>

#include <zstd.h>

#include "atomicflag.h"
#include "decompressor.h"
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

    const auto compressedSize
        = ZSTD_compress( compressed.data(), maxDst, rawData.constData(),
                         static_cast<std::size_t>( rawData.size() ), 1 /* compression level */ );

    if ( ZSTD_isError( compressedSize ) ) {
        return false;
    }

    file.write( compressed.constData(), static_cast<qint64>( compressedSize ) );
    file.close();
    return true;
}

/// Compresses raw data into one complete Zstd frame.
QByteArray zstdFrame( const QByteArray& rawData )
{
    const auto maxDst = ZSTD_compressBound( static_cast<std::size_t>( rawData.size() ) );
    QByteArray compressed( static_cast<qsizetype>( maxDst ), Qt::Uninitialized );

    const auto compressedSize
        = ZSTD_compress( compressed.data(), maxDst, rawData.constData(),
                         static_cast<std::size_t>( rawData.size() ), 1 /* compression level */ );
    if ( ZSTD_isError( compressedSize ) ) {
        return {};
    }

    compressed.truncate( static_cast<qsizetype>( compressedSize ) );
    return compressed;
}

/// Log Lines "line <first>" to "line <last>", each ending in a newline.
QByteArray logLines( int first, int last )
{
    QByteArray lines;
    for ( int number = first; number <= last; ++number ) {
        lines.append( "line " ).append( QByteArray::number( number ) ).append( '\n' );
    }
    return lines;
}

/// Writes bytes as they are to a temporary .zst Log File.
bool writeRawFile( QTemporaryFile& file, const QByteArray& bytes )
{
    file.setFileTemplate( "zstd_log_XXXXXX.zst" );
    if ( !file.open() ) {
        return false;
    }
    const auto written = file.write( bytes );
    file.close();
    return written == bytes.size();
}

struct DecompressOutcome {
    bool finishedInTime = false;
    bool succeeded = false;
    QByteArray content;
};

/// Decompresses a Log File the way the main window does, but gives up after
/// the timeout instead of waiting forever for a decompression that hangs.
DecompressOutcome decompressWithin( const QString& logFilePath, std::chrono::milliseconds timeout )
{
    // The decompression runs on a pool thread and holds on to the interrupt
    // flag and the output file. If it hangs they must outlive this function.
    struct Run {
        AtomicFlag interrupt;
        QTemporaryFile output;
        Decompressor decompressor;
    };
    auto run = std::make_unique<Run>();

    DecompressOutcome outcome;
    if ( !run->output.open() ) {
        return outcome;
    }

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    QObject::connect( &run->decompressor, &Decompressor::finished, &loop, [ & ]( bool succeeded ) {
        outcome.finishedInTime = true;
        outcome.succeeded = succeeded;
        loop.quit();
    } );

    if ( !run->decompressor.decompress( logFilePath, &run->output, run->interrupt ) ) {
        return outcome;
    }
    deadline.start( timeout );
    loop.exec();

    if ( !outcome.finishedInTime ) {
        run->interrupt.set();
        static std::vector<std::unique_ptr<Run>> hungRuns;
        hungRuns.push_back( std::move( run ) );
        return outcome;
    }

    QFile decompressed( run->output.fileName() );
    if ( decompressed.open( QIODevice::ReadOnly ) ) {
        outcome.content = decompressed.readAll();
    }
    return outcome;
}

constexpr std::chrono::seconds kDecompressTimeout{ 20 };

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

SCENARIO( "A .zst Log File made of several frames decompresses completely",
          "[zstddevice][decompressor]" )
{
    GIVEN( "A Log File whose Log Lines are split over three concatenated frames" )
    {
        const auto firstPart = logLines( 1, 30000 );
        const auto secondPart = logLines( 30001, 60000 );
        const auto thirdPart = logLines( 60001, 60010 );

        const auto frames
            = zstdFrame( firstPart ) + zstdFrame( secondPart ) + zstdFrame( thirdPart );

        QTemporaryFile logFile;
        REQUIRE( writeRawFile( logFile, frames ) );

        WHEN( "the Log File is decompressed" )
        {
            const auto outcome = decompressWithin( logFile.fileName(), kDecompressTimeout );

            THEN( "every Log Line of every frame is there" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE( outcome.succeeded );
                REQUIRE( outcome.content.count( '\n' ) == 60010 );
                // Compared as a whole so a mismatch does not print megabytes.
                const bool sameContent = outcome.content == logLines( 1, 60010 );
                REQUIRE( sameContent );
            }
        }

        WHEN( "the frames are read through a ZstdDevice in small pieces" )
        {
            ZstdDevice device( logFile.fileName() );
            REQUIRE( device.open( QIODevice::ReadOnly ) );

            QByteArray result;
            while ( !device.atEnd() ) {
                const auto chunk = device.read( 1000 );
                if ( chunk.isEmpty() ) {
                    break;
                }
                result.append( chunk );
            }

            THEN( "the device ends only after the last frame" )
            {
                REQUIRE( result.size() == logLines( 1, 60010 ).size() );
                const bool sameContent = result == logLines( 1, 60010 );
                REQUIRE( sameContent );
            }
        }
    }
}

SCENARIO( "A truncated .zst Log File reports an error", "[zstddevice][decompressor]" )
{
    GIVEN( "A two-frame Log File cut off in the middle of its second frame" )
    {
        const auto firstFrame = zstdFrame( logLines( 1, 1000 ) );
        const auto secondFrame = zstdFrame( logLines( 1001, 20000 ) );
        const auto truncated = firstFrame + secondFrame.left( secondFrame.size() / 2 );

        QTemporaryFile logFile;
        REQUIRE( writeRawFile( logFile, truncated ) );

        WHEN( "the Log File is decompressed" )
        {
            const auto outcome = decompressWithin( logFile.fileName(), kDecompressTimeout );

            THEN( "the decompression finishes and fails" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE_FALSE( outcome.succeeded );
            }
        }
    }

    GIVEN( "A single-frame Log File missing only its last bytes" )
    {
        const auto frame = zstdFrame( logLines( 1, 5000 ) );

        QTemporaryFile logFile;
        REQUIRE( writeRawFile( logFile, frame.left( frame.size() - 4 ) ) );

        WHEN( "the Log File is decompressed" )
        {
            const auto outcome = decompressWithin( logFile.fileName(), kDecompressTimeout );

            THEN( "the decompression finishes and fails" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE_FALSE( outcome.succeeded );
            }
        }
    }
}

SCENARIO( "A corrupt .zst Log File reports an error", "[zstddevice][decompressor]" )
{
    GIVEN( "A Log File whose valid first frame is followed by garbage" )
    {
        const auto corrupt = zstdFrame( logLines( 1, 1000 ) ) + QByteArray( 64, 'x' );

        QTemporaryFile logFile;
        REQUIRE( writeRawFile( logFile, corrupt ) );

        WHEN( "the Log File is decompressed" )
        {
            const auto outcome = decompressWithin( logFile.fileName(), kDecompressTimeout );

            THEN( "the decompression finishes and fails" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE_FALSE( outcome.succeeded );
            }
        }
    }
}
