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

// Micro-benchmarks for the line positions of an Index (#321): appending them
// one by one, as tailing does, and a block at a time, as indexing does, and
// reading them back one by one and as a range. The Log Lines are of ordinary
// length, so every compressed block spans far less than 4 GiB.
//
// Uses only what LinePositionArray offered before #321, so the same file
// measures both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "linepositionarray.h"

#include <cstdint>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr uint64_t LogLineCount = 2'000'000;
constexpr uint64_t IndexingBlockLines = 10'000;
constexpr uint64_t RandomReads = 100'000;

// End of line positions of Log Lines between 60 and 250 bytes long.
std::vector<OffsetInFile> linePositions()
{
    std::vector<OffsetInFile> positions;
    positions.reserve( LogLineCount );
    int64_t position = 0;
    uint32_t state = 12345;
    for ( uint64_t line = 0; line < LogLineCount; ++line ) {
        state = state * 1664525u + 1013904223u;
        position += 60 + ( state >> 8 ) % 190;
        positions.push_back( OffsetInFile( position ) );
    }
    return positions;
}

} // namespace

TEST_CASE( "Line positions of an Index", "[linepositionarray-benchmark]" )
{
    const auto positions = linePositions();

    BENCHMARK( "append, line by line" )
    {
        LinePositionArray array;
        for ( const auto position : positions ) {
            array.append( position );
        }
        return array.size();
    };

    std::vector<FastLinePositionArray> indexingBlocks;
    for ( uint64_t first = 0; first < LogLineCount; first += IndexingBlockLines ) {
        auto& block = indexingBlocks.emplace_back();
        for ( auto line = first; line < first + IndexingBlockLines && line < LogLineCount;
              ++line ) {
            block.append( positions[ line ] );
        }
    }

    BENCHMARK( "append_list, a block of Log Lines at a time" )
    {
        LinePositionArray array;
        for ( const auto& block : indexingBlocks ) {
            array.append_list( block );
        }
        return array.size();
    };

    LinePositionArray array;
    for ( const auto& block : indexingBlocks ) {
        array.append_list( block );
    }
    REQUIRE( array.size() == LinesCount( LogLineCount ) );

    std::vector<uint64_t> randomLines;
    uint32_t state = 54321;
    for ( uint64_t read = 0; read < RandomReads; ++read ) {
        state = state * 1664525u + 1013904223u;
        randomLines.push_back( state % LogLineCount );
    }

    BENCHMARK( "at, random Log Lines" )
    {
        int64_t sum = 0;
        for ( const auto line : randomLines ) {
            sum += array.at( line ).get();
        }
        return sum;
    };

    BENCHMARK( "range, the whole Index" )
    {
        return array.range( 0_lnum, LinesCount( LogLineCount ) ).size();
    };
}
