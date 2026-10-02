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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include "textencoding.h"
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include "test_policies.h"

#include "fake_run_control.h"
#include "indexingblocks.h"
#include "indexoperation.h"
#include "linetypes.h"

// Indexing reads a Log File in blocks and parses them in parallel, so the
// Log Lines crossing from one block into the next are where it can go wrong
// (#290). These tests index generated Log Files in blocks of a few bytes, so
// that nearly every Log Line crosses a block boundary, and hold the Index to
// what a scan of the whole Log File, byte by byte, finds.

namespace {

constexpr qint64 DefaultIndexingBlockSize = IndexingBlockPlan::DefaultBlockSize;

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

IndexingRun indexInBlocks( const QString& path, const TextEncoding* encoding, qint64 blockSize,
                           int readBufferSizeMb = 16 )
{
    auto policy = testSettingsPolicies().indexing;
    policy.readBufferSizeMb = readBufferSizeMb;

    IndexingRun run;

    // The block size and the one thing these tests watch -- how many block
    // buffers the run allocated -- are planned here, where the run is asked
    // for, and nowhere in the indexing itself (#335).
    IndexingBlockPlan blockPlan{ .blockSize = blockSize,
                                 .blockBuffersAllocated
                                 = [ &run ]( qint64 buffers ) { run.blockBuffers = buffers; } };

    auto data = std::make_shared<IndexingData>();
    const FakeRunControl indexRun;
    FullIndexOperation operation{
        path, data, indexRun, policy, FullIndexRequest::Automatic, encoding, std::move( blockPlan )
    };
    REQUIRE( std::get<LoadingStatus>( operation.run().status ) == LoadingStatus::Successful );

    IndexingData::ConstAccessor accessor{ data.get() };
    for ( auto line = 0u; line < accessor.getNbLines().get(); ++line ) {
        run.endOfLines.push_back( accessor.getEndOfLineOffset( LineNumber( line ) ).get() );
    }
    run.maxLength = accessor.getMaxLength().get();
    run.hash = accessor.getHash();
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
    auto* codec = TextEncoding::forName( encoded.encoding );
    REQUIRE( codec != nullptr );

    const auto encode = [ codec ]( const QString& text ) { return codec->fromUnicode( text ); };

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
    auto* codec = TextEncoding::forName( "UTF-8" );

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
        auto* codec = TextEncoding::forName( "UTF-8" );
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

// While the encoding guess is provisional, parsing a block also finds its
// first byte beyond ASCII (#657), in the same pass that finds its line feeds
// and tabs (#701). The scanner reads 16 bytes at a time and the rest of a
// block byte by byte, so the bytes beyond ASCII are put at every offset of
// blocks of every size around those.

namespace {

// ASCII Log Lines with tabs, size bytes of them.
QByteArray asciiBytes( qint64 size )
{
    static constexpr char Text[] = "Ping\t42 ok\nlonger Log Line\tagain\n";
    QByteArray bytes( size, Qt::Uninitialized );
    for ( qint64 at = 0; at < size; ++at ) {
        bytes[ at ] = Text[ static_cast<std::size_t>( at ) % ( sizeof( Text ) - 1 ) ];
    }
    return bytes;
}

std::unique_ptr<indexing_blocks::IndexingBlock> blockOf( const QByteArray& bytes,
                                                         bool findBeyondAscii )
{
    const auto size = static_cast<std::int64_t>( bytes.size() );
    auto block = std::make_unique<indexing_blocks::IndexingBlock>( std::max<qint64>( size, 1 ) );
    std::copy( bytes.begin(), bytes.end(), block->bytes() );
    block->size = size;
    block->findBeyondAscii = findBeyondAscii;
    return block;
}

std::optional<std::int64_t> firstBeyondAsciiParsed( const QByteArray& bytes )
{
    auto block = blockOf( bytes, true );
    indexing_blocks::parseBlock( *block );
    return block->firstBeyondAscii;
}

// The offsets of the line feeds and tabs the scanner finds, and whether it
// saw a byte beyond ASCII, once it found all of them.
struct Scanned {
    std::vector<std::size_t> hits;
    bool sawByteBeyondAscii = false;
};

Scanned scan( const QByteArray& bytes, std::size_t from = 0 )
{
    const auto size = static_cast<std::size_t>( bytes.size() );
    indexing_blocks::LineFeedAndTabScanner scanner( bytes.data(), size, from );
    Scanned scanned;
    for ( auto hit = scanner.next(); hit < size; hit = scanner.next() ) {
        scanned.hits.push_back( hit );
    }
    scanned.sawByteBeyondAscii = scanner.sawByteBeyondAscii();
    return scanned;
}

std::vector<std::size_t> lineFeedsAndTabs( const QByteArray& bytes, std::size_t from = 0 )
{
    std::vector<std::size_t> hits;
    for ( auto at = from; at < static_cast<std::size_t>( bytes.size() ); ++at ) {
        if ( bytes[ static_cast<qsizetype>( at ) ] == '\n'
             || bytes[ static_cast<qsizetype>( at ) ] == '\t' ) {
            hits.push_back( at );
        }
    }
    return hits;
}

constexpr qint64 LargestSmallBlock = 100;
constexpr char BeyondAscii[] = { '\x80', '\xC3', '\xFF' };

} // namespace

SCENARIO( "Parsing a block finds its first byte beyond ASCII while asked to",
          "[indexing][blocks][encoding]" )
{
    GIVEN( "blocks of ASCII alone" )
    {
        THEN( "none is found, whatever the size of the block" )
        {
            for ( qint64 size = 0; size <= LargestSmallBlock; ++size ) {
                CAPTURE( size );
                REQUIRE_FALSE( firstBeyondAsciiParsed( asciiBytes( size ) ) );
            }
            REQUIRE_FALSE( firstBeyondAsciiParsed( asciiBytes( 4099 ) ) );
        }
    }

    GIVEN( "blocks with a byte beyond ASCII" )
    {
        const auto beyond
            = GENERATE( from_range( std::begin( BeyondAscii ), std::end( BeyondAscii ) ) );
        CAPTURE( static_cast<int>( static_cast<unsigned char>( beyond ) ) );

        THEN( "it is found at every offset of blocks of every size" )
        {
            for ( qint64 size = 1; size <= LargestSmallBlock; ++size ) {
                for ( qint64 offset = 0; offset < size; ++offset ) {
                    CAPTURE( size, offset );
                    auto bytes = asciiBytes( size );
                    bytes[ offset ] = beyond;
                    REQUIRE( firstBeyondAsciiParsed( bytes ) == offset );
                }
            }
        }

        THEN( "it is found anywhere in a large block" )
        {
            for ( const qint64 offset : { 0, 15, 16, 4000, 4079, 4080, 4095, 4096, 4098 } ) {
                CAPTURE( offset );
                auto bytes = asciiBytes( 4099 );
                bytes[ offset ] = beyond;
                REQUIRE( firstBeyondAsciiParsed( bytes ) == offset );
            }
        }

        THEN( "only the first of several is found" )
        {
            auto bytes = asciiBytes( 300 );
            for ( const qint64 offset : { 290, 40, 37, 200 } ) {
                bytes[ offset ] = beyond;
            }
            REQUIRE( firstBeyondAsciiParsed( bytes ) == 37 );
        }

        WHEN( "parsing is not asked to look for it" )
        {
            auto bytes = asciiBytes( 64 );
            bytes[ 20 ] = beyond;
            auto block = blockOf( bytes, false );
            indexing_blocks::parseBlock( *block );

            THEN( "none is found" )
            {
                REQUIRE_FALSE( block->firstBeyondAscii );
            }
        }

        WHEN( "the block is read and parsed again with ASCII alone" )
        {
            auto bytes = asciiBytes( 64 );
            bytes[ 20 ] = beyond;
            auto block = blockOf( bytes, true );
            indexing_blocks::parseBlock( *block );
            REQUIRE( block->firstBeyondAscii == 20 );

            const auto ascii = asciiBytes( 64 );
            std::copy( ascii.begin(), ascii.end(), block->bytes() );
            indexing_blocks::parseBlock( *block );

            THEN( "none is found any more" )
            {
                REQUIRE_FALSE( block->firstBeyondAscii );
            }
        }

        THEN( "the Log Lines are found as in ASCII alone" )
        {
            auto bytes = asciiBytes( 300 );
            bytes[ 40 ] = beyond;
            auto withBeyond = blockOf( bytes, true );
            auto ascii = blockOf( asciiBytes( 300 ), true );
            indexing_blocks::parseBlock( *withBeyond );
            indexing_blocks::parseBlock( *ascii );
            REQUIRE( withBeyond->endOfLines.size() == ascii->endOfLines.size() );
            REQUIRE( withBeyond->maxLength == ascii->maxLength );
            REQUIRE( withBeyond->lastLineStart == ascii->lastLineStart );
            REQUIRE( withBeyond->lastLineWidening == ascii->lastLineWidening );
        }
    }
}

SCENARIO( "The line feed and tab scanner sees whether the bytes go beyond ASCII",
          "[indexing][blocks][encoding]" )
{
    GIVEN( "ASCII bytes of every size" )
    {
        THEN( "it finds their line feeds and tabs, and sees nothing beyond ASCII" )
        {
            for ( qint64 size = 0; size <= LargestSmallBlock; ++size ) {
                CAPTURE( size );
                const auto bytes = asciiBytes( size );
                const auto scanned = scan( bytes );
                REQUIRE( scanned.hits == lineFeedsAndTabs( bytes ) );
                REQUIRE_FALSE( scanned.sawByteBeyondAscii );
            }
        }
    }

    GIVEN( "bytes with one beyond ASCII" )
    {
        const auto beyond
            = GENERATE( from_range( std::begin( BeyondAscii ), std::end( BeyondAscii ) ) );

        THEN( "it sees it at every offset, and finds the same line feeds and tabs" )
        {
            for ( qint64 size = 1; size <= LargestSmallBlock; ++size ) {
                for ( qint64 offset = 0; offset < size; ++offset ) {
                    CAPTURE( size, offset );
                    auto bytes = asciiBytes( size );
                    bytes[ offset ] = beyond;
                    const auto scanned = scan( bytes );
                    REQUIRE( scanned.hits == lineFeedsAndTabs( bytes ) );
                    REQUIRE( scanned.sawByteBeyondAscii );
                }
            }
        }

        THEN( "it only sees the bytes from where it starts" )
        {
            auto bytes = asciiBytes( 70 );
            bytes[ 5 ] = beyond;
            for ( const std::size_t from :
                  { std::size_t{ 6 }, std::size_t{ 17 }, std::size_t{ 69 }, std::size_t{ 70 } } ) {
                CAPTURE( from );
                const auto scanned = scan( bytes, from );
                REQUIRE( scanned.hits == lineFeedsAndTabs( bytes, from ) );
                REQUIRE_FALSE( scanned.sawByteBeyondAscii );
            }
            REQUIRE( scan( bytes, 5 ).sawByteBeyondAscii );
        }
    }
}
