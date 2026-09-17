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

// A Decoding Policy that hides ANSI color sequences removes them from every
// Log Line read, whether the Log Lines are read one at a time or as a block
// for a Search (#278).

#include <catch2/catch.hpp>

#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <QString>
#include <QTextCodec>
#include <QTextDecoder>

#include "ansicolorsequences.h"
#include "searchblocksource.h"

namespace {

// Raw Log Lines as a Log File in the given encoding holds them, one line feed
// after each.
RawLines rawLinesOf( const std::vector<std::string>& lines, const char* encoding,
                     bool hideAnsiColorSequences )
{
    auto* const codec = QTextCodec::codecForName( encoding );

    RawLines rawLines;
    rawLines.startLine = LineNumber{ 0 };
    // No byte order mark: a Log File has at most one, before its first line.
    QTextCodec::ConverterState state( QTextCodec::IgnoreHeader );
    for ( const auto& line : lines ) {
        const auto text = QString::fromStdString( line + "\n" );
        const auto encoded
            = codec->fromUnicode( text.constData(), static_cast<int>( text.size() ), &state );
        rawLines.buffer.insert( rawLines.buffer.end(), encoded.begin(), encoded.end() );
        rawLines.endOfLines.push_back( static_cast<qint64>( rawLines.buffer.size() ) );
    }
    rawLines.textDecoder.decoder = std::make_unique<QTextDecoder>( codec );
    rawLines.textDecoder.encodingParams = EncodingParameters( codec );
    rawLines.hideAnsiColorSequences = hideAnsiColorSequences;
    return rawLines;
}

const std::vector<std::string> MixedLogLines{
    "plain line",         "\x1B[31mERROR\x1B[0m: disk full",
    "another plain line", "\x1B[1;32mOK\x1B[K done",
    "last plain line",
};

const std::vector<QString> MixedLogLinesHidden{
    "plain line", "ERROR: disk full", "another plain line", "OK done", "last plain line",
};

} // namespace

SCENARIO( "Hiding ANSI color sequences in a text", "[ansi]" )
{
    GIVEN( "a text with color and erase-in-line sequences" )
    {
        QString text = "\x1B[31mERROR\x1B[0m: disk \x1B[1;32mfull\x1B[K";

        WHEN( "its ANSI color sequences are removed" )
        {
            removeAnsiColorSequences( text );

            THEN( "only the text they interrupted is left" )
            {
                REQUIRE( text == "ERROR: disk full" );
            }
        }
    }

    GIVEN( "a text without an escape character" )
    {
        QString text = "plain [31m text";

        WHEN( "its ANSI color sequences are removed" )
        {
            removeAnsiColorSequences( text );

            THEN( "it is unchanged" )
            {
                REQUIRE( text == "plain [31m text" );
            }
        }
    }

    GIVEN( "a text with an escape character that starts no color sequence" )
    {
        QString text = "title \x1B]0;name\x07 and \x1B[2J cleared";

        WHEN( "its ANSI color sequences are removed" )
        {
            removeAnsiColorSequences( text );

            THEN( "it is unchanged" )
            {
                REQUIRE( text == "title \x1B]0;name\x07 and \x1B[2J cleared" );
            }
        }
    }

    GIVEN( "many threads removing ANSI color sequences at once" )
    {
        constexpr int ThreadCount = 8;
        constexpr int TextsPerThread = 2000;
        std::vector<int> correct( ThreadCount, 0 );

        {
            std::vector<std::jthread> threads;
            for ( int thread = 0; thread < ThreadCount; ++thread ) {
                threads.emplace_back( [ thread, &correct ] {
                    for ( int index = 0; index < TextsPerThread; ++index ) {
                        QString text = QStringLiteral( "\x1B[3%1mline\x1B[0m %2" )
                                           .arg( thread % 8 )
                                           .arg( index );
                        removeAnsiColorSequences( text );
                        if ( text == QStringLiteral( "line %1" ).arg( index ) ) {
                            ++correct[ static_cast<std::size_t>( thread ) ];
                        }
                    }
                } );
            }
        }

        THEN( "every thread removes them from every text" )
        {
            REQUIRE( correct == std::vector<int>( ThreadCount, TextsPerThread ) );
        }
    }
}

SCENARIO( "Raw Log Lines hide ANSI color sequences when decoded", "[ansi]" )
{
    const auto encoding = GENERATE( "UTF-8", "ISO-8859-1", "UTF-16LE" );

    GIVEN( std::string( "a block of " ) + encoding
           + " Log Lines, some with ANSI color sequences, read hiding them" )
    {
        const auto rawLines = rawLinesOf( MixedLogLines, encoding, true );

        THEN( "every Log Line decodes without them" )
        {
            const auto decoded = rawLines.decodeLines();
            REQUIRE( std::vector<QString>( decoded.begin(), decoded.end() )
                     == MixedLogLinesHidden );
        }

        THEN( "the UTF-8 view of the block has every Log Line without them" )
        {
            const auto view = rawLines.buildUtf8View();
            std::vector<QString> lines;
            for ( const auto line : view ) {
                lines.push_back(
                    QString::fromUtf8( line.data(), static_cast<qsizetype>( line.size() ) ) );
            }
            REQUIRE( lines == MixedLogLinesHidden );
        }
    }

    GIVEN( std::string( "a block of " ) + encoding
           + " Log Lines without ANSI color sequences, read hiding them" )
    {
        const std::vector<std::string> plain{ "first [31m line", "second line" };
        const auto rawLines = rawLinesOf( plain, encoding, true );

        THEN( "the UTF-8 view of the block has every Log Line as it is" )
        {
            const auto view = rawLines.buildUtf8View();
            REQUIRE( view.size() == 2 );
            REQUIRE( view[ 0 ] == "first [31m line" );
            REQUIRE( view[ 1 ] == "second line" );
        }
    }

    GIVEN( std::string( "a block of " ) + encoding
           + " Log Lines with ANSI color sequences, read showing them" )
    {
        const auto rawLines = rawLinesOf( MixedLogLines, encoding, false );

        THEN( "every Log Line decodes with them" )
        {
            const auto decoded = rawLines.decodeLines();
            REQUIRE( decoded.size() == MixedLogLines.size() );
            REQUIRE( decoded[ 1 ] == QString::fromStdString( MixedLogLines[ 1 ] ) );
            REQUIRE( decoded[ 3 ] == QString::fromStdString( MixedLogLines[ 3 ] ) );
        }
    }
}
