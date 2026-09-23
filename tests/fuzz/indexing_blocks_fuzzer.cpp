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

// Fuzz target: indexing parses a Log File in blocks, each on its own and in
// parallel, then stitches them together (#319). Whatever bytes a file holds,
// in whatever of the encodings' line feed widths, parsing and stitching must
// stay within the blocks.

#include "indexingblocks.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

using namespace indexing_blocks;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    FuzzedDataProvider input( data, size );

    EncodingParameters encoding;
    encoding.lineFeedWidth = input.PickValueInArray( { 1, 2, 4 } );
    encoding.lineFeedIndex = input.ConsumeIntegralInRange<int>( 0, encoding.lineFeedWidth - 1 );

    OpenLogLine line;
    std::int64_t beginning = 0;
    const auto blockCount = input.ConsumeIntegralInRange<int>( 1, 4 );
    for ( int sequence = 0; sequence < blockCount; ++sequence ) {
        const auto bytes = input.ConsumeBytes<char>(
            input.ConsumeIntegralInRange<std::size_t>( 0, input.remaining_bytes() ) );
        const auto blockSize = static_cast<std::int64_t>( bytes.size() );

        IndexingBlock block( std::max<std::int64_t>( blockSize, 1 ) );
        std::copy( bytes.begin(), bytes.end(), block.bytes() );
        block.sequence = static_cast<std::size_t>( sequence );
        block.beginning = beginning;
        block.size = blockSize;
        block.encoding = encoding;

        // The bytes of the neighbouring blocks, kept around the block's own.
        block.bytesBefore = sequence == 0 ? 0 : input.ConsumeIntegralInRange<int>( 0, MaxDelimiterNeighbours );
        block.bytesAfter = sequence + 1 == blockCount ? 0 : input.ConsumeIntegralInRange<int>( 0, MaxDelimiterNeighbours );
        for ( int i = 1; i <= block.bytesBefore; ++i ) {
            block.bytes()[ -i ] = static_cast<char>( input.ConsumeIntegral<std::uint8_t>() );
        }
        for ( int i = 0; i < block.bytesAfter; ++i ) {
            block.bytes()[ blockSize + i ] = static_cast<char>( input.ConsumeIntegral<std::uint8_t>() );
        }

        parseBlock( block );
        stitchBlock( block, line );
        openLineLength( block, line );

        beginning += blockSize;
    }
    return 0;
}
