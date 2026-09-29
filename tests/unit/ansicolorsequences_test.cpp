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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "textencoding.h"
#include <QString>

#include "ansicolorsequences.h"
#include "searchblocksource.h"

namespace {

// Raw Log Lines as a Log File in the given encoding holds them, one line feed
// after each.
RawLines rawLinesOf( const std::vector<std::string>& lines, const char* encoding,
                     bool hideAnsiColorSequences )
{
    auto* const codec = TextEncoding::forName( encoding );

    RawLines rawLines;
    rawLines.startLine = LineNumber{ 0 };
    // No byte order mark: a Log File has at most one, before its first line.
    for ( const auto& line : lines ) {
        const auto text = QString::fromStdString( line + "\n" );
        const auto encoded = codec->fromUnicode( text );
        rawLines.buffer.insert( rawLines.buffer.end(), encoded.begin(), encoded.end() );
        rawLines.endOfLines.push_back( static_cast<qint64>( rawLines.buffer.size() ) );
    }
    rawLines.textDecoder.decoder = codec->makeDecoder();
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

namespace {

// The texts the scenarios above hide the sequences of, and the edge cases of
// the pattern: too many digits, an empty parameter, a separator at the end,
// the letter case of the final byte, two escape characters in a row.
const std::vector<QString> PatternCases{
    QStringLiteral( "\x1B[31mERROR\x1B[0m: disk \x1B[1;32mfull\x1B[K" ),
    QStringLiteral( "plain [31m text" ),
    QStringLiteral( "title \x1B]0;name\x07 and \x1B[2J cleared" ),
    QStringLiteral( "\x1B[31mERROR\x1B[0m: disk full" ),
    QStringLiteral( "\x1B[1;32mOK\x1B[K done" ),
    QStringLiteral( "\x1B[12345mfive digits\x1B[1;1234mfour after a separator" ),
    QStringLiteral( "\x1B[1;mempty parameter\x1B[;1mleading separator" ),
    QStringLiteral( "\x1B[31Mupper case\x1B[kerase\x1B[0K" ),
    QStringLiteral( "\x1B\x1B[31mtwo escapes\x1B" ),
    QStringLiteral( "\x1B[mreset\x1B[" ),
    QStringLiteral( "\x1B[38:5:196mcolons\x1B[38;2;1;2;3mtruecolor" ),
    QString( "kelvin \x1B[0" ) + QChar( 0x212A ) + "sign",
    QString( "lone surrogate \x1B[31m" ) + QChar( 0xD800 ) + "\x1B[0m",
};

// Random texts made of what the pattern looks at, and a few characters it
// does not: every way a sequence can be almost right is in them.
std::vector<QString> generatedTexts()
{
    const QString alphabet
        = QString( "\x1B\x1B\x1B[[[0123456789;;::mMkKx \t" ) + QChar( 0x212A ) + QChar( 0x00E9 );
    std::mt19937 random{ 573 };
    std::uniform_int_distribution<int> length( 0, 40 );
    std::uniform_int_distribution<qsizetype> pick( 0, alphabet.size() - 1 );

    std::vector<QString> texts;
    for ( int count = 0; count < 20'000; ++count ) {
        QString text;
        const auto size = length( random );
        for ( int index = 0; index < size; ++index ) {
            text += alphabet[ pick( random ) ];
        }
        texts.push_back( text );
    }
    return texts;
}

QString withoutSequences( QString text )
{
    removeAnsiColorSequences( text );
    return text;
}

constexpr auto lineColor = AnsiColor{};

} // namespace

SCENARIO( "Parsing ANSI color sequences leaves exactly the text hiding them leaves", "[ansi]" )
{
    GIVEN( "the texts the pattern's edge cases are in" )
    {
        THEN( "every one parses to the text without its sequences" )
        {
            for ( const auto& text : PatternCases ) {
                INFO( text.toStdString() );
                CHECK( parseAnsiColorSequences( text ).text == withoutSequences( text ) );
            }
        }
    }

    GIVEN( "many generated texts" )
    {
        const auto texts = generatedTexts();

        THEN( "every one parses to the text without its sequences" )
        {
            int differing = 0;
            for ( const auto& text : texts ) {
                if ( parseAnsiColorSequences( text ).text != withoutSequences( text ) ) {
                    ++differing;
                    UNSCOPED_INFO( text.toStdString() );
                }
            }
            REQUIRE( differing == 0 );
        }

        THEN( "no color span reaches past the end of its text" )
        {
            for ( const auto& text : texts ) {
                const auto parsed = parseAnsiColorSequences( text );
                for ( const auto& span : parsed.spans ) {
                    REQUIRE( span.start >= 0 );
                    REQUIRE( span.length > 0 );
                    REQUIRE( span.start + span.length <= parsed.text.size() );
                }
            }
        }
    }
}

SCENARIO( "Parsing ANSI color sequences keeps the colors they ask for", "[ansi]" )
{
    GIVEN( "a text without an escape character" )
    {
        const QString text = "plain [31m text";

        THEN( "it comes back as it is, without a color" )
        {
            const auto parsed = parseAnsiColorSequences( text );
            REQUIRE( parsed.text == text );
            REQUIRE( parsed.spans.empty() );
        }
    }

    GIVEN( "a word in a basic foreground color, reset afterwards" )
    {
        const auto parsed = parseAnsiColorSequences( "\x1B[31mERROR\x1B[0m: disk full" );

        THEN( "the word has the color, in the columns of the text without the sequences" )
        {
            REQUIRE( parsed.text == "ERROR: disk full" );
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 0, 5, AnsiColor::indexed( 1 ), lineColor } } );
        }
    }

    GIVEN( "bright foreground and background colors" )
    {
        const auto parsed = parseAnsiColorSequences( "\x1B[91;104mA\x1B[37;40mB" );

        THEN( "they are the basic colors 8 to 15" )
        {
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 0, 1, AnsiColor::indexed( 9 ), AnsiColor::indexed( 12 ) },
                         { 1, 1, AnsiColor::indexed( 7 ), AnsiColor::indexed( 0 ) } } );
        }
    }

    GIVEN( "a 256-color foreground and a truecolor background, with either separator" )
    {
        const auto parsed
            = parseAnsiColorSequences( "\x1B[38;5;196mA\x1B[48:2:1:2:3mB\x1B[39mC\x1B[49mD" );

        THEN( "each is kept as given, and 39 and 49 give back the line's own color" )
        {
            REQUIRE( parsed.text == "ABCD" );
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 0, 1, AnsiColor::indexed( 196 ), lineColor },
                         { 1, 1, AnsiColor::indexed( 196 ), AnsiColor::rgb( 1, 2, 3 ) },
                         { 2, 1, lineColor, AnsiColor::rgb( 1, 2, 3 ) } } );
        }
    }

    GIVEN( "an empty sequence resetting a color" )
    {
        const auto parsed = parseAnsiColorSequences( "\x1B[32mgreen\x1B[m plain" );

        THEN( "it resets like 0" )
        {
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 0, 5, AnsiColor::indexed( 2 ), lineColor } } );
        }
    }

    GIVEN( "codes other than colors: bold, underline, inverse, erase in line" )
    {
        const auto parsed = parseAnsiColorSequences( "\x1B[1mbold\x1B[4;7m under\x1B[K" );

        THEN( "they are removed without a color" )
        {
            REQUIRE( parsed.text == "bold under" );
            REQUIRE( parsed.spans.empty() );
        }
    }

    GIVEN( "malformed colors: an index missing, one out of range, a truecolor cut short" )
    {
        const auto parsed = parseAnsiColorSequences(
            "\x1B[38;5mA\x1B[31;38;5;256mB\x1B[48;2;1;2mC\x1B[38;9;1mD" );

        THEN( "they are removed without a color" )
        {
            REQUIRE( parsed.text == "ABCD" );
            REQUIRE( parsed.spans.empty() );
        }
    }

    GIVEN( "a color that is never reset" )
    {
        const auto parsed = parseAnsiColorSequences( "plain \x1B[33mto the end" );

        THEN( "it colors the text up to its end" )
        {
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 6, 10, AnsiColor::indexed( 3 ), lineColor } } );
        }
    }

    GIVEN( "a color set again before any text" )
    {
        const auto parsed = parseAnsiColorSequences( "\x1B[31m\x1B[32mA\x1B[32mB\x1B[0m" );

        THEN( "only the color the text is in counts, as one span" )
        {
            REQUIRE( parsed.spans
                     == logsquirl::vector<AnsiColorSpan>{
                         { 0, 2, AnsiColor::indexed( 2 ), lineColor } } );
        }
    }
}
