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

#include <catch2/catch.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QTextCodec>

#include "test_policies.h"

#include "atomicflag.h"
#include "linetypes.h"
#include "logdataworker.h"

// Indexing reads a Log File in blocks and parses them in parallel, so the
// Log Lines crossing from one block into the next are where it can go wrong
// (#290). These tests index generated Log Files in blocks of a few bytes, so
// that nearly every Log Line crosses a block boundary, and hold the Index to
// what a scan of the whole Log File, byte by byte, finds.

namespace {

constexpr qint64 DefaultIndexingBlockSize = 5 * 1024 * 1024;

// What one indexing run left behind.
struct IndexingRun {
    std::vector<qint64> endOfLines;
    qint64 maxLength = 0;
    IndexedHash hash;
    qint64 blockBuffers = 0;
};

// The Index a scan of the whole Log File finds, one byte after the other.
//
// A line feed or a tab counts where its byte is followed (in an encoding
// whose line feed starts with it) or preceded (otherwise) by as many zero
// bytes as the encoding's line feed has. A Log Line ends where its line feed
// starts, and the next one starts right after it. A tab widens its Log Line
// to the next tab stop; a Log Line is as long as its characters, tabs
// widened. A Log File not ending in a line feed gets an end of line one byte
// past its end.
struct ReferenceIndex {
    std::vector<qint64> endOfLines;
    qint64 maxLength = 0;
};

ReferenceIndex scanWholeLogFile( const QByteArray& bytes, int lineFeedWidth, int lineFeedIndex )
{
    const auto size = static_cast<qint64>( bytes.size() );
    const auto isDelimiter = [ & ]( qint64 at ) {
        for ( int k = 1; k < lineFeedWidth; ++k ) {
            const auto neighbour = lineFeedIndex == 0 ? at + k : at - k;
            if ( neighbour < 0 || neighbour >= size || bytes[ neighbour ] != '\0' ) {
                return false;
            }
        }
        return true;
    };

    ReferenceIndex index;
    qint64 lineStart = 0;
    qint64 widened = 0;
    for ( qint64 at = 0; at < size; ++at ) {
        if ( ( bytes[ at ] != '\n' && bytes[ at ] != '\t' ) || !isDelimiter( at ) ) {
            continue;
        }
        const auto characterStart = at - lineFeedIndex;
        if ( bytes[ at ] == '\t' ) {
            const auto column = ( characterStart - lineStart ) / lineFeedWidth + widened;
            widened += TabStop - column % TabStop - 1;
        }
        else {
            index.maxLength = std::max( index.maxLength,
                                        ( characterStart - lineStart ) / lineFeedWidth + widened );
            lineStart = characterStart + lineFeedWidth;
            widened = 0;
            index.endOfLines.push_back( lineStart );
        }
    }
    if ( lineStart < size ) {
        index.maxLength = std::max(
            index.maxLength, ( size - lineFeedIndex - lineStart ) / lineFeedWidth + widened );
        index.endOfLines.push_back( size + 1 );
    }
    return index;
}

void writeLogFile( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

IndexingRun indexInBlocks( const QString& path, QTextCodec* encoding, qint64 blockSize,
                           int readBufferSizeMb = 16 )
{
    auto policy = testSettingsPolicies().indexing;
    policy.readBufferSizeMb = readBufferSizeMb;

    auto data = std::make_shared<IndexingData>();
    AtomicFlag interruptRequest;
    FullIndexOperation operation{ path, data, interruptRequest, policy, encoding };
    operation.setBlockSize( blockSize );
    REQUIRE( std::get<bool>( operation.run() ) );

    IndexingRun run;
    IndexingData::ConstAccessor accessor{ data.get() };
    for ( auto line = 0u; line < accessor.getNbLines().get(); ++line ) {
        run.endOfLines.push_back( accessor.getEndOfLineOffset( LineNumber( line ) ).get() );
    }
    run.maxLength = accessor.getMaxLength().get();
    run.hash = accessor.getHash();
    run.blockBuffers = operation.blockBuffersAllocated();
    return run;
}

// Printable text with tabs, carriage returns and empty lines, lines from
// empty to a few hundred characters long.
QString generatedText( std::uint32_t seed, int lines, bool trailingLineFeed )
{
    std::mt19937 random( seed );
    const auto pick = [ &random ]( int bound ) {
        return static_cast<int>( random() % static_cast<std::uint32_t>( bound ) );
    };
    QString text;
    for ( int line = 0; line < lines; ++line ) {
        const auto length = pick( 8 ) == 0 ? 0 : pick( 12 ) == 0 ? 300 + pick( 200 ) : pick( 60 );
        for ( int character = 0; character < length; ++character ) {
            const auto kind = pick( 10 );
            text += kind == 0 ? QChar( '\t' ) : QChar( 'a' + pick( 26 ) );
        }
        if ( pick( 4 ) == 0 ) {
            text += QChar( '\r' );
        }
        if ( line + 1 < lines || trailingLineFeed ) {
            text += QChar( '\n' );
        }
    }
    return text;
}

// Bytes mostly made of line feed, tab and zero bytes, so that in a
// multi-byte encoding line feeds and tabs are met both where they count and
// where they do not, at every alignment.
QByteArray delimiterNoise( std::uint32_t seed, int size )
{
    std::mt19937 random( seed );
    static constexpr char Alphabet[] = { '\n', '\t', '\0', '\0', '\0', 'x' };
    QByteArray bytes( size, Qt::Uninitialized );
    for ( auto& byte : bytes ) {
        byte = Alphabet[ random() % sizeof( Alphabet ) ];
    }
    return bytes;
}

struct Encoded {
    const char* encoding;
    int lineFeedWidth;
    int lineFeedIndex;
};

constexpr Encoded Encodings[] = {
    { "UTF-8", 1, 0 },    { "UTF-16LE", 2, 0 }, { "UTF-16BE", 2, 1 },
    { "UTF-32LE", 4, 0 }, { "UTF-32BE", 4, 3 },
};

void requireIndexOfWholeScan( const IndexingRun& run, const QByteArray& bytes,
                              const Encoded& encoded )
{
    const auto reference = scanWholeLogFile( bytes, encoded.lineFeedWidth, encoded.lineFeedIndex );
    REQUIRE( run.endOfLines.size() == reference.endOfLines.size() );
    REQUIRE( run.endOfLines == reference.endOfLines );
    REQUIRE( run.maxLength == reference.maxLength );
}

} // namespace

TEST_CASE( "The whole-Log-File scan widens tabs to the next tab stop", "[indexing][blocks]" )
{
    // Worked examples, so the scan the tests below hold indexing to is itself
    // held to something.
    CHECK( scanWholeLogFile( "a\tb\n", 1, 0 ).maxLength == 9 );
    CHECK( scanWholeLogFile( "abcdefgh\tx\n", 1, 0 ).maxLength == 17 );
    CHECK( scanWholeLogFile( "\t\t\n", 1, 0 ).maxLength == 16 );
    CHECK( scanWholeLogFile( "ab\r\nc", 1, 0 ).endOfLines == std::vector<qint64>{ 4, 6 } );
    CHECK( scanWholeLogFile( "ab\r\nc", 1, 0 ).maxLength == 3 );
    CHECK( scanWholeLogFile( QByteArray( "a\0\t\0b\0\n\0", 8 ), 2, 0 ).maxLength == 9 );
    CHECK( scanWholeLogFile( QByteArray( "a\0\t\0b\0\n\0", 8 ), 2, 0 ).endOfLines
           == std::vector<qint64>{ 8 } );
    CHECK( scanWholeLogFile( QByteArray( "\0a\0\t\0b\0\n", 8 ), 2, 1 ).endOfLines
           == std::vector<qint64>{ 8 } );
    CHECK( scanWholeLogFile( "", 1, 0 ).endOfLines.empty() );
}

SCENARIO( "Indexing in blocks finds the Log Lines a scan of the whole Log File finds",
          "[indexing][blocks]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( "generated.log" );

    const auto blockSize
        = GENERATE( qint64{ 1 }, qint64{ 2 }, qint64{ 3 }, qint64{ 5 }, qint64{ 16 }, qint64{ 17 },
                    qint64{ 64 }, qint64{ 1000 }, DefaultIndexingBlockSize );
    const auto encoded = GENERATE( from_range( std::begin( Encodings ), std::end( Encodings ) ) );
    auto* codec = QTextCodec::codecForName( encoded.encoding );
    REQUIRE( codec != nullptr );

    const auto encode = [ codec ]( const QString& text ) {
        QTextCodec::ConverterState state( QTextCodec::IgnoreHeader );
        return codec->fromUnicode( text.constData(), static_cast<int>( text.size() ), &state );
    };

    CAPTURE( blockSize, encoded.encoding );

    GIVEN( "Log Lines of every length, with tabs and carriage returns, ending in a line feed" )
    {
        const auto bytes = encode( generatedText( 290, 120, true ) );
        writeLogFile( path, bytes );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), bytes, encoded );
    }

    GIVEN( "Log Lines not ending in a line feed" )
    {
        const auto bytes = encode( generatedText( 291, 120, false ) );
        writeLogFile( path, bytes );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), bytes, encoded );
    }

    GIVEN( "a Log Line spanning many blocks between short ones" )
    {
        QString text = "short\tone\n";
        for ( int character = 0; character < 3000; ++character ) {
            text += character % 7 == 0 ? QChar( '\t' ) : QChar( 'a' + character % 26 );
        }
        text += "\nand\ta short one after it\n";
        const auto bytes = encode( text );
        writeLogFile( path, bytes );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), bytes, encoded );
    }

    GIVEN( "a single Log Line" )
    {
        const auto terminated = GENERATE( true, false );
        const auto bytes = encode( terminated ? "one\tline\n" : "one\tline" );
        writeLogFile( path, bytes );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), bytes, encoded );
    }

    GIVEN( "an empty Log File" )
    {
        writeLogFile( path, {} );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), {}, encoded );
    }

    GIVEN( "line feeds, tabs and zero bytes at every alignment" )
    {
        const auto bytes = delimiterNoise( 292, 1500 );
        writeLogFile( path, bytes );
        requireIndexOfWholeScan( indexInBlocks( path, codec, blockSize ), bytes, encoded );
    }
}

SCENARIO( "Indexing in blocks digests the Log File as indexing it in one block does",
          "[indexing][blocks]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( "generated.log" );
    const auto bytes = generatedText( 293, 400, true ).toUtf8();
    writeLogFile( path, bytes );
    auto* codec = QTextCodec::codecForName( "UTF-8" );

    const auto whole = indexInBlocks( path, codec, DefaultIndexingBlockSize );
    const auto blocks = indexInBlocks( path, codec, GENERATE( qint64{ 7 }, qint64{ 4096 } ) );

    REQUIRE( blocks.hash.size == whole.hash.size );
    REQUIRE( blocks.hash.fullDigest == whole.hash.fullDigest );
    REQUIRE( blocks.hash.headerSize == whole.hash.headerSize );
    REQUIRE( blocks.hash.headerDigest == whole.hash.headerDigest );
    REQUIRE( blocks.hash.tailOffset == whole.hash.tailOffset );
    REQUIRE( blocks.hash.tailSize == whole.hash.tailSize );
    REQUIRE( blocks.hash.tailDigest == whole.hash.tailDigest );
}

SCENARIO( "The read buffer setting bounds the blocks read ahead, in MiB", "[indexing][blocks]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( "generated.log" );

    GIVEN( "a Log File of many 256 KiB blocks" )
    {
        // About 8 MiB.
        QByteArray bytes;
        while ( bytes.size() < 8 * 1024 * 1024 ) {
            bytes += generatedText( static_cast<std::uint32_t>( bytes.size() ), 50, true ).toUtf8();
        }
        writeLogFile( path, bytes );
        auto* codec = QTextCodec::codecForName( "UTF-8" );
        const Encoded utf8{ "UTF-8", 1, 0 };
        constexpr qint64 BlockSize = 256 * 1024;

        WHEN( "it is indexed with a read buffer of 1 MiB" )
        {
            const auto run = indexInBlocks( path, codec, BlockSize, 1 );

            THEN( "no more blocks than fit in 1 MiB are ever allocated, and they are reused" )
            {
                REQUIRE( run.blockBuffers >= 1 );
                REQUIRE( run.blockBuffers * BlockSize <= 1024 * 1024 );
                requireIndexOfWholeScan( run, bytes, utf8 );
            }
        }

        WHEN( "it is indexed with a read buffer of 2 MiB" )
        {
            const auto run = indexInBlocks( path, codec, BlockSize, 2 );

            THEN( "no more blocks than fit in 2 MiB are ever allocated" )
            {
                REQUIRE( run.blockBuffers * BlockSize <= 2 * 1024 * 1024 );
                requireIndexOfWholeScan( run, bytes, utf8 );
            }
        }

        WHEN( "it is indexed with a read buffer smaller than a block" )
        {
            const auto run = indexInBlocks( path, codec, 4 * 1024 * 1024, 1 );

            THEN( "one block is read at a time" )
            {
                REQUIRE( run.blockBuffers == 1 );
                requireIndexOfWholeScan( run, bytes, utf8 );
            }
        }
    }
}
