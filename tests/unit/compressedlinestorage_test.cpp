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

// The compressed storage keeps end-of-line positions in packed blocks and reads
// them back from a disk cache, which anybody can edit (#444).

#include <catch2/catch_test_macros.hpp>

#include "compressedlinestorage.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

// The storage packs blocks of this many lines (SimdIndexBlockSize).
constexpr size_t BlockLines = 128;

/// Increasing end-of-line positions with gaps of every size from 1 byte up.
std::vector<OffsetInFile> makeOffsets( size_t count )
{
    std::vector<OffsetInFile> offsets;
    int64_t position = 0;
    for ( size_t i = 0; i < count; ++i ) {
        position += 1 + static_cast<int64_t>( ( i * 7919 ) % 300 );
        offsets.emplace_back( position );
    }
    return offsets;
}

void fill( CompressedLinePositionStorage& storage, const std::vector<OffsetInFile>& offsets )
{
    for ( const auto& offset : offsets ) {
        storage.append( offset );
    }
}

QByteArray serialized( const CompressedLinePositionStorage& storage )
{
    QByteArray bytes;
    QDataStream out( &bytes, QIODevice::WriteOnly );
    storage.serialize( out );
    return bytes;
}

bool deserializes( const QByteArray& bytes )
{
    CompressedLinePositionStorage storage;
    QDataStream in( bytes );
    return storage.deserialize( in );
}

void putBigEndian32( QByteArray& bytes, int position, quint32 value )
{
    for ( int i = 0; i < 4; ++i ) {
        bytes[ position + i ] = static_cast<char>( ( value >> ( 8 * ( 3 - i ) ) ) & 0xff );
    }
}

} // namespace

SCENARIO( "Positions come back as they were appended", "[compressedlinestorage]" )
{
    GIVEN( "An empty storage" )
    {
        CompressedLinePositionStorage storage;

        THEN( "It has no lines" )
        {
            REQUIRE( storage.size().get() == 0 );
        }
    }

    GIVEN( "Lines that fill several blocks and leave a partial one" )
    {
        const auto offsets = makeOffsets( BlockLines * 3 + 41 );
        CompressedLinePositionStorage storage;
        fill( storage, offsets );

        THEN( "Every line reads back, in packed blocks and in the tail" )
        {
            REQUIRE( storage.size().get() == offsets.size() );
            for ( size_t i = 0; i < offsets.size(); ++i ) {
                REQUIRE( storage.at( i ) == offsets[ i ] );
            }
        }

        THEN( "A range reads back across a block boundary" )
        {
            const auto range = storage.range( LineNumber( BlockLines - 3 ), LinesCount( 10 ) );
            REQUIRE( range.size() == 10 );
            for ( size_t i = 0; i < range.size(); ++i ) {
                REQUIRE( range[ i ] == offsets[ BlockLines - 3 + i ] );
            }
        }

        THEN( "The packed positions take less memory than plain 64-bit ones" )
        {
            REQUIRE( storage.allocatedSize() < offsets.size() * sizeof( int64_t ) );
        }

        WHEN( "Lines are popped down into the packed blocks and appended again" )
        {
            auto expected = offsets;
            for ( size_t i = 0; i < 60; ++i ) {
                storage.pop_back();
                expected.pop_back();
            }
            const auto more = makeOffsets( 200 );
            const auto base = expected.back().get();
            for ( const auto& offset : more ) {
                expected.emplace_back( base + offset.get() );
                storage.append( expected.back() );
            }

            THEN( "Every line reads back as it would have been appended" )
            {
                REQUIRE( storage.size().get() == expected.size() );
                for ( size_t i = 0; i < expected.size(); ++i ) {
                    REQUIRE( storage.at( i ) == expected[ i ] );
                }
            }
        }
    }

    GIVEN( "Two lists appended one after the other" )
    {
        const auto offsets = makeOffsets( 500 );
        CompressedLinePositionStorage storage;
        fill( storage, { offsets.begin(), offsets.begin() + 200 } );

        WHEN( "The rest is added as a list" )
        {
            storage.append_list( { offsets.begin() + 200, offsets.end() } );

            THEN( "Every line reads back" )
            {
                REQUIRE( storage.size().get() == offsets.size() );
                for ( size_t i = 0; i < offsets.size(); ++i ) {
                    REQUIRE( storage.at( i ) == offsets[ i ] );
                }
            }
        }
    }

    GIVEN( "A line further than 4 GiB from the first of its block" )
    {
        constexpr int64_t Gib = int64_t{ 1 } << 30;
        std::vector<OffsetInFile> offsets{ OffsetInFile( 100 ), OffsetInFile( 200 ),
                                           OffsetInFile( 200 + 5 * Gib ),
                                           OffsetInFile( 300 + 5 * Gib ) };
        // Complete the block so that it is packed with the wide offsets.
        while ( offsets.size() < BlockLines + 2 ) {
            offsets.emplace_back( offsets.back().get() + 10 );
        }

        CompressedLinePositionStorage storage;
        fill( storage, offsets );

        THEN( "The positions past 32 bits read back exactly" )
        {
            for ( size_t i = 0; i < offsets.size(); ++i ) {
                REQUIRE( storage.at( i ) == offsets[ i ] );
            }
        }

        THEN( "They survive a round trip through the cache format" )
        {
            CompressedLinePositionStorage restored;
            const auto bytes = serialized( storage );
            QDataStream in( bytes );
            REQUIRE( restored.deserialize( in ) );
            REQUIRE( restored.size().get() == offsets.size() );
            for ( size_t i = 0; i < offsets.size(); ++i ) {
                REQUIRE( restored.at( i ) == offsets[ i ] );
            }
        }
    }
}

SCENARIO( "A moved storage keeps its lines", "[compressedlinestorage]" )
{
    GIVEN( "A storage with packed blocks and a tail" )
    {
        const auto offsets = makeOffsets( BlockLines * 2 + 5 );
        CompressedLinePositionStorage storage;
        fill( storage, offsets );

        WHEN( "It is move-constructed and move-assigned" )
        {
            CompressedLinePositionStorage constructed( std::move( storage ) );
            CompressedLinePositionStorage assigned;
            assigned = std::move( constructed );

            THEN( "The lines are in the last owner" )
            {
                REQUIRE( assigned.size().get() == offsets.size() );
                for ( size_t i = 0; i < offsets.size(); ++i ) {
                    REQUIRE( assigned.at( i ) == offsets[ i ] );
                }
            }
        }
    }
}

SCENARIO( "The disk cache format is read back, and refused when it is damaged",
          "[compressedlinestorage]" )
{
    GIVEN( "A serialized storage" )
    {
        const auto offsets = makeOffsets( BlockLines * 2 + 17 );
        CompressedLinePositionStorage storage;
        fill( storage, offsets );
        const auto bytes = serialized( storage );

        THEN( "It restores to the same lines" )
        {
            CompressedLinePositionStorage restored;
            QDataStream in( bytes );
            REQUIRE( restored.deserialize( in ) );
            REQUIRE( restored.size().get() == offsets.size() );
            for ( size_t i = 0; i < offsets.size(); ++i ) {
                REQUIRE( restored.at( i ) == offsets[ i ] );
            }
        }

        THEN( "Every truncation of it is refused" )
        {
            for ( int length = 0; length < bytes.size(); ++length ) {
                INFO( "truncated to " << length << " bytes" );
                REQUIRE( !deserializes( bytes.left( length ) ) );
            }
        }

        THEN( "An absurd block count is refused" )
        {
            auto damaged = bytes;
            putBigEndian32( damaged, 0, 0xffffffffu );
            REQUIRE( !deserializes( damaged ) );
        }

        THEN( "A line count that the blocks do not add up to is refused" )
        {
            auto damaged = bytes;
            // The line count is the second last 64-bit value of the stream.
            damaged[ damaged.size() - 9 ] = static_cast<char>( damaged[ damaged.size() - 9 ] + 1 );
            REQUIRE( !deserializes( damaged ) );
        }

        THEN( "A block that points outside the packed bytes is refused" )
        {
            auto damaged = bytes;
            // The first block's storage offset is the 64-bit value after the
            // block count (4 bytes) and the block's first offset (8 bytes).
            putBigEndian32( damaged, 4 + 8, 0x7fffffffu );
            REQUIRE( !deserializes( damaged ) );
        }
    }

    GIVEN( "An empty storage serialized" )
    {
        CompressedLinePositionStorage storage;
        const auto bytes = serialized( storage );

        THEN( "It restores to an empty storage" )
        {
            CompressedLinePositionStorage restored;
            QDataStream in( bytes );
            REQUIRE( restored.deserialize( in ) );
            REQUIRE( restored.size().get() == 0 );
        }
    }
}
