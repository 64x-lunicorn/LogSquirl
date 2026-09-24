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

#include "lz4device.h"

#include <lz4frame.h>

/// Default input buffer size — 256 KiB.
static constexpr std::size_t kInputBufSize = 256 * 1024;

Lz4Device::Lz4Device( const QString& filePath, QObject* parent )
    : QIODevice( parent )
    , filePath_( filePath )
    , file_( filePath )
    , inBuf_( kInputBufSize )
{
}

Lz4Device::~Lz4Device()
{
    // Qualified: the destructor runs this class's close(), not a subclass's.
    Lz4Device::close();
}

bool Lz4Device::open( OpenMode mode )
{
    if ( !( mode & ReadOnly ) || ( mode & WriteOnly ) ) {
        return false;
    }

    if ( !file_.open( QIODevice::ReadOnly ) ) {
        return false;
    }

    if ( dctx_ ) {
        LZ4F_freeDecompressionContext( dctx_ );
        dctx_ = nullptr;
    }
    const auto err = LZ4F_createDecompressionContext( &dctx_, LZ4F_VERSION );
    if ( LZ4F_isError( err ) ) {
        dctx_ = nullptr;
        file_.close();
        return false;
    }

    inPos_ = 0;
    inSize_ = 0;
    fileExhausted_ = false;
    frameInProgress_ = false;
    finished_ = false;
    failed_ = false;

    return QIODevice::open( mode );
}

void Lz4Device::close()
{
    if ( dctx_ ) {
        LZ4F_freeDecompressionContext( dctx_ );
        dctx_ = nullptr;
    }
    file_.close();
    fileExhausted_ = true;
    finished_ = true;
    QIODevice::close();
}

bool Lz4Device::isSequential() const
{
    return true;
}

bool Lz4Device::atEnd() const
{
    if ( !isOpen() ) {
        return true;
    }
    // Even after the LZ4 stream is fully decoded, QIODevice may still hold
    // decompressed data in its internal read buffer. A failed stream is not at
    // its end: the next read reports the failure.
    return finished_ && !failed_ && QIODevice::atEnd();
}

qint64 Lz4Device::bytesAvailable() const
{
    // QIODevice::read(qint64) for sequential devices limits the read to
    // bytesAvailable(). Return the base-class value plus a large hint so
    // callers can request full-sized reads until the stream is done.
    if ( finished_ ) {
        return QIODevice::bytesAvailable();
    }
    return QIODevice::bytesAvailable() + ( 256 * 1024 );
}

qint64 Lz4Device::readData( char* data, qint64 maxSize )
{
    if ( failed_ ) {
        return -1;
    }
    if ( !dctx_ || maxSize <= 0 || finished_ ) {
        return 0;
    }

    std::size_t totalOut = 0;

    while ( totalOut < static_cast<std::size_t>( maxSize ) ) {
        if ( inPos_ >= inSize_ && !fileExhausted_ ) {
            const auto bytesRead
                = file_.read( inBuf_.data(), static_cast<qint64>( inBuf_.size() ) );
            if ( bytesRead < 0 ) {
                return fail( file_.errorString(), totalOut );
            }
            inPos_ = 0;
            inSize_ = static_cast<std::size_t>( bytesRead );
            fileExhausted_ = bytesRead == 0;
        }

        const auto inputExhausted = fileExhausted_ && inPos_ >= inSize_;
        if ( inputExhausted && !frameInProgress_ ) {
            // The last frame is decoded and flushed; any frame before it was
            // followed by another one, so the whole file has been read.
            finished_ = true;
            break;
        }

        auto srcSize = inSize_ - inPos_;
        auto dstSize = static_cast<std::size_t>( maxSize ) - totalOut;
        const auto prevTotalOut = totalOut;

        const auto hint = LZ4F_decompress( dctx_, data + totalOut, &dstSize, inBuf_.data() + inPos_,
                                           &srcSize, nullptr );

        inPos_ += srcSize;
        totalOut += dstSize;

        if ( LZ4F_isError( hint ) ) {
            return fail( QString::fromLatin1( LZ4F_getErrorName( hint ) ), totalOut );
        }

        // hint == 0 ends a frame. liblz4 resets the context itself, so decoding
        // simply goes on with the next frame while input is left.
        frameInProgress_ = hint != 0;

        if ( inputExhausted && frameInProgress_ && totalOut == prevTotalOut ) {
            return fail( QStringLiteral( "truncated lz4 frame" ), totalOut );
        }
    }

    return static_cast<qint64>( totalOut );
}

qint64 Lz4Device::fail( const QString& reason, std::size_t decompressedBytes )
{
    LZ4F_freeDecompressionContext( dctx_ );
    dctx_ = nullptr;
    finished_ = true;
    failed_ = true;
    setErrorString( QStringLiteral( "Cannot decompress %1: %2" ).arg( filePath_, reason ) );

    // Hand out what was decoded before the error; the next read reports it.
    return decompressedBytes > 0 ? static_cast<qint64>( decompressedBytes ) : -1;
}

qint64 Lz4Device::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1; // read-only device
}
