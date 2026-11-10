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

// The Log Line Reader (#951) against a Log File held in memory: every shape
// of read -- a range, a block for a Search, a sparse set, prefixes and UTF-8
// -- keeps the same rules: a Log Line too long to read is not read (#825), a
// carriage return ending a Log Line and the byte order marks starting it are
// no part of its text, UTF-8 is passed through.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QString>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "in_memory_block_source.h"
#include "loglinereader.h"
#include "loglinetext.h"

namespace {

template <typename Lines>
std::vector<QString> texts( const Lines& lines )
{
    std::vector<QString> result;
    for ( const auto& line : lines ) {
        if constexpr ( std::is_same_v<std::decay_t<decltype( line )>, QString> ) {
            result.push_back( line );
        }
        else {
            result.push_back( line.text );
        }
    }
    return result;
}

std::vector<QString> blockTexts( RawLines&& rawLines )
{
    return texts( rawLines.decodeLines() );
}

std::vector<std::string> utf8View( const RawLines& rawLines )
{
    std::vector<std::string> lines;
    for ( const auto line : rawLines.buildUtf8View() ) {
        lines.emplace_back( line );
    }
    return lines;
}

struct Prefix {
    QString text;
    bool cut = false;

    bool operator==( const Prefix& ) const = default;
};

std::vector<Prefix> prefixes( LogLineReader&& reader, const std::vector<LineNumber>& lines,
                              qint64 maxBytes, std::size_t maxLines = 1000 )
{
    std::vector<Prefix> read;
    std::move( reader ).readLinePrefixes( lines, maxBytes, [ & ]( QString&& text, bool cut ) {
        read.push_back( Prefix{ std::move( text ), cut } );
        return read.size() < maxLines;
    } );
    return read;
}

const QString TooLong = QString::fromLatin1( LineTooLongWarning.data(),
                                             static_cast<qsizetype>( LineTooLongWarning.size() ) );
const QString NotRead = QString::fromLatin1( LinesNotReadWarning.data(),
                                             static_cast<qsizetype>( LinesNotReadWarning.size() ) );

} // namespace

SCENARIO( "A Log Line Reader reads every shape of read alike", "[loglinereader]" )
{
    const auto file = std::make_shared<InMemoryLogFile>();
    file->appendLine( QStringLiteral( "first\tline" ) );
    file->appendLine( QStringLiteral( "second line" ) );
    file->appendLine( QStringLiteral( "third line" ) );

    const std::vector<QString> expected{ QStringLiteral( "first\tline" ),
                                         QStringLiteral( "second line" ),
                                         QStringLiteral( "third line" ) };
    const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum };

    THEN( "a range, a block, a sparse set and prefixes read the same text" )
    {
        REQUIRE( texts( file->reader().readLines( 0_lnum, 3_lcount ) ) == expected );
        REQUIRE( blockTexts( file->reader().readRawLines( 0_lnum, 3_lcount ) ) == expected );
        REQUIRE( texts( file->reader().readLinesSparse( all ) ) == expected );
        REQUIRE( texts( file->reader().readAnsiColoredLines( 0_lnum, 3_lcount ) ) == expected );
        REQUIRE( texts( file->reader().readAnsiColoredLinesSparse( all ) ) == expected );
        REQUIRE( file->reader().readUtf8LinesSparse( all )
                 == "first\tline\nsecond line\nthird line\n" );
        REQUIRE( prefixes( file->reader(), all, 1000 )
                 == std::vector<Prefix>{ { expected[ 0 ], false },
                                         { expected[ 1 ], false },
                                         { expected[ 2 ], false } } );
    }

    THEN( "the expanded reads expand tabs" )
    {
        const auto expanded = untabify( QStringLiteral( "first\tline" ) );
        REQUIRE( file->reader().readExpandedLines( 0_lnum, 1_lcount ).front() == expanded );
        const std::vector<LineNumber> first{ 0_lnum };
        REQUIRE( file->reader().readExpandedLinesSparse( first ).front() == expanded );
    }

    THEN( "a range past the last Log Line reads as warnings, a block as nothing" )
    {
        const auto lines = texts( file->reader().readLines( 2_lnum, 2_lcount ) );
        REQUIRE( lines.size() == 2 );
        REQUIRE( lines[ 1 ] == NotRead );
        REQUIRE( file->reader().readRawLines( 2_lnum, 2_lcount ).endOfLines.empty() );
    }

    THEN( "a block bounded by its bytes ends before the Log Line that would pass the bound" )
    {
        const auto rawLines = file->reader().readRawLines( 0_lnum, 3_lcount, 20 );
        REQUIRE( rawLines.endOfLines.size() == 1 );
        const auto alone = file->reader().readRawLines( 1_lnum, 2_lcount, 1 );
        REQUIRE( alone.endOfLines.size() == 1 );
    }

    THEN( "a prefix read reads no more than its bound, and stops when told" )
    {
        REQUIRE( prefixes( file->reader(), all, 5 )
                 == std::vector<Prefix>{ { QStringLiteral( "first" ), true },
                                         { QStringLiteral( "secon" ), true },
                                         { QStringLiteral( "third" ), true } } );
        REQUIRE( prefixes( file->reader(), all, 1000, 1 ).size() == 1 );
    }

    THEN( "every read leaves the Log File's bytes as it found them" )
    {
        (void)file->reader().readLines( 0_lnum, 3_lcount );
        (void)file->reader().readLinesSparse( all );
        (void)prefixes( file->reader(), all, 3 );
        REQUIRE( file->openReads() == 0 );
    }
}

SCENARIO( "A Log Line Reader reads a Log Line too long to read as the warning, without its bytes",
          "[loglinereader]" )
{
    // A gibibyte the Log File held in memory does not hold: reading any of it
    // would be recorded (#825).
    const auto file = std::make_shared<InMemoryLogFile>();
    file->appendLine( QStringLiteral( "before" ) );
    file->appendTooLongLine( MaxLogLineLength );
    file->appendLine( QStringLiteral( "after" ) );

    const std::vector<QString> expected{ QStringLiteral( "before" ), TooLong,
                                         QStringLiteral( "after" ) };
    const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum };

    THEN( "a range reads it as the warning" )
    {
        REQUIRE( texts( file->reader().readLines( 0_lnum, 3_lcount ) ) == expected );
        REQUIRE( texts( file->reader().readAnsiColoredLines( 0_lnum, 3_lcount ) ) == expected );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a block holds its line feed alone" )
    {
        const auto rawLines = file->reader().readRawLines( 0_lnum, 3_lcount );
        REQUIRE( rawLines.buffer.size() == std::string( "before\n\nafter\n" ).size() );
        REQUIRE( rawLines.tooLongLines.size() == 1 );
        REQUIRE( rawLines.tooLongLines.front() == 1 );
        REQUIRE(
            utf8View( rawLines )
            == std::vector<std::string>{ "before", std::string( LineTooLongWarning ), "after" } );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a block that starts with it holds it" )
    {
        const auto rawLines = file->reader().readRawLines( 1_lnum, 2_lcount, 1 );
        REQUIRE( rawLines.endOfLines.size() == 1 );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a sparse read and a UTF-8 read read it as the warning" )
    {
        REQUIRE( texts( file->reader().readLinesSparse( all ) ) == expected );
        REQUIRE( file->reader().readUtf8LinesSparse( all )
                 == "before\n" + std::string( LineTooLongWarning ) + "\nafter\n" );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a prefix read reads no more of it than its bound" )
    {
        const std::vector<LineNumber> tooLong{ 1_lnum };
        const auto read = prefixes( file->reader(), tooLong, 4 );
        REQUIRE( read.size() == 1 );
        REQUIRE( read.front().cut );
        for ( const auto& fileRead : file->reads() ) {
            REQUIRE( fileRead.size <= 4 );
        }
    }
}

SCENARIO( "A Log Line Reader leaves out a carriage return ending a Log Line", "[loglinereader]" )
{
    const auto file = std::make_shared<InMemoryLogFile>();
    file->appendBytes( "ends in CR LF\r\n"
                       "holds a lone\rCR\n"
                       "ends in two\r\r\n" );

    const std::vector<QString> expected{ QStringLiteral( "ends in CR LF" ),
                                         QStringLiteral( "holds a lone\rCR" ),
                                         QStringLiteral( "ends in two\r" ) };
    const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum };

    REQUIRE( texts( file->reader().readLines( 0_lnum, 3_lcount ) ) == expected );
    REQUIRE( utf8View( file->reader().readRawLines( 0_lnum, 3_lcount ) )
             == std::vector<std::string>{ "ends in CR LF", "holds a lone\rCR", "ends in two\r" } );
    REQUIRE( texts( file->reader().readLinesSparse( all ) ) == expected );
    REQUIRE( file->reader().readUtf8LinesSparse( all )
             == "ends in CR LF\nholds a lone\rCR\nends in two\r\n" );
    REQUIRE( texts( file->reader().readAnsiColoredLinesSparse( all ) ) == expected );
    REQUIRE( prefixes( file->reader(), all, 1000 ).front().text == expected.front() );
}

SCENARIO( "A Log Line Reader leaves out the byte order marks starting a Log Line",
          "[loglinereader]" )
{
    GIVEN( "a UTF-8 Log File that starts with one, and a Log Line after that starts with two" )
    {
        const auto file = std::make_shared<InMemoryLogFile>();
        file->appendBytes( "\xEF\xBB\xBF"
                           "first\n"
                           "second\n"
                           "\xEF\xBB\xBF\xEF\xBB\xBF"
                           "third\n" );

        const std::vector<QString> expected{ QStringLiteral( "first" ), QStringLiteral( "second" ),
                                             QStringLiteral( "third" ) };
        const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum };

        THEN( "every shape of read reads the Log Lines without them" )
        {
            REQUIRE( texts( file->reader().readLines( 0_lnum, 3_lcount ) ) == expected );
            REQUIRE( utf8View( file->reader().readRawLines( 0_lnum, 3_lcount ) )
                     == std::vector<std::string>{ "first", "second", "third" } );
            REQUIRE( texts( file->reader().readLinesSparse( all ) ) == expected );
            REQUIRE( file->reader().readUtf8LinesSparse( all ) == "first\nsecond\nthird\n" );
            REQUIRE( texts( file->reader().readAnsiColoredLinesSparse( all ) ) == expected );
            const std::vector<LineNumber> third{ 2_lnum };
            REQUIRE( prefixes( file->reader(), third, 1000 ).front().text == expected[ 2 ] );
        }
    }
}

SCENARIO( "A Log Line Reader passes UTF-8 through", "[loglinereader]" )
{
    const std::string utf8 = "gr\xC3\xBC\xC3\x9F"
                             "e \xE6\x97\xA5\xE6\x9C\xAC";
    const auto text = QString::fromUtf8( utf8 );

    GIVEN( "a UTF-8 Log File" )
    {
        const auto file = std::make_shared<InMemoryLogFile>();
        file->appendBytes( utf8 + "\n" + utf8 + "\n" );
        const std::vector<LineNumber> both{ 1_lnum, 0_lnum };

        THEN( "its UTF-8 text is its bytes" )
        {
            REQUIRE( file->reader().readUtf8LinesSparse( both ) == utf8 + "\n" + utf8 + "\n" );
            REQUIRE( utf8View( file->reader().readRawLines( 0_lnum, 2_lcount ) )
                     == std::vector<std::string>{ utf8, utf8 } );
        }

        THEN( "its text is its bytes decoded" )
        {
            REQUIRE( texts( file->reader().readLines( 0_lnum, 2_lcount ) )
                     == std::vector<QString>{ text, text } );
            REQUIRE( texts( file->reader().readLinesSparse( both ) )
                     == std::vector<QString>{ text, text } );
        }

        THEN( "a prefix cut inside a character ends before it" )
        {
            const std::vector<LineNumber> first{ 0_lnum };
            const auto read = prefixes( file->reader(), first, 3 );
            REQUIRE( read == std::vector<Prefix>{ { QStringLiteral( "gr" ), true } } );
        }
    }

    GIVEN( "a Latin-1 Log File" )
    {
        const auto file = std::make_shared<InMemoryLogFile>( "ISO-8859-1" );
        file->appendLine( QStringLiteral( "grüße" ) );
        const std::vector<LineNumber> first{ 0_lnum };

        THEN( "its UTF-8 text is its text converted" )
        {
            REQUIRE( file->reader().readUtf8LinesSparse( first )
                     == "gr\xC3\xBC\xC3\x9F"
                        "e\n" );
            REQUIRE( file->reader().readLines( 0_lnum, 1_lcount ).front()
                     == QStringLiteral( "grüße" ) );
        }
    }
}

SCENARIO( "A Log Line Reader reads UTF-16 Log Lines", "[loglinereader]" )
{
    const auto* encoding = GENERATE( "UTF-16LE", "UTF-16BE" );
    CAPTURE( encoding );

    const auto file = std::make_shared<InMemoryLogFile>( encoding );
    const auto isLittleEndian = std::string_view( encoding ) == "UTF-16LE";
    file->appendBytes( isLittleEndian ? std::string_view( "\xFF\xFE", 2 )
                                      : std::string_view( "\xFE\xFF", 2 ) );
    file->appendLine( QStringLiteral( "first 日本" ) );
    file->appendLine( QStringLiteral( "second\r" ) );
    file->appendTooLongLine( MaxLogLineLength + 1 );
    file->appendLine( QStringLiteral( "﻿fourth" ) );

    const std::vector<QString> expected{ QStringLiteral( "first 日本" ), QStringLiteral( "second" ),
                                         TooLong, QStringLiteral( "fourth" ) };
    const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum, 3_lnum };

    THEN( "a range and a sparse set read their text, without the byte order marks" )
    {
        REQUIRE( texts( file->reader().readLines( 0_lnum, 4_lcount ) ) == expected );
        REQUIRE( texts( file->reader().readLinesSparse( all ) ) == expected );
        REQUIRE( texts( file->reader().readAnsiColoredLinesSparse( all ) ) == expected );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a block holds the line feed alone of the Log Line too long to read" )
    {
        const auto rawLines = file->reader().readRawLines( 0_lnum, 4_lcount );
        REQUIRE( rawLines.tooLongLines.size() == 1 );
        REQUIRE( rawLines.tooLongLines.front() == 2 );
        REQUIRE( blockTexts( file->reader().readRawLines( 0_lnum, 4_lcount ) ) == expected );
        REQUIRE( utf8View( rawLines )
                 == std::vector<std::string>{ "first \xE6\x97\xA5\xE6\x9C\xAC", "second",
                                              std::string( LineTooLongWarning ), "fourth" } );
        REQUIRE( !file->readTooLongBytes() );
    }

    THEN( "a UTF-8 read converts their text" )
    {
        REQUIRE( file->reader().readUtf8LinesSparse( all )
                 == "first \xE6\x97\xA5\xE6\x9C\xAC\nsecond\n" + std::string( LineTooLongWarning )
                        + "\nfourth\n" );
    }

    THEN( "a prefix read counts its bound in bytes" )
    {
        const std::vector<LineNumber> second{ 1_lnum };
        REQUIRE( prefixes( file->reader(), second, 6 )
                 == std::vector<Prefix>{ { QStringLiteral( "sec" ), true } } );
    }
}

SCENARIO( "A Log Line Reader reads a sparse set of Log Lines across reads of the Log File",
          "[loglinereader]" )
{
    // Far enough apart that no read of the Log File holds two of them
    // (SparseReadLimits).
    constexpr int LogLineCount = 1000;
    const auto file = std::make_shared<InMemoryLogFile>();
    for ( int line = 0; line < LogLineCount; ++line ) {
        file->appendLine( QStringLiteral( "Log Line %1 %2" )
                              .arg( line )
                              .arg( QString( 100, QLatin1Char( '.' ) ) ) );
    }
    const auto lineText = []( int line ) {
        return QStringLiteral( "Log Line %1 %2" )
            .arg( line )
            .arg( QString( 100, QLatin1Char( '.' ) ) );
    };

    const std::vector<LineNumber> lines{ 999_lnum, 3_lnum, 500_lnum, 4_lnum, 500_lnum, 1000_lnum };

    THEN( "each Log Line asked for is read where it was asked, in reads of their own" )
    {
        const auto read = texts( file->reader().readLinesSparse( lines ) );
        REQUIRE( read
                 == std::vector<QString>{ lineText( 999 ), lineText( 3 ), lineText( 500 ),
                                          lineText( 4 ), lineText( 500 ), NotRead } );
        // Log Lines 3 and 4 share a read; 500 and 999 have one each.
        REQUIRE( file->reads().size() == 3 );
        REQUIRE( file->openReads() == 0 );
    }

    THEN( "a UTF-8 read reads them in the order asked" )
    {
        const auto read = file->reader().readUtf8LinesSparse( lines );
        const auto expected
            = ( lineText( 999 ) + '\n' + lineText( 3 ) + '\n' + lineText( 500 ) + '\n'
                + lineText( 4 ) + '\n' + lineText( 500 ) + '\n' + NotRead + '\n' )
                  .toStdString();
        REQUIRE( read == expected );
    }

    THEN( "a block reads the Log Lines from the first one on in one read" )
    {
        const auto rawLines = file->reader().readRawLines( 3_lnum, 500_lcount );
        REQUIRE( rawLines.endOfLines.size() == 500 );
        REQUIRE( file->reads().size() == 1 );
        REQUIRE( blockTexts( file->reader().readRawLines( 499_lnum, 2_lcount ) )
                 == std::vector<QString>{ lineText( 499 ), lineText( 500 ) } );
    }
}

SCENARIO( "A Log Line Reader reads under the Decoding Policy it was built with", "[loglinereader]" )
{
    const auto file = std::make_shared<InMemoryLogFile>();
    file->appendLine( QStringLiteral( "\x1B[31mred\x1B[0m text" ) );
    const std::vector<LineNumber> first{ 0_lnum };

    GIVEN( "a Decoding Policy that hides ANSI color sequences" )
    {
        file->setDecodingPolicy( DecodingPolicy{ .hideAnsiColorSequences = true } );

        THEN( "every shape of read hides them" )
        {
            const auto hidden = QStringLiteral( "red text" );
            REQUIRE( file->reader().readLines( 0_lnum, 1_lcount ).front() == hidden );
            REQUIRE( file->reader().readLinesSparse( first ).front() == hidden );
            REQUIRE( file->reader().readUtf8LinesSparse( first ) == "red text\n" );
            REQUIRE( prefixes( file->reader(), first, 1000 ).front().text == hidden );
        }
    }

    GIVEN( "one that shows them" )
    {
        THEN( "the colored reads hand them out as colors either way" )
        {
            const auto colored = file->reader().readAnsiColoredLines( 0_lnum, 1_lcount ).front();
            REQUIRE( colored.text == QStringLiteral( "red text" ) );
            REQUIRE( !colored.spans.empty() );
            REQUIRE( file->reader().readLines( 0_lnum, 1_lcount ).front()
                     == QStringLiteral( "\x1B[31mred\x1B[0m text" ) );
        }
    }
}
