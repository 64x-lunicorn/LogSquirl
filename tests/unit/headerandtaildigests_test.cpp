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

#include "filedigest.h"
#include "headerandtaildigests.h"

#include <QByteArray>

#include <algorithm>
#include <cstddef>

#include <catch2/catch.hpp>

// The header and tail digests of an Index taken as its bytes are indexed
// (#277), checked against digests of the same byte ranges taken in one go.
// A block of 16 bytes stands in for the indexing block.

namespace {

constexpr qint64 BlockSize = 16;

quint64 digestOfRange( const QByteArray& content, qint64 offset, qint64 size )
{
    FileDigest digest;
    digest.addData( content.constData() + offset, static_cast<std::size_t>( size ) );
    return digest.digest();
}

QByteArray logBytes( int size )
{
    QByteArray content;
    for ( int i = 0; i < size; ++i ) {
        content += static_cast<char>( 'a' + ( i * 7 ) % 26 );
    }
    return content;
}

void requireHeaderOf( const HeaderAndTailDigests& digests, const QByteArray& content, qint64 size )
{
    const auto header = digests.header( size );
    REQUIRE( header.has_value() );
    REQUIRE( header->offset == 0 );
    REQUIRE( header->size == std::min( size, BlockSize ) );
    REQUIRE( header->digest == digestOfRange( content, 0, header->size ) );
}

void requireTailOf( const HeaderAndTailDigests& digests, const QByteArray& content, qint64 size )
{
    const auto tail = digests.tail( size );
    REQUIRE( tail.has_value() );
    const auto expected = HeaderAndTailDigests::tailRange( BlockSize, size );
    REQUIRE( tail->offset == expected.offset );
    REQUIRE( tail->size == expected.size );
    REQUIRE( tail->offset + tail->size == size );
    REQUIRE( tail->digest == digestOfRange( content, tail->offset, tail->size ) );
}

} // namespace

TEST_CASE( "The tail of a Log File is between half a block and a block long",
           "[headerandtaildigests]" )
{
    struct Expected {
        qint64 logFileSize;
        qint64 offset;
        qint64 size;
    };
    const auto expected
        = GENERATE( Expected{ 0, 0, 0 }, Expected{ 5, 0, 5 }, Expected{ 15, 0, 15 },
                    Expected{ 16, 8, 8 }, Expected{ 23, 8, 15 }, Expected{ 24, 16, 8 },
                    Expected{ 1000, 992, 8 }, Expected{ 1007, 992, 15 } );

    const auto range = HeaderAndTailDigests::tailRange( BlockSize, expected.logFileSize );
    REQUIRE( range.offset == expected.offset );
    REQUIRE( range.size == expected.size );
}

SCENARIO( "The header and tail digests follow a Log File as it grows", "[headerandtaildigests]" )
{
    const auto content = logBytes( 200 );

    GIVEN( "a Log File fed from its start in appends of any size" )
    {
        const auto appendSize = GENERATE( 1, 3, 8, 16, 21 );
        HeaderAndTailDigests digests( BlockSize );

        THEN( "after every append both digests are those of the bytes fed so far" )
        {
            for ( qint64 end = 0; end < content.size(); ) {
                const auto size = std::min<qint64>( appendSize, content.size() - end );
                digests.add( end, content.constData() + end, size );
                end += size;

                INFO( "Log File size " << end );
                requireHeaderOf( digests, content, end );
                requireTailOf( digests, content, end );
                REQUIRE_FALSE( digests.tail( end - 1 ).has_value() );
            }
        }
    }

    GIVEN( "a Log File indexed at once when its size was known" )
    {
        HeaderAndTailDigests digests( BlockSize );
        digests.expectLogFileSize( 150 );
        digests.add( 0, content.constData(), 150 );

        THEN( "its header and tail are known" )
        {
            requireHeaderOf( digests, content, 150 );
            requireTailOf( digests, content, 150 );
        }

        WHEN( "it grows further, byte by byte" )
        {
            for ( qint64 end = 150; end < content.size(); ++end ) {
                digests.add( end, content.constData() + end, 1 );
            }

            THEN( "the tail follows it" )
            {
                requireHeaderOf( digests, content, content.size() );
                requireTailOf( digests, content, content.size() );
            }
        }
    }

    GIVEN( "bytes fed from the middle of a Log File, as after loading a cached Index" )
    {
        HeaderAndTailDigests digests( BlockSize );
        digests.add( 20, content.constData() + 20, 10 );

        THEN( "neither the header nor a tail starting before them is known" )
        {
            REQUIRE_FALSE( digests.header( 30 ).has_value() );
            REQUIRE_FALSE( digests.tail( 30 ).has_value() );
        }

        WHEN( "enough is appended for the tail to start past where they started" )
        {
            digests.add( 30, content.constData() + 30, 10 );

            THEN( "the tail is known, the header still is not" )
            {
                REQUIRE_FALSE( digests.header( 40 ).has_value() );
                requireTailOf( digests, content, 40 );
            }
        }

        WHEN( "the tail range is fed again from the Log File" )
        {
            const auto range = HeaderAndTailDigests::tailRange( BlockSize, 30 );
            digests.add( range.offset, content.constData() + range.offset, range.size );

            THEN( "the tail is known again, and follows further appends" )
            {
                requireTailOf( digests, content, 30 );
                digests.add( 30, content.constData() + 30, 50 );
                requireTailOf( digests, content, 80 );
            }
        }
    }

    GIVEN( "bytes that do not continue those fed before" )
    {
        HeaderAndTailDigests digests( BlockSize );
        digests.add( 0, content.constData(), 60 );
        const auto other = logBytes( 100 ).replace( 'a', 'z' );
        digests.add( 70, other.constData() + 70, 10 );

        THEN( "nothing fed before them counts" )
        {
            REQUIRE_FALSE( digests.header( 80 ).has_value() );
            REQUIRE_FALSE( digests.tail( 60 ).has_value() );
            requireTailOf( digests, other, 80 );
        }
    }

    GIVEN( "digests that were reset" )
    {
        HeaderAndTailDigests digests( BlockSize );
        digests.add( 0, content.constData(), 60 );
        digests.reset();
        const auto replaced = logBytes( 100 ).replace( 'b', 'y' );
        digests.add( 0, replaced.constData(), 40 );

        THEN( "they are those of the bytes fed since" )
        {
            requireHeaderOf( digests, replaced, 40 );
            requireTailOf( digests, replaced, 40 );
        }
    }
}
