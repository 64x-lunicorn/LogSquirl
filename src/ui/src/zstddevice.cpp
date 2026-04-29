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
    close();
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
    finished_ = false;

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
    // decompressed data in its internal read buffer.
    return finished_ && QIODevice::atEnd();
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
    if ( !dctx_ || maxSize <= 0 || finished_ ) {
        return 0;
    }

    ZSTD_outBuffer output{ data, static_cast<std::size_t>( maxSize ), 0 };

    while ( output.pos < output.size ) {
        // Refill input buffer if exhausted and file still has data
        if ( inPos_ >= inSize_ ) {
            if ( fileExhausted_ ) {
                // Provide genuinely empty input for ZSTD to flush internals
                inPos_ = 0;
                inSize_ = 0;
            }
            else {
                const auto bytesRead = file_.read( inBuf_.data(),
                                                   static_cast<qint64>( inBuf_.size() ) );
                if ( bytesRead < 0 ) {
                    return -1; // I/O error
                }
                if ( bytesRead == 0 ) {
                    fileExhausted_ = true;
                    inPos_ = 0;
                    inSize_ = 0;
                }
                else {
                    inPos_ = 0;
                    inSize_ = static_cast<std::size_t>( bytesRead );
                }
            }
        }

        ZSTD_inBuffer input{ inBuf_.data(), inSize_, inPos_ };
        const auto prevOutPos = output.pos;
        const auto rc = ZSTD_decompressStream( dctx_, &output, &input );
        inPos_ = input.pos;

        if ( ZSTD_isError( rc ) ) {
            ZSTD_freeDCtx( dctx_ );
            dctx_ = nullptr;
            finished_ = true;
            return output.pos > 0 ? static_cast<qint64>( output.pos ) : -1;
        }

        if ( rc == 0 ) {
            // Frame fully decoded
            finished_ = true;
            break;
        }

        // If file is exhausted and ZSTD made no progress, stream is done
        if ( fileExhausted_ && output.pos == prevOutPos ) {
            finished_ = true;
            break;
        }
    }

    return static_cast<qint64>( output.pos );
}

qint64 ZstdDevice::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1; // read-only device
}
