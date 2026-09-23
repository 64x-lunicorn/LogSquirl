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

#include "sparselineread.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Planning a sparse read over an Index held in memory: every Log Line is ten
// bytes long, so Log Line n ends at byte 10 * (n + 1).

namespace {

struct Line {
    std::size_t request;
    qint64 begin;
    qint64 end;

    bool operator==( const Line& ) const = default;
};

struct Read {
    std::int64_t firstByte;
    qint64 size;
    std::vector<Line> lines;

    bool operator==( const Read& ) const = default;
};

std::ostream& operator<<( std::ostream& out, const Read& read )
{
    out << "{ firstByte " << read.firstByte << ", size " << read.size << ", lines";
    for ( const auto& line : read.lines ) {
        out << " (" << line.request << ": " << line.begin << "-" << line.end << ")";
    }
    return out << " }";
}

class TenByteIndex {
public:
    explicit TenByteIndex( std::uint64_t nbLines )
        : nbLines_( nbLines )
    {
    }

    LinesCount nbLines() const
    {
        return LinesCount( nbLines_ );
    }

    // Answers like the Index would, as far as it reaches.
    EndOfLineOffsets offsets()
    {
        return [ this ]( LineNumber first, LinesCount count ) {
            ++lookups;
            logsquirl::vector<OffsetInFile> offsets;
            for ( auto line = first.get(); line < first.get() + count.get() && line < reach;
                  ++line ) {
                offsets.push_back( OffsetInFile( static_cast<std::int64_t>( 10 * ( line + 1 ) ) ) );
            }
            return offsets;
        };
    }

    int lookups = 0;
    // Where the Index ends, as if it was truncated while being read.
    std::uint64_t reach = UINT64_MAX;

private:
    std::uint64_t nbLines_;
};

std::vector<Read> plan( TenByteIndex& index, std::vector<LineNumber> lines,
                        const SparseReadLimits& limits = {} )
{
    std::vector<Read> reads;
    for ( const auto& read : planSparseRead( lines, index.nbLines(), index.offsets(), limits ) ) {
        Read r{ read.firstByte.get(), read.size, {} };
        for ( const auto& line : read.lines ) {
            r.lines.push_back( { line.request, line.begin, line.end } );
        }
        reads.push_back( r );
    }
    return reads;
}

SparseReadLimits closeLimits()
{
    SparseReadLimits limits;
    limits.maxBytesBetween = 64;
    limits.maxReadBytes = 1000;
    return limits;
}

} // namespace

SCENARIO( "A sparse read reads nearby Log Lines together", "[sparselineread]" )
{
    TenByteIndex index( 100 );

    THEN( "contiguous Log Lines are one read" )
    {
        REQUIRE(
            plan( index, { 3_lnum, 4_lnum, 5_lnum }, closeLimits() )
            == std::vector<Read>{ { 30, 30, { { 0, 0, 10 }, { 1, 10, 20 }, { 2, 20, 30 } } } } );
    }

    THEN( "Log Lines a few bytes apart are one read, skipping what lies between them" )
    {
        REQUIRE(
            plan( index, { 3_lnum, 5_lnum, 9_lnum }, closeLimits() )
            == std::vector<Read>{ { 30, 70, { { 0, 0, 10 }, { 1, 20, 30 }, { 2, 60, 70 } } } } );
    }

    THEN( "the Index is looked up once for Log Lines that are near each other" )
    {
        plan( index, { 3_lnum, 5_lnum, 9_lnum, 12_lnum }, closeLimits() );
        REQUIRE( index.lookups == 1 );
    }

    THEN( "Log Lines more bytes apart than allowed are separate reads" )
    {
        // 70 bytes lie between Log Line 3 and Log Line 11.
        REQUIRE(
            plan( index, { 3_lnum, 11_lnum }, closeLimits() )
            == std::vector<Read>{ { 30, 10, { { 0, 0, 10 } } }, { 110, 10, { { 1, 0, 10 } } } } );
    }

    THEN( "Log Lines far apart in the Index are looked up separately, and still share a read" )
    {
        auto limits = closeLimits();
        limits.maxBytesBetween = 1000;
        REQUIRE( plan( index, { 3_lnum, 90_lnum }, limits )
                 == std::vector<Read>{ { 30, 880, { { 0, 0, 10 }, { 1, 870, 880 } } } } );
        REQUIRE( index.lookups == 2 );
    }

    THEN( "a read joins no further Log Line across a gap once it is long enough" )
    {
        auto limits = closeLimits();
        limits.maxReadBytes = 25;
        REQUIRE( plan( index, { 0_lnum, 2_lnum, 4_lnum }, limits )
                 == std::vector<Read>{ { 0, 30, { { 0, 0, 10 }, { 1, 20, 30 } } },
                                       { 40, 10, { { 2, 0, 10 } } } } );
    }

    THEN( "contiguous Log Lines stay one read however long it grows" )
    {
        auto limits = closeLimits();
        limits.maxReadBytes = 15;
        REQUIRE(
            plan( index, { 0_lnum, 1_lnum, 2_lnum }, limits )
            == std::vector<Read>{ { 0, 30, { { 0, 0, 10 }, { 1, 10, 20 }, { 2, 20, 30 } } } } );
    }

    THEN( "the first and the last Log Line are read" )
    {
        REQUIRE(
            plan( index, { 0_lnum, 99_lnum }, closeLimits() )
            == std::vector<Read>{ { 0, 10, { { 0, 0, 10 } } }, { 990, 10, { { 1, 0, 10 } } } } );
    }
}

SCENARIO( "A sparse read takes Log Lines as they are asked for", "[sparselineread]" )
{
    TenByteIndex index( 10 );

    THEN( "Log Lines past the last one are in no read" )
    {
        REQUIRE( plan( index, { 8_lnum, 9_lnum, 10_lnum, 15_lnum }, closeLimits() )
                 == std::vector<Read>{ { 80, 20, { { 0, 0, 10 }, { 1, 10, 20 } } } } );
    }

    THEN( "Log Lines asked for out of order or twice are read in order, each time asked" )
    {
        REQUIRE(
            plan( index, { 6_lnum, 2_lnum, 2_lnum }, closeLimits() )
            == std::vector<Read>{ { 20, 50, { { 1, 0, 10 }, { 2, 0, 10 }, { 0, 40, 50 } } } } );
    }

    THEN( "nothing asked for is nothing to read" )
    {
        REQUIRE( plan( index, {}, closeLimits() ).empty() );
        REQUIRE( index.lookups == 0 );
    }

    WHEN( "the Index reaches less far than it should" )
    {
        index.reach = 5;

        THEN( "the Log Lines it has no offsets for are in no read" )
        {
            REQUIRE( plan( index, { 3_lnum, 4_lnum, 7_lnum }, closeLimits() )
                     == std::vector<Read>{ { 30, 20, { { 0, 0, 10 }, { 1, 10, 20 } } } } );
        }
    }
}
