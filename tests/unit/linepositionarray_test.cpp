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

#include <catch2/catch.hpp>

#include "linetypes.h"
#include "log.h"

#include "linepositionarray.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

#include <configuration.h>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

SCENARIO( "A moved LinePositionArray keeps growing correctly", "[linepositionarray]" )
{
    GIVEN( "an array holding several compressed blocks, moved into another" )
    {
        LinePositionArray original;
        std::vector<OffsetInFile> offsets;
        for ( auto line = 1; line <= 1000; ++line ) {
            offsets.push_back( OffsetInFile( line * 16 + ( line % 13 ) ) );
            original.append( offsets.back() );
        }
        LinePositionArray moved( std::move( original ) );

        WHEN( "its last line is dropped and more lines are appended, filling new blocks" )
        {
            moved.pop_back();
            offsets.pop_back();
            for ( auto line = 1000; line <= 1600; ++line ) {
                offsets.push_back( OffsetInFile( line * 16 + ( line % 13 ) ) );
                moved.append( offsets.back() );
            }

            THEN( "every line position, old and new, reads back as appended" )
            {
                REQUIRE( moved.size().get() == offsets.size() );
                for ( auto i = 0u; i < offsets.size(); ++i ) {
                    REQUIRE( moved.at( i ) == offsets[ i ] );
                }
            }
        }
    }
}

SCENARIO( "LinePositionArray with small number of lines", "[linepositionarray]" )
{

    std::array<OffsetInFile, 6> offsets = { 4_offset,     8_offset, 10_offset,
                                            345_offset,   // A longer (>128) line
                                            20000_offset, // An even longer (>16384) line
                                            20020_offset };

    GIVEN( "LinePositionArray with small number of lines" )
    {

        LinePositionArray line_array;

        for ( const auto& offset : offsets ) {
            line_array.append( offset );
        }

        REQUIRE( line_array.size() == 6_lcount );

        WHEN( "Access items in linear order" )
        {
            THEN( "Corrent offsets returned" )
            {
                for ( auto i = 0u; i < offsets.size(); ++i ) {
                    REQUIRE( line_array.at( i ) == offsets[ i ] );
                }
            }
        }

        WHEN( "Access items in random order" )
        {
            std::random_device rd;
            std::mt19937 g( rd() );

            auto index = std::vector<uint32_t>( offsets.size() );
            std::generate( index.begin(), index.end(), [ n = 0u ]() mutable { return n++; } );

            THEN( "Corrent offsets returned" )
            {
                for ( auto i : index ) {
                    std::cout << "Test " << i << std::endl;
                    REQUIRE( line_array.at( i ) == offsets[ i ] );
                }
            }
        }

        WHEN( "Adding fake lf" )
        {
            line_array.setFakeFinalLF();

            THEN( "Last offset is returned" )
            {
                REQUIRE( line_array.at( 5 ) == offsets[ 5 ] );
            }
        }

        WHEN( "Adding line after fake lf" )
        {
            line_array.setFakeFinalLF();
            line_array.append( 20030_offset );

            THEN( "New last offset is returned" )
            {
                REQUIRE( line_array.at( 5 ) == 20030_offset );
            }
        }

        WHEN( "Add line to single line array with fake lf" )
        {
            LinePositionArray one_line_array;
            one_line_array.append( 10_offset );
            one_line_array.setFakeFinalLF();
            one_line_array.append( 20_offset );
            THEN( "New last offset is returned" )
            {
                REQUIRE( one_line_array.at( 0 ) == 20_offset );
            }
        }

        WHEN( "Appending other array " )
        {

            GIVEN( "FastLinePositionArray" )
            {
                FastLinePositionArray other_array;
                other_array.append( 150000_offset );
                other_array.append( 150023_offset );

                WHEN( "Appending other array without fake lf" )
                {
                    line_array.append_list( other_array );

                    THEN( "All lines are kept" )
                    {
                        for ( auto i = 0u; i < offsets.size(); ++i ) {
                            REQUIRE( line_array.at( i ) == offsets[ i ] );
                        }
                        REQUIRE( line_array.at( 6 ) == other_array.at( 0 ) );
                        REQUIRE( line_array.at( 7 ) == other_array.at( 1 ) );
                    }
                }

                WHEN( "Appending after fake lf" )
                {
                    line_array.setFakeFinalLF();
                    line_array.append_list( other_array );

                    THEN( "Last line is popped back" )
                    {
                        REQUIRE( line_array.size() == 7_lcount );
                        for ( auto i = 0u; i < offsets.size() - 1; ++i ) {
                            REQUIRE( line_array.at( i ) == offsets[ i ] );
                        }
                        REQUIRE( line_array.at( 5 ) == other_array.at( 0 ) );
                        REQUIRE( line_array.at( 6 ) == other_array.at( 1 ) );
                    }
                }
            }
        }
    }
}

SCENARIO( "LinePositionArray with full block of lines", "[linepositionarray]" )
{

    GIVEN( "LinePositionArray with block of lines" )
    {

        LinePositionArray line_array;

        // Add 255 lines (of various sizes)
        const int lines = 255;
        for ( int i = 0; i < lines; ++i )
            line_array.append( OffsetInFile( i * 4 ) );
        // Line no 256
        line_array.append( OffsetInFile( 255 * 4 ) );

        WHEN( "Adding line after block" )
        {
            // Add line no 257
            line_array.append( OffsetInFile( 255 * 4 + 10 ) );

            THEN( "Correct offset is returned" )
            {
                REQUIRE( line_array.at( 256 ) == OffsetInFile( 255 * 4 + 10 ) );
            }
        }

        WHEN( "Adding lines after fake lf" )
        {
            THEN( "Correct offset is returned" )
            for ( uint32_t i = 0; i < 1000; ++i ) {
                int64_t pos = ( 257LL * 4 ) + i * 35LL;
                line_array.append( OffsetInFile( pos ) );
                line_array.setFakeFinalLF();
                REQUIRE( line_array.at( 256 + i ) == OffsetInFile( pos ) );
                line_array.append( OffsetInFile( pos + 21LL ) );
                REQUIRE( line_array.at( 256 + i ) == OffsetInFile( pos + 21LL ) );
            }
        }
    }
}

SCENARIO( "LinePositionArray with UINT32_MAX offsets", "[linepositionarray]" )
{

    GIVEN( "LinePositionArray with long offsets" )
    {
        std::array<OffsetInFile, 3> offsets = {
            OffsetInFile( UINT32_MAX - 10 ),
            OffsetInFile( (uint64_t)UINT32_MAX + 10LL ),
            OffsetInFile( (uint64_t)UINT32_MAX + 30LL ),
        };

        LinePositionArray line_array;

        for ( const auto& offset : offsets ) {
            line_array.append( offset );
        }

        REQUIRE( line_array.size() == 3_lcount );

        WHEN( "Access items in linear order" )
        {
            THEN( "Corrent offsets returned" )
            {
                for ( auto i = 0u; i < offsets.size(); ++i ) {
                    REQUIRE( line_array.at( i ) == offsets[ i ] );
                }
            }
        }

        WHEN( "Adding lines after fake lf" )
        {
            THEN( "Correct offset is returned" )
            for ( uint32_t i = 0; i < 1000; ++i ) {
                int64_t pos = UINT32_MAX + 524LL + i * 35LL;
                line_array.append( OffsetInFile( pos ) );
                line_array.setFakeFinalLF();
                REQUIRE( line_array.at( offsets.size() + i ) == OffsetInFile( pos ) );
                line_array.append( OffsetInFile( pos + 21LL ) );
                REQUIRE( line_array.at( offsets.size() + i ) == OffsetInFile( pos + 21LL ) );
            }
        }
    }

    GIVEN( "LinePositionArray with small lines" )
    {
        std::array<OffsetInFile, 2> offsets = {
            OffsetInFile( (uint64_t)UINT32_MAX / 2 + 10LL ),
            OffsetInFile( (uint64_t)UINT32_MAX / 2 + 12LL ),
        };
        LinePositionArray line_array;
        for ( const auto& offset : offsets ) {
            line_array.append( offset );
        }

        WHEN( "Appending large lines" )
        {
            FastLinePositionArray other_array;
            other_array.append( OffsetInFile( (uint64_t)UINT32_MAX + 10LL ) );
            other_array.append( OffsetInFile( (uint64_t)UINT32_MAX + 30LL ) );

            line_array.append_list( other_array );

            THEN( "Correct offsets are returned" )
            {
                REQUIRE( line_array.size() == 4_lcount );

                REQUIRE( line_array.at( 0 ) == offsets[ 0 ] );
                REQUIRE( line_array.at( 1 ) == offsets[ 1 ] );
                REQUIRE( line_array.at( 2 ) == OffsetInFile( (uint64_t)UINT32_MAX + 10 ) );
                REQUIRE( line_array.at( 3 ) == OffsetInFile( (uint64_t)UINT32_MAX + 30 ) );
            }
        }
    }
}

SCENARIO( "A block's line positions appended at once span compressed blocks",
          "[linepositionarray]" )
{
    // Indexing appends a block's line positions all at once, and the
    // compressed storage packs them 128 at a time (#290).
    GIVEN( "a LinePositionArray part way into a compressed block" )
    {
        LinePositionArray line_array;
        std::vector<OffsetInFile> expected;
        for ( int i = 1; i <= 100; ++i ) {
            expected.push_back( OffsetInFile( i * 7 ) );
            line_array.append( expected.back() );
        }

        WHEN( "the positions of many lines are appended at once" )
        {
            FastLinePositionArray other_array;
            for ( int i = 1; i <= 700; ++i ) {
                const auto position = OffsetInFile( 1000 + i * 13 );
                expected.push_back( position );
                other_array.append( position );
            }
            line_array.append_list( other_array );

            THEN( "every position is kept, one by one and as a range" )
            {
                REQUIRE( line_array.size() == LinesCount( 800 ) );
                for ( auto i = 0u; i < expected.size(); ++i ) {
                    REQUIRE( line_array.at( i ) == expected[ i ] );
                }
                const auto range = line_array.range( LineNumber( 90 ), LinesCount( 600 ) );
                REQUIRE( std::equal( range.begin(), range.end(), expected.begin() + 90,
                                     expected.begin() + 690 ) );
            }

            AND_WHEN( "more lines are appended one by one after the last one popped" )
            {
                line_array.setFakeFinalLF();
                line_array.append( OffsetInFile( 20000 ) );
                expected.back() = OffsetInFile( 20000 );

                THEN( "the popped line is replaced" )
                {
                    REQUIRE( line_array.size() == LinesCount( 800 ) );
                    for ( auto i = 0u; i < expected.size(); ++i ) {
                        REQUIRE( line_array.at( i ) == expected[ i ] );
                    }
                }
            }
        }

        WHEN( "nothing is appended at once" )
        {
            line_array.append_list( FastLinePositionArray{} );

            THEN( "the positions are unchanged" )
            {
                REQUIRE( line_array.size() == LinesCount( 100 ) );
                REQUIRE( line_array.at( 99 ) == expected.back() );
            }
        }
    }
}

namespace {

// Past 4 GiB, and not a multiple of it, so a position truncated to 32 bits
// is told from the right one.
constexpr int64_t BeyondFourGiB = ( int64_t{ 1 } << 32 ) * 3 + 12345;

// Positions of 600 Log Lines, several compressed blocks' worth, where a few
// Log Lines are longer than 4 GiB: the first block spans more than 4 GiB from
// its second line on, one block does from its last line, and one block holds
// only lines of that length.
std::vector<OffsetInFile> positionsSpanningFourGiB()
{
    std::vector<OffsetInFile> positions;
    int64_t position = 0;
    for ( int line = 0; line < 600; ++line ) {
        const bool longLine = line == 1 || line == 255 || ( line >= 384 && line < 512 );
        position += longLine ? BeyondFourGiB + line : 40 + line % 7;
        positions.push_back( OffsetInFile( position ) );
    }
    return positions;
}

void requireSamePositions( const LinePositionArray& array,
                           const std::vector<OffsetInFile>& expected )
{
    REQUIRE( array.size().get() == expected.size() );
    for ( auto i = 0u; i < expected.size(); ++i ) {
        REQUIRE( array.at( i ) == expected[ i ] );
    }
    const auto all = array.range( 0_lnum, LinesCount( expected.size() ) );
    REQUIRE( std::equal( all.begin(), all.end(), expected.begin(), expected.end() ) );
}

} // namespace

SCENARIO( "Line positions of a compressed block spanning 4 GiB or more are kept exactly",
          "[linepositionarray]" )
{
    // A compressed block stored its positions as 32-bit offsets from its
    // first one (#321).
    const auto expected = positionsSpanningFourGiB();

    GIVEN( "the positions appended one by one" )
    {
        LinePositionArray array;
        for ( const auto position : expected ) {
            array.append( position );
        }

        THEN( "every position reads back exactly, one by one and as ranges" )
        {
            requireSamePositions( array, expected );
            const auto part = array.range( LineNumber( 250 ), LinesCount( 300 ) );
            REQUIRE( std::equal( part.begin(), part.end(), expected.begin() + 250,
                                 expected.begin() + 550 ) );
        }

        WHEN( "lines are dropped back into a block spanning 4 GiB and appended again" )
        {
            auto shortened = expected;
            for ( int i = 0; i < 600 - 500; ++i ) {
                array.pop_back();
                shortened.pop_back();
            }
            for ( int line = 0; line < 200; ++line ) {
                shortened.push_back( shortened.back()
                                     + OffsetInFile( line % 3 == 0 ? BeyondFourGiB : 50 ) );
                array.append( shortened.back() );
            }

            THEN( "every position, old and new, reads back exactly" )
            {
                requireSamePositions( array, shortened );
            }
        }
    }

    GIVEN( "the positions appended a block of Log Lines at a time" )
    {
        LinePositionArray array;
        for ( auto first = 0u; first < expected.size(); first += 100 ) {
            FastLinePositionArray block;
            for ( auto line = first; line < first + 100; ++line ) {
                block.append( expected[ line ] );
            }
            array.append_list( block );
        }

        THEN( "every position reads back exactly" )
        {
            requireSamePositions( array, expected );
        }
    }

    GIVEN( "the positions serialized for the Index cache" )
    {
        CompressedLinePositionStorage storage;
        // 600 lines leave a tail of 88 not yet compressed; three more long
        // lines make that tail span 4 GiB too.
        auto withLongTail = expected;
        for ( int line = 0; line < 3; ++line ) {
            withLongTail.push_back( withLongTail.back() + OffsetInFile( BeyondFourGiB ) );
        }
        for ( const auto position : withLongTail ) {
            storage.append( position );
        }

        QByteArray bytes;
        {
            QDataStream out( &bytes, QIODevice::WriteOnly );
            storage.serialize( out );
            REQUIRE( out.status() == QDataStream::Ok );
        }

        WHEN( "they are deserialized" )
        {
            CompressedLinePositionStorage loaded;
            QDataStream in( bytes );
            REQUIRE( loaded.deserialize( in ) );
            LinePositionArray array( std::move( loaded ) );

            THEN( "every position reads back exactly" )
            {
                requireSamePositions( array, withLongTail );
            }

            AND_WHEN( "more lines are appended" )
            {
                for ( int line = 0; line < 100; ++line ) {
                    withLongTail.push_back( withLongTail.back() + OffsetInFile( 30 ) );
                    array.append( withLongTail.back() );
                }

                THEN( "every position, loaded and new, reads back exactly" )
                {
                    requireSamePositions( array, withLongTail );
                }
            }
        }
    }
}

namespace {

// The fields of a serialized CompressedLinePositionStorage, so a test can
// damage one of them.
struct SerializedStorage {
    std::vector<std::pair<qint64, quint64>> blocks;
    quint64 packedSize = 0;
    QByteArray packedBytes;
    std::vector<qint64> tail;
    qint64 lines = 0;
    qint64 lastPosition = 0;

    static SerializedStorage of( const CompressedLinePositionStorage& storage )
    {
        QByteArray bytes;
        {
            QDataStream out( &bytes, QIODevice::WriteOnly );
            storage.serialize( out );
        }
        QDataStream in( bytes );
        SerializedStorage fields;
        quint32 blockCount = 0;
        in >> blockCount;
        for ( quint32 i = 0; i < blockCount; ++i ) {
            qint64 firstOffset = 0;
            quint64 storageOffset = 0;
            in >> firstOffset >> storageOffset;
            fields.blocks.emplace_back( firstOffset, storageOffset );
        }
        in >> fields.packedSize;
        fields.packedBytes.resize( static_cast<qsizetype>( fields.packedSize ) );
        in.readRawData( fields.packedBytes.data(), static_cast<int>( fields.packedSize ) );
        quint32 tailCount = 0;
        in >> tailCount;
        for ( quint32 i = 0; i < tailCount; ++i ) {
            qint64 offset = 0;
            in >> offset;
            fields.tail.push_back( offset );
        }
        in >> fields.lines >> fields.lastPosition;
        REQUIRE( in.status() == QDataStream::Ok );
        return fields;
    }

    bool deserializeInto( CompressedLinePositionStorage& storage ) const
    {
        QByteArray bytes;
        {
            QDataStream out( &bytes, QIODevice::WriteOnly );
            out << static_cast<quint32>( blocks.size() );
            for ( const auto& [ firstOffset, storageOffset ] : blocks ) {
                out << firstOffset << storageOffset;
            }
            out << packedSize;
            out.writeRawData( packedBytes.constData(), static_cast<int>( packedBytes.size() ) );
            out << static_cast<quint32>( tail.size() );
            for ( const auto offset : tail ) {
                out << offset;
            }
            out << lines << lastPosition;
        }
        QDataStream in( bytes );
        return storage.deserialize( in );
    }
};

} // namespace

SCENARIO( "Packed line positions loaded from the Index cache must lie within the packed bytes",
          "[linepositionarray]" )
{
    // 300 short Log Lines: two compressed blocks holding one byte per
    // position, and a tail of 44 (#321).
    std::vector<OffsetInFile> expected;
    CompressedLinePositionStorage storage;
    for ( int line = 0; line < 300; ++line ) {
        expected.push_back( OffsetInFile( line * 40 ) );
        storage.append( expected.back() );
    }
    const auto fields = SerializedStorage::of( storage );
    REQUIRE( fields.blocks.size() == 2 );
    REQUIRE( fields.packedSize == 2 * ( 32 + 128 ) );

    WHEN( "the serialized positions are loaded unchanged" )
    {
        CompressedLinePositionStorage loaded;
        REQUIRE( fields.deserializeInto( loaded ) );
        LinePositionArray array( std::move( loaded ) );

        THEN( "every position reads back exactly, also after more are appended" )
        {
            requireSamePositions( array, expected );
            for ( int line = 0; line < 300; ++line ) {
                expected.push_back( expected.back() + OffsetInFile( 70'000 + line ) );
                array.append( expected.back() );
            }
            requireSamePositions( array, expected );
        }
    }

    WHEN( "the last block's control bytes claim more bytes than were stored" )
    {
        auto damaged = fields;
        // Four 4-byte offsets instead of four 1-byte ones.
        damaged.packedBytes[ 32 + 128 ] = static_cast<char>( 0xFF );

        THEN( "loading fails" )
        {
            CompressedLinePositionStorage loaded;
            REQUIRE_FALSE( damaged.deserializeInto( loaded ) );
        }
    }

    WHEN( "the packed bytes are cut short inside the last block" )
    {
        auto damaged = fields;
        damaged.packedSize -= 20;
        damaged.packedBytes.chop( 20 );

        THEN( "loading fails" )
        {
            CompressedLinePositionStorage loaded;
            REQUIRE_FALSE( damaged.deserializeInto( loaded ) );
        }
    }

    WHEN( "a block starts too close to the end of the packed bytes for its control bytes" )
    {
        auto damaged = fields;
        damaged.blocks.back().second = damaged.packedSize - 10;

        THEN( "loading fails" )
        {
            CompressedLinePositionStorage loaded;
            REQUIRE_FALSE( damaged.deserializeInto( loaded ) );
        }
    }

    WHEN( "a block starts beyond the packed bytes" )
    {
        auto damaged = fields;
        damaged.blocks.back().second = damaged.packedSize + 1000;

        THEN( "loading fails" )
        {
            CompressedLinePositionStorage loaded;
            REQUIRE_FALSE( damaged.deserializeInto( loaded ) );
        }
    }
}
