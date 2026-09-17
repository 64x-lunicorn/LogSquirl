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

#include "headerandtaildigests.h"

#include <algorithm>
#include <cstddef>
#include <utility>

HeaderAndTailDigests::HeaderAndTailDigests( qint64 blockSize )
    : blockSize_( std::max( blockSize - blockSize % 2, qint64{ 2 } ) )
{
}

RangeDigest HeaderAndTailDigests::tailRange( qint64 blockSize, qint64 logFileSize )
{
    if ( logFileSize < blockSize ) {
        return { 0, logFileSize, 0 };
    }
    const auto halfBlock = blockSize / 2;
    const auto offset = ( logFileSize / halfBlock - 1 ) * halfBlock;
    return { offset, logFileSize - offset, 0 };
}

void HeaderAndTailDigests::reset()
{
    startOver( 0 );
    tailStartsFrom_ = 0;
}

void HeaderAndTailDigests::expectLogFileSize( qint64 size )
{
    tailStartsFrom_ = tailRange( blockSize_, size ).offset;
}

void HeaderAndTailDigests::startOver( qint64 offset )
{
    end_ = offset;
    headerRunning_ = offset == 0;
    header_.reset();
    previousSegment_.offset = -1;
    lastSegment_.offset = -1;
}

void HeaderAndTailDigests::startTailSegment( qint64 offset )
{
    if ( offset < tailStartsFrom_ ) {
        previousSegment_.offset = -1;
        lastSegment_.offset = -1;
        return;
    }

    if ( lastSegment_.offset >= 0 && lastSegment_.offset == offset - halfBlock() ) {
        std::swap( previousSegment_, lastSegment_ );
    }
    else {
        previousSegment_.offset = -1;
    }
    lastSegment_.offset = offset;
    lastSegment_.digest.reset();
}

void HeaderAndTailDigests::add( qint64 offset, const char* data, qint64 size )
{
    if ( size <= 0 ) {
        return;
    }
    if ( offset != end_ ) {
        startOver( offset );
    }

    auto position = offset;
    while ( size > 0 ) {
        const auto intoSegment = position % halfBlock();
        if ( intoSegment == 0 ) {
            startTailSegment( position );
        }

        // Up to the next multiple of half a block, so the header, which
        // ends at one, is never fed past its end.
        const auto chunk = std::min( size, halfBlock() - intoSegment );
        const auto length = static_cast<std::size_t>( chunk );
        if ( headerRunning_ && position < blockSize_ ) {
            header_.addData( data, length );
        }
        if ( lastSegment_.offset >= 0 ) {
            lastSegment_.digest.addData( data, length );
        }
        if ( previousSegment_.offset >= 0 ) {
            previousSegment_.digest.addData( data, length );
        }

        position += chunk;
        data += chunk;
        size -= chunk;
    }
    end_ = position;
}

std::optional<RangeDigest> HeaderAndTailDigests::header( qint64 logFileSize ) const
{
    if ( logFileSize != end_ || !headerRunning_ ) {
        return std::nullopt;
    }
    return RangeDigest{ 0, std::min( logFileSize, blockSize_ ), header_.digest() };
}

std::optional<RangeDigest> HeaderAndTailDigests::tail( qint64 logFileSize ) const
{
    if ( logFileSize != end_ ) {
        return std::nullopt;
    }

    const auto range = tailRange( blockSize_, logFileSize );
    if ( range.offset == 0 ) {
        return header( logFileSize );
    }
    for ( const auto* segment : { &lastSegment_, &previousSegment_ } ) {
        if ( segment->offset == range.offset ) {
            return RangeDigest{ range.offset, range.size, segment->digest.digest() };
        }
    }
    return std::nullopt;
}
