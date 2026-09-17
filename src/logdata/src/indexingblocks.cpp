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

#include "indexingblocks.h"

#include <algorithm>

namespace indexing_blocks {

namespace {

// How many spaces a tab starting its character at characterStart widens a
// Log Line starting at lineStart by, the line being widened by widening
// already: up to the next tab stop.
std::int64_t tabWidening( std::int64_t characterStart, std::int64_t lineStart,
                          std::int64_t widening, const EncodingParameters& encoding )
{
    const auto column = ( characterStart - lineStart ) / encoding.lineFeedWidth + widening;
    return TabStop - column % TabStop - 1;
}

// The length of a Log Line starting at lineStart and ending where the
// character at characterEnd starts, widened by widening.
std::int64_t lineLength( std::int64_t characterEnd, std::int64_t lineStart, std::int64_t widening,
                         const EncodingParameters& encoding )
{
    return ( characterEnd - lineStart ) / encoding.lineFeedWidth + widening;
}

} // namespace

IndexingBlock::IndexingBlock( std::int64_t blockCapacity )
    : capacity( blockCapacity )
    , buffer( std::make_unique_for_overwrite<char[]>(
          static_cast<std::size_t>( blockCapacity + 2 * MaxDelimiterNeighbours ) ) )
{
}

bool IndexingBlock::isDelimiter( std::int64_t offset ) const
{
    const auto width = encoding.lineFeedWidth;
    if ( width == 1 ) {
        return true;
    }
    const auto forward = encoding.lineFeedIndex == 0;
    if ( forward ? offset + width - 1 >= size + bytesAfter
                 : offset - ( width - 1 ) < -bytesBefore ) {
        return false;
    }
    const auto* at = bytes() + offset;
    for ( int i = 1; i < width; ++i ) {
        if ( ( forward ? at[ i ] : at[ -i ] ) != '\0' ) {
            return false;
        }
    }
    return true;
}

void parseBlock( IndexingBlock& block )
{
    block.endOfLines = FastLinePositionArray{};
    block.firstLineFeed.reset();
    block.maxLength = 0;
    block.lastLineStart = 0;
    block.lastLineWidening = 0;

    const auto& encoding = block.encoding;
    const auto beforeCr = encoding.getBeforeCrOffset();

    LineFeedAndTabScanner scanner( block.bytes(), static_cast<std::size_t>( block.size ) );
    std::int64_t lineStart = 0;
    std::int64_t widening = 0;
    for ( auto hit = static_cast<std::int64_t>( scanner.next() ); hit < block.size;
          hit = static_cast<std::int64_t>( scanner.next() ) ) {
        const auto isLineFeed = block.bytes()[ hit ] == '\n';
        // Tabs before the first line feed belong to the Log Line crossing into
        // the block, which only stitching knows the start of.
        if ( ( !isLineFeed && !block.firstLineFeed ) || !block.isDelimiter( hit ) ) {
            continue;
        }

        const auto characterStart = block.beginning + hit - beforeCr;
        if ( !isLineFeed ) {
            widening += tabWidening( characterStart, lineStart, widening, encoding );
            continue;
        }

        if ( block.firstLineFeed ) {
            block.maxLength = std::max(
                block.maxLength, lineLength( characterStart, lineStart, widening, encoding ) );
        }
        else {
            block.firstLineFeed = hit;
        }
        lineStart = characterStart + encoding.lineFeedWidth;
        widening = 0;
        block.endOfLines.append( OffsetInFile( lineStart ) );
    }

    block.lastLineStart = lineStart;
    block.lastLineWidening = widening;
}

std::optional<std::int64_t> stitchBlock( const IndexingBlock& block, OpenLogLine& line )
{
    const auto& encoding = block.encoding;
    const auto beforeCr = encoding.getBeforeCrOffset();

    // The bytes of the block before its first line feed, or all of them; a
    // Log Line starting after the block's beginning only does so by the zero
    // bytes of a line feed.
    const auto from = std::clamp<std::int64_t>( line.start - block.beginning, 0, block.size );
    const auto to = block.firstLineFeed.value_or( block.size );

    LineFeedAndTabScanner scanner( block.bytes(), static_cast<std::size_t>( to ),
                                   static_cast<std::size_t>( from ) );
    for ( auto hit = static_cast<std::int64_t>( scanner.next() ); hit < to;
          hit = static_cast<std::int64_t>( scanner.next() ) ) {
        if ( block.bytes()[ hit ] == '\t' && block.isDelimiter( hit ) ) {
            line.widening += tabWidening( block.beginning + hit - beforeCr, line.start,
                                          line.widening, encoding );
        }
    }

    if ( !block.firstLineFeed ) {
        return std::nullopt;
    }

    const auto length = lineLength( block.beginning + *block.firstLineFeed - beforeCr, line.start,
                                    line.widening, encoding );
    line.start = block.lastLineStart;
    line.widening = block.lastLineWidening;
    return length;
}

std::int64_t openLineLength( const IndexingBlock& block, const OpenLogLine& line )
{
    return std::max<std::int64_t>(
        0, lineLength( block.beginning + block.size - block.encoding.getBeforeCrOffset(),
                       line.start, line.widening, block.encoding ) );
}

IndexingBlockPool::IndexingBlockPool( std::int64_t blockSize )
    : blockSize_( blockSize )
{
}

IndexingBlock* IndexingBlockPool::acquire()
{
    std::scoped_lock lock( mutex_ );
    if ( !free_.empty() ) {
        auto* block = free_.back();
        free_.pop_back();
        return block;
    }
    return blocks_.emplace_back( std::make_unique<IndexingBlock>( blockSize_ ) ).get();
}

void IndexingBlockPool::release( IndexingBlock* block )
{
    std::scoped_lock lock( mutex_ );
    free_.push_back( block );
}

std::int64_t IndexingBlockPool::allocated() const
{
    std::scoped_lock lock( mutex_ );
    return static_cast<std::int64_t>( blocks_.size() );
}

std::int64_t blocksInReadBuffer( int readBufferSizeMb, std::int64_t blockSize )
{
    const auto readBufferBytes = std::int64_t{ readBufferSizeMb } * 1024 * 1024;
    return std::max<std::int64_t>( 1,
                                   readBufferBytes / ( blockSize + 2 * MaxDelimiterNeighbours ) );
}

} // namespace indexing_blocks
