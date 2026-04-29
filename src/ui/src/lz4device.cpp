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
    close();
}

bool Lz4Device::open( OpenMode mode )
{
    if ( !( mode & ReadOnly ) || ( mode & WriteOnly ) ) {
        return false;
    }

    if ( !file_.open( QIODevice::ReadOnly ) ) {
        return false;
    }

    const auto err = LZ4F_createDecompressionContext( &dctx_, LZ4F_VERSION );
    if ( LZ4F_isError( err ) ) {
        file_.close();
        return false;
    }

    inPos_ = 0;
    inSize_ = 0;
    finished_ = false;

    return QIODevice::open( mode );
}

void Lz4Device::close()
{
    if ( dctx_ ) {
        LZ4F_freeDecompressionContext( dctx_ );
        dctx_ = nullptr;
    }
    file_.close();
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
    return finished_ && QIODevice::atEnd();
}

qint64 Lz4Device::bytesAvailable() const
{
    if ( finished_ ) {
        return QIODevice::bytesAvailable();
    }
    return QIODevice::bytesAvailable() + ( 256 * 1024 );
}

qint64 Lz4Device::readData( char* data, qint64 maxSize )
{
    if ( !dctx_ || maxSize <= 0 ) {
        return 0;
    }

    std::size_t totalOut = 0;

    while ( totalOut < static_cast<std::size_t>( maxSize ) ) {
        // Refill input buffer if exhausted
        if ( inPos_ >= inSize_ ) {
            const auto bytesRead = file_.read( inBuf_.data(),
                                               static_cast<qint64>( inBuf_.size() ) );
            if ( bytesRead < 0 ) {
                return -1; // I/O error
            }
            if ( bytesRead == 0 ) {
                finished_ = true;
                break;
            }
            inPos_ = 0;
            inSize_ = static_cast<std::size_t>( bytesRead );
        }

        auto srcSize = inSize_ - inPos_;
        auto dstSize = static_cast<std::size_t>( maxSize ) - totalOut;

        const auto hint = LZ4F_decompress(
            dctx_,
            data + totalOut, &dstSize,
            inBuf_.data() + inPos_, &srcSize,
            nullptr );

        inPos_ += srcSize;
        totalOut += dstSize;

        if ( LZ4F_isError( hint ) ) {
            finished_ = true;
            return totalOut > 0 ? static_cast<qint64>( totalOut ) : -1;
        }

        if ( hint == 0 ) {
            // Frame complete
            finished_ = true;
            break;
        }
    }

    return static_cast<qint64>( totalOut );
}

qint64 Lz4Device::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1; // read-only device
}
