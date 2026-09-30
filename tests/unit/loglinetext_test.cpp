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

// The text of a Log Line made from its bytes (loglinetext.h), in memory: from
// the bytes, the Encoding and the Decoding Policy to each shape a reader
// wants -- a QString as displayed, the same with the colors of its ANSI color
// sequences, UTF-8 as a Search matches it in a block's UTF-8 view (#291), and
// UTF-8 as the grep CLI prints it -- whether the Log Line is read in a block
// or on its own. Reading the right bytes from a Log File is the log data's,
// and tested there.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "textencoding.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "ansicolorsequences.h"
#include "loglinetext.h"
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

// Calls onLine( const ReadLogLine& ) for every Log Line of the block, as a
// reader reading them one at a time hands them over.
template <typename OnLine>
void readOneAtATime( const RawLines& rawLines, OnLine&& onLine )
{
    const std::string_view bytes( rawLines.buffer.data(), rawLines.buffer.size() );
    const auto lineFeedWidth = rawLines.textDecoder.encodingParams.lineFeedWidth;
    qint64 lineStart = 0;
    for ( std::size_t request = 0; request < rawLines.endOfLines.size(); ++request ) {
        const auto lineEnd = rawLines.endOfLines[ request ];
        onLine( readLogLine( request, bytes, lineStart, lineEnd - lineStart - lineFeedWidth,
                             rawLines.hideAnsiColorSequences ) );
        lineStart = lineEnd;
    }
}

// The shapes of the text of every Log Line of a block, read in the block and
// read one at a time.
struct Shapes {
    std::vector<QString> block;
    std::vector<AnsiColoredText> blockColored;
    std::vector<std::string> utf8View;
    std::vector<QString> oneAtATime;
    std::vector<AnsiColoredText> oneAtATimeColored;
    std::string oneAtATimeUtf8;
};

// Each shape is read from a block of its own, made by makeRawLines(): a
// block is read once.
template <typename MakeRawLines>
Shapes shapesOf( const MakeRawLines& makeRawLines, const char* encoding )
{
    const TextCodecHolder codec( TextEncoding::forName( encoding ) );
    const auto oneAtATimeRawLines = makeRawLines();
    const auto count = oneAtATimeRawLines.endOfLines.size();
    const auto read = [ &oneAtATimeRawLines ]( const auto& onLine ) {
        readOneAtATime( oneAtATimeRawLines, onLine );
    };

    Shapes shapes;
    const auto block = makeRawLines().decodeLines();
    shapes.block = { block.begin(), block.end() };
    const auto blockColored = makeRawLines().decodeAnsiColoredLines();
    shapes.blockColored = { blockColored.begin(), blockColored.end() };
    const auto viewRawLines = makeRawLines();
    const auto view = viewRawLines.buildUtf8View();
    shapes.utf8View = { view.begin(), view.end() };
    const auto oneAtATime = decodeReadLogLines<QString>( codec, count, read, logLineText );
    shapes.oneAtATime = { oneAtATime.begin(), oneAtATime.end() };
    const auto oneAtATimeColored
        = decodeReadLogLines<AnsiColoredText>( codec, count, read, ansiColoredLogLineText );
    shapes.oneAtATimeColored = { oneAtATimeColored.begin(), oneAtATimeColored.end() };
    shapes.oneAtATimeUtf8 = decodeReadLogLinesToUtf8( codec, count, read );
    return shapes;
}

// The shapes of a block holding the given bytes.
Shapes shapesOf( const std::vector<QByteArray>& bytes, const char* encoding,
                 bool hideAnsiColorSequences )
{
    return shapesOf( [ & ] { return rawLinesOfBytes( bytes, encoding, hideAnsiColorSequences ); },
                     encoding );
}

std::string asUtf8( const QString& text )
{
    return text.toUtf8().toStdString();
}

// A Log Line of the table: written as line, followed by a line end, in every
// Encoding of the table that can encode it. Its text is shown when the
// Decoding Policy shows ANSI color sequences, hidden when it hides them; read
// with its colors, it is hidden with spans, whatever the Decoding Policy.
struct TextCase {
    std::string what;
    QString line;
    QString shown;
    QString hidden;
    logsquirl::vector<AnsiColorSpan> spans;
};

const QString ByteOrderMark( QChar::ByteOrderMark );

std::vector<TextCase> textCases()
{
    const auto same = []( std::string what, const QString& line ) {
        return TextCase{ std::move( what ), line, line, line, {} };
    };
    return {
        same( "plain", QStringLiteral( "2026-09-17 12:34:56 INFO plain line" ) ),
        same( "empty", QString() ),
        same( "tabs", QStringLiteral( "\tcolumn\tafter tabs" ) ),
        same( "a carriage return inside", QStringLiteral( "a carriage\rreturn inside" ) ),
        TextCase{ "a byte order mark at the start",
                  ByteOrderMark + QStringLiteral( "after a byte order mark" ),
                  QStringLiteral( "after a byte order mark" ),
                  QStringLiteral( "after a byte order mark" ),
                  {} },
        TextCase{ "two byte order marks at the start",
                  ByteOrderMark + ByteOrderMark + QStringLiteral( "after two" ),
                  QStringLiteral( "after two" ),
                  QStringLiteral( "after two" ),
                  {} },
        same( "a byte order mark inside", QStringLiteral( "a zero width no-break space " )
                                              + ByteOrderMark + QStringLiteral( " inside" ) ),
        TextCase{ "an ANSI color sequence",
                  QStringLiteral( "\x1B[31mERROR\x1B[0m: disk full" ),
                  QStringLiteral( "\x1B[31mERROR\x1B[0m: disk full" ),
                  QStringLiteral( "ERROR: disk full" ),
                  { { 0, 5, AnsiColor::indexed( 1 ), AnsiColor{} } } },
        TextCase{ "a background color over a tab",
                  QStringLiteral( "\x1B[42mgreen\tback\x1B[m" ),
                  QStringLiteral( "\x1B[42mgreen\tback\x1B[m" ),
                  QStringLiteral( "green\tback" ),
                  { { 0, 10, AnsiColor{}, AnsiColor::indexed( 2 ) } } },
        TextCase{ "256 colors and true color",
                  QStringLiteral( "\x1B[38;5;196m256\x1B[48;2;1;2;3m truecolor" ),
                  QStringLiteral( "\x1B[38;5;196m256\x1B[48;2;1;2;3m truecolor" ),
                  QStringLiteral( "256 truecolor" ),
                  { { 0, 3, AnsiColor::indexed( 196 ), AnsiColor{} },
                    { 3, 10, AnsiColor::indexed( 196 ), AnsiColor::rgb( 1, 2, 3 ) } } },
        TextCase{ "a byte order mark before an ANSI color sequence",
                  ByteOrderMark + QStringLiteral( "\x1B[31mred\x1B[0m after it" ),
                  QStringLiteral( "\x1B[31mred\x1B[0m after it" ),
                  QStringLiteral( "red after it" ),
                  { { 0, 3, AnsiColor::indexed( 1 ), AnsiColor{} } } },
        same( "no escape character before a bracket", QStringLiteral( "first [31m line" ) ),
        same( "a NUL", QString::fromLatin1( "a NUL \0 in the middle", 21 ) ),
        same( "Latin-1", QStringLiteral( "café über ÿ" ) ),
        same( "beyond Latin-1", QStringLiteral( "euro € and a clef \U0001D11E" ) ),
    };
}

// A Log Line of the table given as bytes, without its line feed, in one
// Encoding: bytes not valid in it. Its text is shown and hidden as above; a
// Search matches searched, the Log Line in UTF-8 as it was read, where that is
// not its text.
struct BytesCase {
    std::string what;
    const char* encoding;
    QByteArray bytes;
    QString shown;
    QString hidden;
    std::optional<std::string> searched;
};

std::vector<BytesCase> bytesCases()
{
    const QString replacement( QChar::ReplacementCharacter );
    const auto asRead = []( const QByteArray& bytes ) { return bytes.toStdString(); };
    const auto sameText = []( std::string what, const char* encoding, const QByteArray& bytes,
                              const QString& text, std::optional<std::string> searched ) {
        return BytesCase{ std::move( what ), encoding, bytes, text, text, std::move( searched ) };
    };

    const QByteArray overlong( "overlong \xC0\xAF" );
    const QByteArray surrogate( "a surrogate \xED\xA0\x80 here" );
    const QByteArray pastTheLast( "past U+10FFFF \xF4\x90\x80\x80" );
    const QByteArray continuation( "a lone continuation byte \x80 here" );
    const QByteArray cutShort( "cut short at the end \xE2\x82" );
    const QByteArray latin1( "latin-1 \xE9t\xE9" );
    const QByteArray colored( "\x1B[31mred\x1B[0m \x80" );

    return {
        sameText( "an overlong sequence", "UTF-8", overlong,
                  QStringLiteral( "overlong " ) + replacement + replacement, asRead( overlong ) ),
        sameText( "a surrogate", "UTF-8", surrogate,
                  QStringLiteral( "a surrogate " ) + replacement + replacement + replacement
                      + QStringLiteral( " here" ),
                  asRead( surrogate ) ),
        sameText( "a code point past U+10FFFF", "UTF-8", pastTheLast,
                  QStringLiteral( "past U+10FFFF " ) + replacement + replacement + replacement
                      + replacement,
                  asRead( pastTheLast ) ),
        sameText( "a lone continuation byte", "UTF-8", continuation,
                  QStringLiteral( "a lone continuation byte " ) + replacement
                      + QStringLiteral( " here" ),
                  asRead( continuation ) ),
        // Decoded on its own, the sequence cut short is still waiting for the
        // rest of its character when the Log Line ends.
        sameText( "a sequence cut short at the end", "UTF-8", cutShort,
                  QStringLiteral( "cut short at the end " ), asRead( cutShort ) ),
        // Its last byte starts a character it ends in.
        sameText( "Latin-1 read as UTF-8", "UTF-8", latin1,
                  QStringLiteral( "latin-1 " ) + replacement + QStringLiteral( "t" ),
                  asRead( latin1 ) ),
        BytesCase{ "an ANSI color sequence and a byte not UTF-8", "UTF-8", colored,
                   QStringLiteral( "\x1B[31mred\x1B[0m " ) + replacement,
                   QStringLiteral( "red " ) + replacement, asRead( colored ) },
        // A lone surrogate is decoded as it is; UTF-8 has no place for it.
        sameText( "a lone high surrogate", "UTF-16LE",
                  QByteArray( "a\0\x00\xD8"
                              "b\0",
                              6 ),
                  QStringLiteral( "a" ) + QChar( 0xD800 ) + QStringLiteral( "b" ), std::nullopt ),
        // Text that is not valid UTF-16 is not searched for ANSI color
        // sequences: it keeps them.
        BytesCase{ "an ANSI color sequence and a lone low surrogate", "UTF-16LE",
                   QByteArray( "\x1B\0[\0"
                               "1\0m\0"
                               "c\0\x00\xDC",
                               12 ),
                   QStringLiteral( "\x1B[1mc" ) + QChar( 0xDC00 ),
                   QStringLiteral( "\x1B[1mc" ) + QChar( 0xDC00 ), std::nullopt },
        sameText( "a lone high surrogate", "UTF-16BE", QByteArray( "\0a\xD8\x00\0b", 6 ),
                  QStringLiteral( "a" ) + QChar( 0xD800 ) + QStringLiteral( "b" ), std::nullopt ),
    };
}

// Checks every shape of the Log Lines of a block against their text.
void checkShapes( const Shapes& shapes, const std::vector<QString>& texts,
                  const std::vector<AnsiColoredText>& colored,
                  const std::vector<std::string>& searched )
{
    std::string utf8;
    for ( const auto& text : texts ) {
        utf8 += asUtf8( text );
        utf8 += '\n';
    }

    CHECK( shapes.block == texts );
    CHECK( shapes.oneAtATime == texts );
    CHECK( shapes.oneAtATimeUtf8 == utf8 );
    CHECK( shapes.utf8View == searched );

    REQUIRE( shapes.blockColored.size() == colored.size() );
    REQUIRE( shapes.oneAtATimeColored.size() == colored.size() );
    for ( std::size_t line = 0; line < colored.size(); ++line ) {
        CAPTURE( line );
        CHECK( shapes.blockColored[ line ].text == colored[ line ].text );
        CHECK( shapes.blockColored[ line ].spans == colored[ line ].spans );
        CHECK( shapes.oneAtATimeColored[ line ].text == colored[ line ].text );
        CHECK( shapes.oneAtATimeColored[ line ].spans == colored[ line ].spans );
    }
}

} // namespace

// Every Log Line of the table, in every Encoding that can encode it, ending in
// LF and in CRLF, under either Decoding Policy -- once in a block of all of
// them and once in a block of its own, so that the UTF-8 view converts it in
// each of its ways (#291): as the whole block, line by line or decoded as a
// block.
SCENARIO( "A Log Line's text in every shape, from its bytes", "[loglinetext][encoding]" )
{
    const auto* const encoding
        = GENERATE( "UTF-8", "US-ASCII", "ISO-8859-1", "windows-1252", "UTF-16LE", "UTF-16BE" );
    const auto lineEnd = GENERATE( as<std::string>{}, "\n", "\r\n" );
    const auto hideAnsiColorSequences = GENERATE( false, true );
    const auto* const codec = TextEncoding::forName( encoding );
    REQUIRE( codec != nullptr );

    std::vector<TextCase> cases;
    for ( auto& textCase : textCases() ) {
        if ( codec->canEncode( textCase.line )
             && ( QByteArray( encoding ) != "US-ASCII"
                  || textCase.line.toLatin1() == textCase.line.toUtf8() ) ) {
            cases.push_back( std::move( textCase ) );
        }
    }
    REQUIRE( cases.size() >= 8 );

    const auto bytesOf = [ codec, &lineEnd ]( const TextCase& textCase ) {
        return codec->fromUnicode( textCase.line + QString::fromStdString( lineEnd ) );
    };
    const auto textOf = [ hideAnsiColorSequences ]( const TextCase& textCase ) {
        return hideAnsiColorSequences ? textCase.hidden : textCase.shown;
    };

    GIVEN( std::string( "every Log Line of the table in " ) + encoding + ", ending in "
           + ( lineEnd == "\n" ? "LF" : "CRLF" ) + ", "
           + ( hideAnsiColorSequences ? "hiding" : "showing" ) + " ANSI color sequences" )
    {
        THEN( "in one block, each reads as its text in every shape" )
        {
            std::vector<QByteArray> bytes;
            std::vector<QString> texts;
            std::vector<AnsiColoredText> colored;
            std::vector<std::string> searched;
            for ( const auto& textCase : cases ) {
                bytes.push_back( bytesOf( textCase ) );
                texts.push_back( textOf( textCase ) );
                colored.emplace_back( textCase.hidden, textCase.spans );
                searched.push_back( asUtf8( textOf( textCase ) ) );
            }
            checkShapes( shapesOf( bytes, encoding, hideAnsiColorSequences ), texts, colored,
                         searched );
        }

        THEN( "each in a block of its own reads as its text in every shape" )
        {
            for ( const auto& textCase : cases ) {
                CAPTURE( textCase.what );
                checkShapes( shapesOf( { bytesOf( textCase ) }, encoding, hideAnsiColorSequences ),
                             { textOf( textCase ) },
                             { AnsiColoredText( textCase.hidden, textCase.spans ) },
                             { asUtf8( textOf( textCase ) ) } );
            }
        }
    }
}

SCENARIO( "A Log Line's text in every shape, from bytes not valid in its Encoding",
          "[loglinetext][encoding]" )
{
    const auto hideAnsiColorSequences = GENERATE( false, true );

    for ( const auto& bytesCase : bytesCases() ) {
        CAPTURE( bytesCase.what, bytesCase.encoding, hideAnsiColorSequences );

        auto bytes = bytesCase.bytes;
        bytes += TextEncoding::forName( bytesCase.encoding )->fromUnicode( QStringLiteral( "\n" ) );

        const auto text = hideAnsiColorSequences ? bytesCase.hidden : bytesCase.shown;
        // A Search matches the Log Line as it was read unless it decodes it.
        const bool isDecodedForSearch
            = !bytesCase.searched
              || ( hideAnsiColorSequences && bytesCase.bytes.contains( '\x1B' ) );
        checkShapes( shapesOf( { bytes }, bytesCase.encoding, hideAnsiColorSequences ), { text },
                     { AnsiColoredText( bytesCase.hidden,
                                        parseAnsiColorSequences( bytesCase.shown ).spans ) },
                     { isDecodedForSearch ? asUtf8( text ) : *bytesCase.searched } );
    }
}

// The grep CLI prints Log Lines read one at a time in UTF-8, and a Search
// matches a UTF-8 block's view, made line by line: both make the text of a
// Log Line in UTF-8 by one rule. Only an undecodable byte reads differently:
// the Search matches the Log Line where it was read (#291).
SCENARIO( "A Log Line's text in UTF-8 by the per-line UTF-8 rule", "[loglinetext][utf8]" )
{
    struct Utf8Case {
        std::string what;
        QByteArray bytes;
        bool hideAnsiColorSequences;
        std::string printed;
        std::string searched;
    };
    const std::vector<Utf8Case> cases{
        { "plain", "plain text", false, "plain text", "plain text" },
        { "ANSI shown", "\x1B[31mred\x1B[0m", false, "\x1B[31mred\x1B[0m", "\x1B[31mred\x1B[0m" },
        { "ANSI hidden", "\x1B[31mred\x1B[0m", true, "red", "red" },
        { "a byte order mark",
          "\xEF\xBB\xBF"
          "after it",
          false, "after it", "after it" },
        { "a carriage return", "before it\r", true, "before it", "before it" },
        { "an undecodable byte", "a \x80 b", false, "a \xEF\xBF\xBD b", "a \x80 b" },
        { "an undecodable byte and ANSI hidden", "\x1B[31mred\x1B[0m \x80", true,
          "red \xEF\xBF\xBD", "red \xEF\xBF\xBD" },
    };

    for ( const auto& utf8Case : cases ) {
        CAPTURE( utf8Case.what );
        const auto shapes
            = shapesOf( { utf8Case.bytes + '\n' }, "UTF-8", utf8Case.hideAnsiColorSequences );
        CHECK( shapes.oneAtATimeUtf8 == utf8Case.printed + '\n' );
        CHECK( shapes.utf8View == std::vector<std::string>{ utf8Case.searched } );
    }
}

SCENARIO( "Log Lines are each decoded on their own", "[loglinetext]" )
{
    GIVEN( "a UTF-8 Log Line cut short in the middle of a character, and two after it" )
    {
        const std::vector<QByteArray> bytes{ QByteArray( "cut short \xE2\x82\n" ),
                                             QByteArray( "next\n" ),
                                             QByteArray( "\xEF\xBB\xBF"
                                                         "after a byte order mark\n" ) };

        THEN( "what is cut short does not reach the next, in every shape read one at a time" )
        {
            const auto shapes = shapesOf( bytes, "UTF-8", false );
            const std::vector<QString> texts{ QStringLiteral( "cut short " ),
                                              QStringLiteral( "next" ),
                                              QStringLiteral( "after a byte order mark" ) };
            CHECK( shapes.oneAtATime == texts );
            CHECK( shapes.oneAtATimeUtf8 == "cut short \nnext\nafter a byte order mark\n" );
            REQUIRE( shapes.oneAtATimeColored.size() == 3 );
            for ( std::size_t line = 0; line < texts.size(); ++line ) {
                CHECK( shapes.oneAtATimeColored[ line ].text == texts[ line ] );
            }
        }

        THEN( "what is cut short does not reach the next in a block either (#649)" )
        {
            const auto shapes = shapesOf( bytes, "UTF-8", false );
            const std::vector<QString> texts{ QStringLiteral( "cut short " ),
                                              QStringLiteral( "next" ),
                                              QStringLiteral( "after a byte order mark" ) };
            CHECK( shapes.block == texts );
            REQUIRE( shapes.blockColored.size() == 3 );
            for ( std::size_t line = 0; line < texts.size(); ++line ) {
                CHECK( shapes.blockColored[ line ].text == texts[ line ] );
            }
        }
    }
}

SCENARIO( "A Log Line that cannot be read reads as a warning in every shape", "[loglinetext]" )
{
    const auto warningLine = []( std::string_view warning ) {
        return QString::fromLatin1( warning.data(), static_cast<qsizetype>( warning.size() ) );
    };

    GIVEN( "a block whose second Log Line reaches past the bytes read" )
    {
        const auto rawLines = [] {
            auto cutShort = rawLinesOfBytes( { "first\n", "second\n", "third\n" }, "UTF-8", false );
            cutShort.buffer.resize( 9 );
            return cutShort;
        };

        THEN( "it reads as a warning, and every Log Line after it as one" )
        {
            const auto expected = std::vector<QString>{ QStringLiteral( "first" ),
                                                        warningLine( FileReadFailedWarning ),
                                                        warningLine( LinesNotDecodedWarning ) };
            const auto shapes = shapesOf( rawLines, "UTF-8" );
            CHECK( shapes.block == expected );
            REQUIRE( shapes.blockColored.size() == 3 );
            CHECK( shapes.blockColored[ 1 ].text == expected[ 1 ] );
            CHECK( shapes.blockColored[ 2 ].text == expected[ 2 ] );
        }

        THEN( "read one at a time, only it reads as a warning past the bytes read" )
        {
            const auto shapes = shapesOf( rawLines, "UTF-8" );
            const auto expected = std::vector<QString>{ QStringLiteral( "first" ),
                                                        warningLine( FileReadFailedWarning ),
                                                        warningLine( FileReadFailedWarning ) };
            CHECK( shapes.oneAtATime == expected );
            CHECK( shapes.oneAtATimeUtf8
                   == "first\n" + std::string( FileReadFailedWarning ) + "\n"
                          + std::string( FileReadFailedWarning ) + "\n" );
        }

        THEN( "its UTF-8 view has only the Log Lines there are bytes for" )
        {
            // The view is decoded as a block, which has the first Log Line and
            // what was read of the second.
            REQUIRE( shapesOf( rawLines, "UTF-8" ).utf8View
                     == std::vector<std::string>{ "first", "sec" } );
        }
    }

    GIVEN( "a block whose first Log Line is too long to decode" )
    {
        auto rawLines = rawLinesOfBytes( { "first\n", "second\n" }, "UTF-8", false );
        rawLines.endOfLines = { MaxLogLineLength + 1, MaxLogLineLength + 8 };

        THEN( "it reads as a warning, and every Log Line after it as one" )
        {
            const auto lines = rawLines.decodeLines();
            REQUIRE( std::vector<QString>( lines.begin(), lines.end() )
                     == std::vector<QString>{ warningLine( LineTooLongWarning ),
                                              warningLine( LinesNotDecodedWarning ) } );
        }
    }

    GIVEN( "Log Lines read one at a time, one of them too long and one not called for" )
    {
        const TextCodecHolder codec( TextEncoding::forName( "UTF-8" ) );
        const std::string_view bytes = "first\n";
        const auto read = [ &bytes ]( const auto& onLine ) {
            onLine( readLogLine( 2, bytes, 0, 5, false ) );
            onLine( readLogLine( 0, bytes, 0, MaxLogLineLength, false ) );
        };

        THEN( "the one too long reads as a warning, the one not called for as another" )
        {
            const auto lines = decodeReadLogLines<QString>( codec, 3, read, logLineText );
            REQUIRE( std::vector<QString>( lines.begin(), lines.end() )
                     == std::vector<QString>{ warningLine( LineTooLongWarning ),
                                              warningLine( LinesNotReadWarning ),
                                              QStringLiteral( "first" ) } );
            REQUIRE( decodeReadLogLinesToUtf8( codec, 3, read )
                     == std::string( LineTooLongWarning ) + "\n"
                            + std::string( LinesNotReadWarning ) + "\nfirst\n" );
        }
    }
}

namespace {

// How a Search saw the block before #291: the whole block decoded to a QString,
// its ANSI color sequences removed, converted to UTF-8 and split at each line
// feed -- and each Log Line then trimmed to its text, without the carriage
// return that ends it or the byte order marks that start it (#522).
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

    std::vector<std::string> split;
    std::string_view rest( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
    for ( auto lineFeed = rest.find( '\n' ); lineFeed != std::string_view::npos;
          lineFeed = rest.find( '\n' ) ) {
        split.emplace_back( trimToLogLineText( rest.substr( 0, lineFeed ) ) );
        rest.remove_prefix( lineFeed + 1 );
    }
    if ( !rest.empty() ) {
        split.emplace_back( trimToLogLineText( rest ) );
    }
    return split;
}

std::vector<std::string> utf8Lines( const RawLines& rawLines )
{
    const auto view = rawLines.buildUtf8View();
    return { view.begin(), view.end() };
}

} // namespace

// Blocks the known line ends do not split into Log Lines are decoded as a
// whole block for their UTF-8 view.
SCENARIO( "A block's UTF-8 view of a block its line ends do not split", "[search][encoding]" )
{
    const auto hideAnsiColorSequences = GENERATE( false, true );

    GIVEN( "a block whose last Log Line has no line feed yet" )
    {
        const auto* const encoding
            = GENERATE( "UTF-8", "UTF-16LE", "UTF-16BE", "ISO-8859-1", "windows-1252" );
        auto* const codec = TextEncoding::forName( encoding );
        std::vector<QByteArray> bytes;
        for ( const auto& line : { QStringLiteral( "\x1B[31mERROR\x1B[0m: disk full\n" ),
                                   QStringLiteral( "café and a carriage return\r\n" ),
                                   QStringLiteral( "last plain line\n" ) } ) {
            bytes.push_back( codec->fromUnicode( line ) );
        }
        bytes.back().chop( EncodingParameters( codec ).lineFeedWidth );
        const auto rawLines = rawLinesOfBytes( bytes, encoding, hideAnsiColorSequences );

        THEN( "the view has the same Log Lines a Search saw before" )
        {
            const auto view = utf8Lines( rawLines );
            REQUIRE( view.size() == rawLines.endOfLines.size() );
            REQUIRE( view == decodedThenConvertedLines( bytes, encoding, hideAnsiColorSequences ) );
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
