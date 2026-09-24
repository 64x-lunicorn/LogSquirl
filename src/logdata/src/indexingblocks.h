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

#pragma once

// The blocks indexing reads a Log File in, and how each is parsed on its own
// (#290). A private header of the log data library: only logdataworker.cpp
// uses it.
//
// Blocks are parsed in parallel, each without knowing where the Log Line
// running into it starts. Every Log Line ending in a block after its first
// line feed is parsed completely there. The Log Line crossing into the block,
// up to its first line feed, is finished by stitching the blocks together in
// file order.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "encodingdetector.h"
#include "linepositionarray.h"
#include "linetypes.h"

#if defined( __SSE2__ ) || defined( _M_X64 ) || ( defined( _M_IX86_FP ) && _M_IX86_FP >= 2 )
#include <emmintrin.h>
#define LOGSQUIRL_INDEXING_SCAN_SSE2
#elif defined( __aarch64__ ) || defined( _M_ARM64 )
#include <arm_neon.h>
#define LOGSQUIRL_INDEXING_SCAN_NEON
#endif

namespace indexing_blocks {

// A line feed or a tab takes at most this many bytes in any encoding: a
// block keeps as many bytes before and after it, to tell whether a line feed
// or tab byte at its edge is one.
inline constexpr int MaxDelimiterNeighbours = 3;

// Finds the line feed and tab bytes of a range of bytes, both in one pass,
// 16 bytes at a time: with SSE2 or NEON, the baseline of every x86-64 and
// arm64 target, and byte by byte on any other.
class LineFeedAndTabScanner {
public:
    LineFeedAndTabScanner( const char* bytes, std::size_t size, std::size_t from = 0 )
        : bytes_( bytes )
        , size_( size )
        , nextChunk_( from )
    {
    }

    // The offset of the next line feed or tab byte, or the size of the range
    // once there is none left.
    std::size_t next()
    {
        while ( hits_ == 0 ) {
            if ( nextChunk_ >= size_ ) {
                return size_;
            }
            chunk_ = nextChunk_;
            hits_ = hitsInChunk();
        }
        const auto bit = static_cast<std::size_t>( std::countr_zero( hits_ ) );
        hits_ &= hits_ - 1;
        return chunk_ + bit / BitsPerByte;
    }

private:
    static constexpr std::size_t ChunkSize = 16;

#ifdef LOGSQUIRL_INDEXING_SCAN_NEON
    // NEON has no movemask: every byte of a chunk is narrowed to 4 bits.
    static constexpr std::size_t BitsPerByte = 4;
#else
    static constexpr std::size_t BitsPerByte = 1;
#endif

    std::uint64_t hitsInChunk()
    {
        const auto* chunk = bytes_ + chunk_;
        if ( size_ - chunk_ < ChunkSize ) {
            nextChunk_ = size_;
            std::uint64_t hits = 0;
            for ( std::size_t i = 0; i < size_ - chunk_; ++i ) {
                if ( chunk[ i ] == '\n' || chunk[ i ] == '\t' ) {
                    hits |= std::uint64_t{ 1 } << ( i * BitsPerByte );
                }
            }
            return hits;
        }
        nextChunk_ = chunk_ + ChunkSize;

#if defined( LOGSQUIRL_INDEXING_SCAN_SSE2 )
        const auto bytes = _mm_loadu_si128( reinterpret_cast<const __m128i*>( chunk ) );
        const auto matches = _mm_or_si128( _mm_cmpeq_epi8( bytes, _mm_set1_epi8( '\n' ) ),
                                           _mm_cmpeq_epi8( bytes, _mm_set1_epi8( '\t' ) ) );
        return static_cast<std::uint32_t>( _mm_movemask_epi8( matches ) );
#elif defined( LOGSQUIRL_INDEXING_SCAN_NEON )
        const auto bytes = vld1q_u8( reinterpret_cast<const std::uint8_t*>( chunk ) );
        const auto matches = vorrq_u8( vceqq_u8( bytes, vdupq_n_u8( '\n' ) ),
                                       vceqq_u8( bytes, vdupq_n_u8( '\t' ) ) );
        const auto nibbles = vget_lane_u64(
            vreinterpret_u64_u8( vshrn_n_u16( vreinterpretq_u16_u8( matches ), 4 ) ), 0 );
        // One bit of each byte's nibble is enough.
        return nibbles & 0x1111'1111'1111'1111ULL;
#else
        std::uint64_t hits = 0;
        for ( std::size_t i = 0; i < ChunkSize; ++i ) {
            if ( chunk[ i ] == '\n' || chunk[ i ] == '\t' ) {
                hits |= std::uint64_t{ 1 } << i;
            }
        }
        return hits;
#endif
    }

    const char* bytes_;
    std::size_t size_;
    std::size_t nextChunk_;
    std::size_t chunk_ = 0;
    std::uint64_t hits_ = 0;
};

// One block of a Log File as indexing reads it, and what parsing it on its own
// found.
struct IndexingBlock {
    explicit IndexingBlock( std::int64_t capacity );

    // The block's bytes: size of them, from beginning in the Log File on.
    const char* bytes() const
    {
        return buffer.get() + MaxDelimiterNeighbours;
    }
    char* bytes()
    {
        return buffer.get() + MaxDelimiterNeighbours;
    }

    // Tells whether the line feed or tab byte at offset within the block is
    // one in the encoding: its other bytes, before or after it, are zero.
    bool isDelimiter( std::int64_t offset ) const;

    std::int64_t capacity;
    // Not zero-filled: every byte used is read into it first.
    std::unique_ptr<char[]> buffer;

    // In the order the blocks were read, from 0 on.
    std::size_t sequence = 0;
    std::int64_t beginning = 0;
    std::int64_t size = 0;
    // Bytes of the Log File right before and after the block's, kept around
    // them in the buffer, at most MaxDelimiterNeighbours each.
    int bytesBefore = 0;
    int bytesAfter = 0;
    EncodingParameters encoding;

    // Where the Log Lines ending after the first line feed of the block end.
    FastLinePositionArray endOfLines;
    // The offset within the block of its first line feed, if it has one.
    std::optional<std::int64_t> firstLineFeed;
    // The longest Log Line starting and ending within the block.
    std::int64_t maxLength = 0;
    // Where the Log Line running out of the block starts, and how many spaces
    // its tabs within the block widen it by: only known when the block has a
    // line feed.
    std::int64_t lastLineStart = 0;
    std::int64_t lastLineWidening = 0;
};

// Parses a block on its own: every Log Line after the first line feed.
void parseBlock( IndexingBlock& block );

// The Log Line indexing is in the middle of, carried from one block to the
// next in file order.
struct OpenLogLine {
    std::int64_t start = 0;
    std::int64_t widening = 0;
};

// Stitches a block parsed on its own to the ones before it: finishes the Log
// Line crossing into it, which is left open when the block has no line feed.
// Returns the length of that Log Line if it ends in the block, and updates
// line to the one running out of the block.
std::optional<std::int64_t> stitchBlock( const IndexingBlock& block, OpenLogLine& line );

// A lower bound of the length of the Log Line running out of the block,
// exact when the Log File ends there.
std::int64_t openLineLength( const IndexingBlock& block, const OpenLogLine& line );

// The blocks an indexing run reads its Log File in, reused from one block to
// the next rather than allocated for each.
class IndexingBlockPool {
public:
    explicit IndexingBlockPool( std::int64_t blockSize );

    std::int64_t blockSize() const
    {
        return blockSize_;
    }

    // A block no one uses, allocated only when every one allocated is in use.
    IndexingBlock* acquire();
    void release( IndexingBlock* block );

    std::int64_t allocated() const;

private:
    std::int64_t blockSize_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<IndexingBlock>> blocks_;
    std::vector<IndexingBlock*> free_;
};

// Where reading a Log File in blocks has got to.
struct BlockReading {
    // Where the last block read ends in the Log File.
    std::int64_t end = 0;
    std::size_t blocksRead = 0;
    // The last bytes of the blocks read so far, and those read past them.
    std::array<char, MaxDelimiterNeighbours> behind{};
    int bytesBehind = 0;
    std::array<char, MaxDelimiterNeighbours> ahead{};
    int bytesAhead = 0;
};

// How many blocks of the given size a read buffer of readBufferSizeMb MiB
// holds, blocks being read, parsed and not yet stitched: at least one.
std::int64_t blocksInReadBuffer( int readBufferSizeMb, std::int64_t blockSize );

} // namespace indexing_blocks
