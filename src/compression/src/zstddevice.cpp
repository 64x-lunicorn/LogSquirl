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

#include "zstddevice.h"

#include <zstd.h>

/// Default input buffer size — 256 KiB matches ZSTD_DStreamInSize().
static constexpr std::size_t kInputBufSize = 256 * 1024;

ZstdDevice::ZstdDevice( const QString& filePath, QObject* parent )
    : QIODevice( parent )
    , filePath_( filePath )
    , file_( filePath )
    , inBuf_( kInputBufSize )
{
}

ZstdDevice::~ZstdDevice()
{
    // Qualified: the destructor runs this class's close(), not a subclass's.
    ZstdDevice::close();
}

bool ZstdDevice::open( OpenMode mode )
{
    if ( !( mode & ReadOnly ) || ( mode & WriteOnly ) ) {
        return false;
    }

    if ( !file_.open( QIODevice::ReadOnly ) ) {
        return false;
    }

    if ( dctx_ ) {
        ZSTD_freeDCtx( dctx_ );
    }
    dctx_ = ZSTD_createDCtx();
    if ( !dctx_ ) {
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

void ZstdDevice::close()
{
    if ( dctx_ ) {
        ZSTD_freeDCtx( dctx_ );
        dctx_ = nullptr;
    }
    file_.close();
    fileExhausted_ = true;
    finished_ = true;
    QIODevice::close();
}

bool ZstdDevice::isSequential() const
{
    return true;
}

bool ZstdDevice::atEnd() const
{
    if ( !isOpen() ) {
        return true;
    }
    // Even after the ZSTD stream is fully decoded, QIODevice may still hold
    // decompressed data in its internal read buffer. A failed stream is not at
    // its end: the next read reports the failure.
    return finished_ && !failed_ && QIODevice::atEnd();
}

qint64 ZstdDevice::bytesAvailable() const
{
    // QIODevice::read(qint64) for sequential devices limits the read to
    // bytesAvailable(). Return the base-class value plus a large hint so
    // callers can request full-sized reads until the stream is done.
    if ( finished_ ) {
        return QIODevice::bytesAvailable();
    }
    return QIODevice::bytesAvailable() + ( 256 * 1024 );
}

qint64 ZstdDevice::readData( char* data, qint64 maxSize )
{
    if ( failed_ ) {
        return -1;
    }
    if ( !dctx_ || maxSize <= 0 || finished_ ) {
        return 0;
    }

    ZSTD_outBuffer output{ data, static_cast<std::size_t>( maxSize ), 0 };

    while ( output.pos < output.size ) {
        if ( inPos_ >= inSize_ && !fileExhausted_ ) {
            const auto bytesRead
                = file_.read( inBuf_.data(), static_cast<qint64>( inBuf_.size() ) );
            if ( bytesRead < 0 ) {
                return fail( file_.errorString(), output.pos );
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

        ZSTD_inBuffer input{ inBuf_.data(), inSize_, inPos_ };
        const auto prevOutPos = output.pos;
        const auto rc = ZSTD_decompressStream( dctx_, &output, &input );
        inPos_ = input.pos;

        if ( ZSTD_isError( rc ) ) {
            return fail( QString::fromLatin1( ZSTD_getErrorName( rc ) ), output.pos );
        }

        // rc == 0 ends a frame. The context then starts the next frame on its
        // own, so decoding simply goes on while input is left.
        frameInProgress_ = rc != 0;

        if ( inputExhausted && frameInProgress_ && output.pos == prevOutPos ) {
            return fail( QStringLiteral( "truncated zstd frame" ), output.pos );
        }
    }

    return static_cast<qint64>( output.pos );
}

qint64 ZstdDevice::fail( const QString& reason, std::size_t decompressedBytes )
{
    ZSTD_freeDCtx( dctx_ );
    dctx_ = nullptr;
    finished_ = true;
    failed_ = true;
    setErrorString( QStringLiteral( "Cannot decompress %1: %2" ).arg( filePath_, reason ) );

    // Hand out what was decoded before the error; the next read reports it.
    return decompressedBytes > 0 ? static_cast<qint64>( decompressedBytes ) : -1;
}

qint64 ZstdDevice::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1; // read-only device
}
