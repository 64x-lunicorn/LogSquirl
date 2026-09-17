/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtEndian>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

#include "compressedlinestorage.h"
#include "containers.h"
#include "cpu_info.h"
#include "linetypes.h"
#include "log.h"

#include <streamvbyte.h>
#include <streamvbytedelta.h>

static constexpr size_t SimdIndexBlockSize = 128;

// A block spanning 4 GiB or more holds its offsets unpacked, 8 bytes each.
static constexpr size_t WideOffsetBytes = sizeof( uint64_t );
static constexpr size_t WideBlockBytes = SimdIndexBlockSize * WideOffsetBytes;

// A packed block starts with one control byte per four offsets; each 2-bit
// code in it gives the length of one offset, 1 to 4 bytes.
static constexpr size_t PackedControlBytes = ( SimdIndexBlockSize + 3 ) / 4;

static size_t packedBlockBytes( const uint8_t* block )
{
    size_t bytes = PackedControlBytes;
    for ( size_t i = 0; i < PackedControlBytes; ++i ) {
        for ( unsigned shift = 0; shift < 8; shift += 2 ) {
            bytes += ( ( block[ i ] >> shift ) & 3u ) + 1u;
        }
    }
    return bytes;
}

void CompressedLinePositionStorage::move_from( CompressedLinePositionStorage&& orig ) noexcept
{
    blocks_ = std::move( orig.blocks_ );
    packedLinesStorage_ = std::move( orig.packedLinesStorage_ );
    currentLinesBlock_ = std::move( orig.currentLinesBlock_ );
    // Without it, the next compressed block would be written over the first.
    packedLinesStorageUsedSize_ = orig.packedLinesStorageUsedSize_;

    nbLines_ = orig.nbLines_;
    lastPos_ = orig.lastPos_;
    canUseSimdSelect_ = orig.canUseSimdSelect_;

    orig.packedLinesStorageUsedSize_ = 0;
    orig.nbLines_ = 0_lcount;
    orig.lastPos_ = 0_offset;
}

CompressedLinePositionStorage::CompressedLinePositionStorage()
{
    auto requiredInstructions = CpuInstructions::SSE41;
    canUseSimdSelect_ = hasRequiredInstructions( supportedCpuInstructions(), requiredInstructions );
}

CompressedLinePositionStorage::CompressedLinePositionStorage(
    CompressedLinePositionStorage&& orig ) noexcept
{
    move_from( std::move( orig ) );
}

CompressedLinePositionStorage&
CompressedLinePositionStorage::operator=( CompressedLinePositionStorage&& orig ) noexcept
{
    move_from( std::move( orig ) );
    return *this;
}

void CompressedLinePositionStorage::append( OffsetInFile pos )
{
    // Lines must be stored in order
    assert( ( pos > lastPos_ ) || ( pos == 0_offset ) );

    currentLinesBlock_.push_back( pos );

    if ( currentLinesBlock_.size() == SimdIndexBlockSize ) {
        compress_current_block();
    }

    lastPos_ = pos;
    ++nbLines_;
}

void CompressedLinePositionStorage::compress_current_block()
{
    BlockMetadata& block = blocks_.emplace_back();
    const auto firstPosition = currentLinesBlock_.front().get();
    block.firstLineOffset = currentLinesBlock_.front();
    block.storageOffsetAndWideFlag = packedLinesStorageUsedSize_;

    // Positions only grow, so the last one is the farthest from the first.
    const auto span = static_cast<uint64_t>( currentLinesBlock_.back().get() - firstPosition );
    if ( span > std::numeric_limits<uint32_t>::max() ) {
        block.storageOffsetAndWideFlag |= BlockMetadata::WideFlag;
        packedLinesStorage_.resize( packedLinesStorageUsedSize_ + WideBlockBytes );
        auto* out = packedLinesStorage_.data() + packedLinesStorageUsedSize_;
        for ( const auto position : currentLinesBlock_ ) {
            qToLittleEndian( static_cast<uint64_t>( position.get() - firstPosition ), out );
            out += WideOffsetBytes;
        }
        packedLinesStorageUsedSize_ += WideBlockBytes;
    }
    else {
        std::array<uint32_t, SimdIndexBlockSize> shifted;
        std::transform( currentLinesBlock_.begin(), currentLinesBlock_.end(), shifted.begin(),
                        [ firstPosition ]( OffsetInFile pos ) {
                            return static_cast<uint32_t>( pos.get() - firstPosition );
                        } );

        const size_t packedLinesSize = streamvbyte_max_compressedbytes( SimdIndexBlockSize );
        packedLinesStorage_.resize( packedLinesStorageUsedSize_ + packedLinesSize );
        packedLinesStorageUsedSize_ += streamvbyte_delta_encode(
            shifted.data(), SimdIndexBlockSize,
            packedLinesStorage_.data() + block.packetStorageOffset(), 0 );
    }

    currentLinesBlock_.clear();
}

OffsetInFile CompressedLinePositionStorage::position_in_block( const BlockMetadata& block,
                                                               size_t indexInBlock ) const
{
    if ( block.hasWideOffsets() ) {
        const auto offset = qFromLittleEndian<uint64_t>(
            &packedLinesStorage_[ block.packetStorageOffset() + indexInBlock * WideOffsetBytes ] );
        return block.firstLineOffset + OffsetInFile( static_cast<int64_t>( offset ) );
    }

    std::array<uint32_t, SimdIndexBlockSize> unpackedBlock;
    streamvbyte_delta_decode( &packedLinesStorage_[ block.packetStorageOffset() ],
                              unpackedBlock.data(), SimdIndexBlockSize, 0 );
    return block.firstLineOffset + OffsetInFile( unpackedBlock[ indexInBlock ] );
}

void CompressedLinePositionStorage::unpack_block( const BlockMetadata& block, size_t first,
                                                  size_t last, OffsetInFile* out ) const
{
    if ( block.hasWideOffsets() ) {
        for ( auto index = first; index < last; ++index ) {
            *out++ = position_in_block( block, index );
        }
        return;
    }

    std::array<uint32_t, SimdIndexBlockSize> unpackedBlock;
    streamvbyte_delta_decode( &packedLinesStorage_[ block.packetStorageOffset() ],
                              unpackedBlock.data(), SimdIndexBlockSize, 0 );
    std::transform(
        unpackedBlock.begin() + static_cast<std::ptrdiff_t>( first ),
        unpackedBlock.begin() + static_cast<std::ptrdiff_t>( last ), out,
        [ &block ]( uint32_t pos ) { return OffsetInFile( pos ) + block.firstLineOffset; } );
}

OffsetInFile CompressedLinePositionStorage::at( LineNumber index ) const
{
    if ( index >= nbLines_ ) {
        LOG_ERROR << "Line number not in storage: " << index.get() << ", storage size is "
                  << nbLines_;
        throw std::runtime_error( "Line number not in storage" );
    }

    const size_t blockIndex = index.get() / SimdIndexBlockSize;
    const size_t indexInBlock = index.get() % SimdIndexBlockSize;

    if ( blockIndex == blocks_.size() ) {
        return currentLinesBlock_[ indexInBlock ];
    }

    return position_in_block( blocks_[ blockIndex ], indexInBlock );
}

void CompressedLinePositionStorage::append_list( const logsquirl::vector<OffsetInFile>& positions )
{
    if ( positions.empty() ) {
        return;
    }
    // Lines must be stored in order
    assert( ( positions.front() > lastPos_ ) || ( positions.front() == 0_offset ) );

    // A whole block of lines is copied and packed at once, rather than
    // appended line by line (#290).
    auto next = positions.begin();
    while ( next != positions.end() ) {
        const auto count = std::min( SimdIndexBlockSize - currentLinesBlock_.size(),
                                     static_cast<size_t>( positions.end() - next ) );
        const auto blockEnd = next + static_cast<std::ptrdiff_t>( count );

        currentLinesBlock_.insert( currentLinesBlock_.end(), next, blockEnd );
        next = blockEnd;

        if ( currentLinesBlock_.size() == SimdIndexBlockSize ) {
            compress_current_block();
        }
    }

    lastPos_ = positions.back();
    nbLines_ += LinesCount( positions.size() );
}

void CompressedLinePositionStorage::uncompress_last_block()
{
    currentLinesBlock_.resize( SimdIndexBlockSize );
    const BlockMetadata& block = blocks_.back();
    unpack_block( block, 0, SimdIndexBlockSize, currentLinesBlock_.data() );
    blocks_.pop_back();
}

void CompressedLinePositionStorage::pop_back()
{
    if ( currentLinesBlock_.empty() && !blocks_.empty() ) {
        // Last entry caused block compression, so we need to uncompress it
        // to de-alloc last entry.
        uncompress_last_block();
    }

    if ( !currentLinesBlock_.empty() ) {
        currentLinesBlock_.pop_back();
    }

    if ( nbLines_.get() == 0 ) {
        lastPos_ = 0_offset;
    }
    else {
        --nbLines_;
        lastPos_ = nbLines_.get() > 0 ? at( nbLines_.get() - 1 ) : 0_offset;
    }
}

size_t CompressedLinePositionStorage::allocatedSize() const
{
    return packedLinesStorage_.size() + blocks_.size() * sizeof( BlockMetadata );
}

logsquirl::vector<OffsetInFile> CompressedLinePositionStorage::range( LineNumber firstLine,
                                                                      LinesCount count ) const
{
    const size_t firstBlockIndex = firstLine.get() / SimdIndexBlockSize;
    const size_t indexInFirstBlock = firstLine.get() % SimdIndexBlockSize;

    const LineNumber lastLine = firstLine + count - 1_lcount;
    const size_t lastBlockIndex = lastLine.get() / SimdIndexBlockSize;
    const size_t indexInLastBlock = lastLine.get() % SimdIndexBlockSize;

    logsquirl::vector<OffsetInFile> result;
    result.reserve( count.get() );

    if ( firstBlockIndex == blocks_.size() ) {
        std::copy( currentLinesBlock_.begin() + static_cast<int64_t>( indexInFirstBlock ),
                   currentLinesBlock_.begin() + static_cast<int64_t>( indexInLastBlock + 1 ),
                   std::back_inserter( result ) );
    }
    else {
        size_t lastBlockToUnpack = std::min( lastBlockIndex, blocks_.size() - 1 );
        for ( size_t blockIndex = firstBlockIndex; blockIndex <= lastBlockToUnpack; ++blockIndex ) {
            const size_t copyFromIndex = blockIndex == firstBlockIndex ? indexInFirstBlock : 0u;
            const size_t copyToIndex
                = blockIndex == lastBlockIndex ? indexInLastBlock + 1 : SimdIndexBlockSize;

            const auto resultSize = result.size();
            result.resize( resultSize + copyToIndex - copyFromIndex );
            unpack_block( blocks_[ blockIndex ], copyFromIndex, copyToIndex,
                          result.data() + resultSize );
        }

        if ( lastBlockIndex == blocks_.size() ) {
            std::copy( currentLinesBlock_.begin(),
                       currentLinesBlock_.begin() + static_cast<int64_t>( indexInLastBlock + 1 ),
                       std::back_inserter( result ) );
        }
    }

    return result;
}

void CompressedLinePositionStorage::serialize( QDataStream& out ) const
{
    // Number of compressed blocks
    out << static_cast<quint32>( blocks_.size() );
    for ( const auto& block : blocks_ ) {
        out << static_cast<qint64>( block.firstLineOffset.get() );
        // A block spanning 4 GiB or more is flagged in the top bit (#321).
        out << static_cast<quint64>( block.storageOffsetAndWideFlag );
    }

    // Packed byte storage
    out << static_cast<quint64>( packedLinesStorageUsedSize_ );
    // QDataStream::writeRawData → memcpy.  Calling memcpy with a null pointer
    // is undefined behaviour even when the length is zero, and ASAN's wrapper
    // crashes on it.  Skip the call when there is nothing to write.
    if ( packedLinesStorageUsedSize_ > 0 && packedLinesStorage_.data() != nullptr ) {
        out.writeRawData( reinterpret_cast<const char*>( packedLinesStorage_.data() ),
                          static_cast<int>( packedLinesStorageUsedSize_ ) );
    }

    // Uncompressed tail block (< 128 lines)
    out << static_cast<quint32>( currentLinesBlock_.size() );
    for ( const auto& offset : currentLinesBlock_ ) {
        out << static_cast<qint64>( offset.get() );
    }

    // Scalar state
    out << static_cast<qint64>( nbLines_.get() );
    out << static_cast<qint64>( lastPos_.get() );
}

bool CompressedLinePositionStorage::deserialize( QDataStream& in )
{
    quint32 blockCount = 0;
    in >> blockCount;
    if ( in.status() != QDataStream::Ok || blockCount > 100'000'000 ) {
        return false;
    }

    blocks_.clear();
    blocks_.reserve( blockCount );
    for ( quint32 i = 0; i < blockCount; ++i ) {
        qint64 firstOffset = 0;
        quint64 storageOffset = 0;
        in >> firstOffset >> storageOffset;
        if ( in.status() != QDataStream::Ok ) {
            return false;
        }
        auto& block = blocks_.emplace_back();
        block.firstLineOffset = OffsetInFile( firstOffset );
        block.storageOffsetAndWideFlag = storageOffset;
    }

    // Packed byte storage
    quint64 packedSize = 0;
    in >> packedSize;
    if ( in.status() != QDataStream::Ok || packedSize > 2'000'000'000ULL ) {
        return false;
    }
    // streamvbyte's decoder reads up to STREAMVBYTE_PADDING bytes past a block.
    packedLinesStorage_.resize( static_cast<size_t>( packedSize ) + STREAMVBYTE_PADDING );
    packedLinesStorageUsedSize_ = static_cast<size_t>( packedSize );
    if ( packedSize > 0 ) {
        if ( in.readRawData( reinterpret_cast<char*>( packedLinesStorage_.data() ),
                             static_cast<int>( packedSize ) )
             != static_cast<int>( packedSize ) ) {
            return false;
        }
    }
    // Every byte of a block must lie within the packed bytes.
    const bool blocksInPackedBytes = std::all_of(
        blocks_.begin(), blocks_.end(), [ this, packedSize ]( const BlockMetadata& block ) {
            const uint64_t offset = block.packetStorageOffset();
            if ( offset > packedSize ) {
                return false;
            }
            const uint64_t available = packedSize - offset;
            if ( block.hasWideOffsets() ) {
                return WideBlockBytes <= available;
            }
            return PackedControlBytes <= available
                   && packedBlockBytes( &packedLinesStorage_[ static_cast<size_t>( offset ) ] )
                          <= available;
        } );
    if ( !blocksInPackedBytes ) {
        return false;
    }

    // Uncompressed tail block
    quint32 tailCount = 0;
    in >> tailCount;
    // A full tail is always packed into a block at once.
    if ( in.status() != QDataStream::Ok || tailCount >= SimdIndexBlockSize ) {
        return false;
    }
    currentLinesBlock_.clear();
    currentLinesBlock_.reserve( tailCount );
    for ( quint32 i = 0; i < tailCount; ++i ) {
        qint64 val = 0;
        in >> val;
        if ( in.status() != QDataStream::Ok ) {
            return false;
        }
        currentLinesBlock_.push_back( OffsetInFile( val ) );
    }

    // Scalar state
    qint64 lines = 0;
    qint64 lastP = 0;
    in >> lines >> lastP;
    if ( in.status() != QDataStream::Ok
         || static_cast<quint64>( lines ) != blocks_.size() * SimdIndexBlockSize + tailCount ) {
        return false;
    }
    nbLines_ = LinesCount( static_cast<LinesCount::UnderlyingType>( lines ) );
    lastPos_ = OffsetInFile( lastP );

    // Detect SIMD capability
    canUseSimdSelect_
        = hasRequiredInstructions( supportedCpuInstructions(), CpuInstructions::SSE41 );

    return true;
}
