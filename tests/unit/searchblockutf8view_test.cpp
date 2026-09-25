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

// A Search matches the UTF-8 view of a block of raw Log Lines. Whatever the
// Encoding of the Log File, each Log Line of the view is the Log Line as it is
// decoded, in UTF-8 (#291).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "textencoding.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "ansicolorsequences.h"
#include "searchblocksource.h"

namespace {

// Raw Log Lines holding the given bytes, each entry one Log Line with its line
// feed (or without one, for a last Log Line still being written).
RawLines rawLinesOfBytes( const std::vector<QByteArray>& lines, const char* encoding,
                          bool hideAnsiColorSequences )
{
    auto* const codec = TextEncoding::forName( encoding );
    REQUIRE( codec != nullptr );

    RawLines rawLines;
    rawLines.startLine = LineNumber{ 0 };
    for ( const auto& line : lines ) {
        rawLines.buffer.insert( rawLines.buffer.end(), line.begin(), line.end() );
        rawLines.endOfLines.push_back( static_cast<qint64>( rawLines.buffer.size() ) );
    }
    rawLines.textDecoder.decoder = codec->makeDecoder();
    rawLines.textDecoder.encodingParams = EncodingParameters( codec );
    rawLines.hideAnsiColorSequences = hideAnsiColorSequences;
    return rawLines;
}

// The given Log Lines encoded as a Log File in the encoding holds them, one
// line feed after each.
std::vector<QByteArray> encodedLines( const QStringList& lines, const char* encoding )
{
    auto* const codec = TextEncoding::forName( encoding );
    std::vector<QByteArray> encoded;
    for ( const auto& line : lines ) {
        const auto text = line + '\n';
        encoded.push_back( codec->fromUnicode( text ) );
    }
    return encoded;
}

std::vector<std::string> utf8Lines( const RawLines& rawLines )
{
    const auto view = rawLines.buildUtf8View();
    return { view.begin(), view.end() };
}

// How a Search saw the block before #291: the whole block decoded to a QString,
// its ANSI color sequences removed, converted to UTF-8 and split at each line
// feed -- and each Log Line then trimmed to its text, without the carriage
// return that ends it or a byte order mark that starts it (#522).
std::vector<std::string> decodedThenConvertedLines( const std::vector<QByteArray>& lines,
                                                    const char* encoding,
                                                    bool hideAnsiColorSequences )
{
    QByteArray block;
    for ( const auto& line : lines ) {
        block += line;
    }

    auto text = TextEncoding::forName( encoding )->toUnicode( block );
    if ( hideAnsiColorSequences ) {
        removeAnsiColorSequences( text );
    }
    const auto utf8 = text.toUtf8();

    const auto asText = []( std::string_view line ) {
        if ( line.ends_with( '\r' ) ) {
            line.remove_suffix( 1 );
        }
        if ( line.starts_with( "\xEF\xBB\xBF" ) ) {
            line.remove_prefix( 3 );
        }
        return std::string( line );
    };

    std::vector<std::string> split;
    std::string_view rest( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
    for ( auto lineFeed = rest.find( '\n' ); lineFeed != std::string_view::npos;
          lineFeed = rest.find( '\n' ) ) {
        split.push_back( asText( rest.substr( 0, lineFeed ) ) );
        rest.remove_prefix( lineFeed + 1 );
    }
    if ( !rest.empty() ) {
        split.push_back( asText( rest ) );
    }
    return split;
}

QStringList logLinesFor( const char* encoding )
{
    QStringList lines{
        QStringLiteral( "2026-09-17 12:34:56 INFO plain line" ),
        QStringLiteral( "\x1B[31mERROR\x1B[0m: disk full" ),
        QString(),
        QStringLiteral( "caf\u00e9 \u00fcber \u00ff and a carriage return\r" ),
        QStringLiteral( "\x1B[1;32mOK\x1B[K done, caf\u00e9" ),
        QStringLiteral( "last plain line" ),
    };

    const QString beyondLatin1 = QStringLiteral( "euro \u20ac and a clef \U0001D11E" );
    if ( TextEncoding::forName( encoding )->canEncode( beyondLatin1 ) ) {
        lines.insert( 2, beyondLatin1 );
    }
    return lines;
}

} // namespace

SCENARIO( "A block's UTF-8 view has every Log Line as it is decoded", "[search][encoding]" )
{
    const auto encoding = GENERATE( "UTF-8", "UTF-16LE", "UTF-16BE", "ISO-8859-1", "windows-1252" );
    const auto hideAnsiColorSequences = GENERATE( false, true );

    GIVEN( std::string( "a block of " ) + encoding + " Log Lines, "
           + ( hideAnsiColorSequences ? "hiding" : "showing" ) + " ANSI color sequences" )
    {
        const auto lines = logLinesFor( encoding );
        const auto rawLines
            = rawLinesOfBytes( encodedLines( lines, encoding ), encoding, hideAnsiColorSequences );

        THEN( "each Log Line of the view is the Log Line in UTF-8, without its line feed or the "
              "carriage return before it" )
        {
            std::vector<std::string> expected;
            for ( auto line : lines ) {
                if ( hideAnsiColorSequences ) {
                    removeAnsiColorSequences( line );
                }
                if ( line.endsWith( QChar::CarriageReturn ) ) {
                    line.chop( 1 );
                }
                expected.push_back( line.toUtf8().toStdString() );
            }
            REQUIRE( utf8Lines( rawLines ) == expected );
        }
    }

    GIVEN( std::string( "a block of " ) + encoding + " Log Lines whose last has no line feed yet" )
    {
        auto bytes = encodedLines( logLinesFor( encoding ), encoding );
        const auto lineFeedWidth
            = EncodingParameters( TextEncoding::forName( encoding ) ).lineFeedWidth;
        bytes.back().chop( lineFeedWidth );
        const auto rawLines = rawLinesOfBytes( bytes, encoding, hideAnsiColorSequences );

        THEN( "the view has the same Log Lines a Search saw before" )
        {
            const auto view = utf8Lines( rawLines );
            REQUIRE( view.size() == rawLines.endOfLines.size() );
            REQUIRE( view == decodedThenConvertedLines( bytes, encoding, hideAnsiColorSequences ) );
        }
    }
}

SCENARIO( "A block's UTF-8 view of UTF-16 Log Lines that are not valid UTF-16",
          "[search][encoding]" )
{
    const auto hideAnsiColorSequences = GENERATE( false, true );

    GIVEN( "UTF-16LE Log Lines with a lone high and a lone low surrogate" )
    {
        const std::vector<QByteArray> bytes{
            QByteArray( "a\0\x00\xD8"
                        "b\0\n\0",
                        8 ),
            QByteArray( "\x1B\0[\0"
                        "1\0m\0"
                        "c\0\x00\xDC\n\0",
                        14 ),
            QByteArray( "f\0i\0\n\0", 6 ),
        };
        const auto rawLines = rawLinesOfBytes( bytes, "UTF-16LE", hideAnsiColorSequences );

        THEN( "every Log Line is in the view, the lone surrogates dropped as decoding drops them" )
        {
            const auto view = utf8Lines( rawLines );
            REQUIRE( view.size() == 3 );
            REQUIRE( view
                     == decodedThenConvertedLines( bytes, "UTF-16LE", hideAnsiColorSequences ) );
            REQUIRE( view[ 0 ] == "ab" );
            REQUIRE( view[ 2 ] == "fi" );
        }
    }

    GIVEN( "UTF-16BE Log Lines with a lone high surrogate" )
    {
        const std::vector<QByteArray> bytes{
            QByteArray( "\0a\xD8\x00\0b\0\n", 8 ),
            QByteArray( "\0c\0\n", 4 ),
        };
        const auto rawLines = rawLinesOfBytes( bytes, "UTF-16BE", hideAnsiColorSequences );

        THEN( "every Log Line is in the view, the lone surrogate dropped as decoding drops it" )
        {
            const auto view = utf8Lines( rawLines );
            REQUIRE( view
                     == decodedThenConvertedLines( bytes, "UTF-16BE", hideAnsiColorSequences ) );
            REQUIRE( view == std::vector<std::string>{ "ab", "c" } );
        }
    }

    GIVEN( "UTF-16LE Log Lines whose last is cut in the middle of a code unit" )
    {
        const std::vector<QByteArray> bytes{
            QByteArray( "a\0\n\0", 4 ),
            QByteArray( "b\0c", 3 ),
        };
        const auto rawLines = rawLinesOfBytes( bytes, "UTF-16LE", hideAnsiColorSequences );

        THEN( "the view has the same Log Lines a Search saw before" )
        {
            REQUIRE( utf8Lines( rawLines )
                     == decodedThenConvertedLines( bytes, "UTF-16LE", hideAnsiColorSequences ) );
        }
    }

    GIVEN( "a UTF-16LE block that starts with a byte order mark" )
    {
        const std::vector<QByteArray> bytes{
            QByteArray( "\xFF\xFE"
                        "a\0\n\0",
                        6 ),
            QByteArray( "b\0\n\0", 4 ),
        };
        const auto rawLines = rawLinesOfBytes( bytes, "UTF-16LE", hideAnsiColorSequences );

        THEN( "the first Log Line is in the view without it, as it is decoded" )
        {
            REQUIRE( utf8Lines( rawLines ) == std::vector<std::string>{ "a", "b" } );
        }
    }
}

namespace {

// Where the Log Lines of a block carry byte order marks.
enum class ByteOrderMarks { None, OnTheFirstLogLine, OnEveryLogLine };

std::string asStd( const QString& text )
{
    return text.toUtf8().toStdString();
}

} // namespace

// A Search matches a Log Line as the user sees it (#522): the UTF-8 view of a
// Log Line is its displayed text before untabifying -- what the block decodes
// it to -- without the carriage return that ends it or the byte order mark
// that starts it, in every Encoding the view is converted in directly and in
// the one it is decoded in as a whole block.
SCENARIO( "A block's UTF-8 view of a Log Line is its displayed text", "[search][encoding]" )
{
    const auto* const encoding
        = GENERATE( "UTF-8", "US-ASCII", "ISO-8859-1", "UTF-16LE", "UTF-16BE", "windows-1252" );
    const auto lineEnd = GENERATE( as<std::string>{}, "\n", "\r\n" );
    const auto byteOrderMarks = GENERATE( ByteOrderMarks::None, ByteOrderMarks::OnTheFirstLogLine,
                                          ByteOrderMarks::OnEveryLogLine );
    const auto hideAnsiColorSequences = GENERATE( false, true );

    const auto* const codec = TextEncoding::forName( encoding );
    REQUIRE( codec != nullptr );
    const QString byteOrderMark( QChar::ByteOrderMark );
    // Only a Unicode Encoding has a byte order mark.
    const bool isUnicode = QByteArray( encoding ).startsWith( "UTF-" );
    if ( byteOrderMarks != ByteOrderMarks::None && !isUnicode ) {
        return;
    }

    QStringList texts{
        QStringLiteral( "alpha foo" ),
        QStringLiteral( "beta bar" ),
        QStringLiteral( "\x1B[31mgamma\x1B[0m foo" ),
        QString(),
    };
    const auto beyondAscii = QStringLiteral( "café foo" );
    if ( codec->canEncode( beyondAscii ) && QByteArray( encoding ) != "US-ASCII" ) {
        texts.insert( 2, beyondAscii );
    }

    std::vector<QByteArray> bytes;
    for ( qsizetype index = 0; index < texts.size(); ++index ) {
        const bool startsWithByteOrderMark
            = byteOrderMarks == ByteOrderMarks::OnEveryLogLine
              || ( byteOrderMarks == ByteOrderMarks::OnTheFirstLogLine && index == 0 );
        bytes.push_back( codec->fromUnicode( ( startsWithByteOrderMark ? byteOrderMark : QString() )
                                             + texts[ index ]
                                             + QString::fromStdString( lineEnd ) ) );
    }

    GIVEN( std::string( "a block of " ) + encoding + " Log Lines ending in "
           + ( lineEnd == "\n" ? "LF" : "CRLF" ) + ", "
           + ( byteOrderMarks == ByteOrderMarks::None
                   ? "without a byte order mark"
                   : ( byteOrderMarks == ByteOrderMarks::OnTheFirstLogLine
                           ? "a byte order mark on the first"
                           : "a byte order mark on each" ) )
           + ", " + ( hideAnsiColorSequences ? "hiding" : "showing" ) + " ANSI color sequences" )
    {
        const auto rawLines = rawLinesOfBytes( bytes, encoding, hideAnsiColorSequences );

        std::vector<std::string> expected;
        for ( auto text : texts ) {
            if ( hideAnsiColorSequences ) {
                removeAnsiColorSequences( text );
            }
            expected.push_back( asStd( text ) );
        }

        THEN( "the block displays each Log Line as its text" )
        {
            std::vector<std::string> displayed;
            for ( const auto& line : rawLines.decodeLines() ) {
                displayed.push_back( asStd( line ) );
            }
            REQUIRE( displayed == expected );
        }

        THEN( "each Log Line of the view is its displayed text" )
        {
            REQUIRE( utf8Lines( rawLines ) == expected );
        }
    }
}
