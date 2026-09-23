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

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QTextDecoder>

#include "ansicolorsequences.h"
#include "searchblocksource.h"

namespace {

// Raw Log Lines holding the given bytes, each entry one Log Line with its line
// feed (or without one, for a last Log Line still being written).
RawLines rawLinesOfBytes( const std::vector<QByteArray>& lines, const char* encoding,
                          bool hideAnsiColorSequences )
{
    auto* const codec = QTextCodec::codecForName( encoding );
    REQUIRE( codec != nullptr );

    RawLines rawLines;
    rawLines.startLine = LineNumber{ 0 };
    for ( const auto& line : lines ) {
        rawLines.buffer.insert( rawLines.buffer.end(), line.begin(), line.end() );
        rawLines.endOfLines.push_back( static_cast<qint64>( rawLines.buffer.size() ) );
    }
    rawLines.textDecoder.decoder = std::make_unique<QTextDecoder>( codec );
    rawLines.textDecoder.encodingParams = EncodingParameters( codec );
    rawLines.hideAnsiColorSequences = hideAnsiColorSequences;
    return rawLines;
}

// The given Log Lines encoded as a Log File in the encoding holds them, one
// line feed after each.
std::vector<QByteArray> encodedLines( const QStringList& lines, const char* encoding )
{
    auto* const codec = QTextCodec::codecForName( encoding );
    QTextCodec::ConverterState state( QTextCodec::IgnoreHeader );
    std::vector<QByteArray> encoded;
    for ( const auto& line : lines ) {
        const auto text = line + '\n';
        encoded.push_back(
            codec->fromUnicode( text.constData(), static_cast<int>( text.size() ), &state ) );
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
// feed.
std::vector<std::string> decodedThenConvertedLines( const std::vector<QByteArray>& lines,
                                                    const char* encoding,
                                                    bool hideAnsiColorSequences )
{
    QByteArray block;
    for ( const auto& line : lines ) {
        block += line;
    }

    QTextDecoder decoder( QTextCodec::codecForName( encoding ) );
    auto text = decoder.toUnicode( block );
    if ( hideAnsiColorSequences ) {
        removeAnsiColorSequences( text );
    }
    const auto utf8 = text.toUtf8();

    std::vector<std::string> split;
    std::string_view rest( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
    for ( auto lineFeed = rest.find( '\n' ); lineFeed != std::string_view::npos;
          lineFeed = rest.find( '\n' ) ) {
        split.emplace_back( rest.substr( 0, lineFeed ) );
        rest.remove_prefix( lineFeed + 1 );
    }
    if ( !rest.empty() ) {
        split.emplace_back( rest );
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
    if ( QTextCodec::codecForName( encoding )->canEncode( beyondLatin1 ) ) {
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

        THEN( "each Log Line of the view is the Log Line in UTF-8, without its line feed" )
        {
            std::vector<std::string> expected;
            for ( auto line : lines ) {
                if ( hideAnsiColorSequences ) {
                    removeAnsiColorSequences( line );
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
            = EncodingParameters( QTextCodec::codecForName( encoding ) ).lineFeedWidth;
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
