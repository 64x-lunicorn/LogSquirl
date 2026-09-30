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

// This file makes the text of Log Lines from their bytes (loglinetext.h),
// for a block of raw Log Lines and for Log Lines read one at a time.

#include "loglinetext.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <QtEndian>

#include <simdutf.h>

#include "ansicolorsequences.h"
#include "containers.h"
#include "log.h"
#include "searchblocksource.h"

QString warningText( std::string_view warning )
{
    return QString::fromLatin1( warning.data(), static_cast<qsizetype>( warning.size() ) );
}

void logNotEnoughMemory()
{
    LOG_ERROR << "not enough memory";
}

QString logLineText( QString&& decodedLine, bool hideAnsiColorSequences )
{
    if ( hideAnsiColorSequences ) {
        removeAnsiColorSequences( decodedLine );
    }
    trimToLogLineText( decodedLine );
    return std::move( decodedLine );
}

// The sequences are parsed out before the text is trimmed, as they are
// removed before it is trimmed for its text.
AnsiColoredText ansiColoredLogLineText( QString&& decodedLine, bool /*hideAnsiColorSequences*/ )
{
    auto parsed = parseAnsiColorSequences( std::move( decodedLine ) );
    const auto untrimmedSize = parsed.text.size();
    const auto withCarriageReturn = parsed.text.endsWith( QChar::CarriageReturn );
    trimToLogLineText( parsed.text );
    if ( parsed.spans.empty() ) {
        return parsed;
    }

    // Only a carriage return at the end and byte order marks at the start
    // are trimmed.
    const auto trimmedAtStart
        = static_cast<int>( untrimmedSize - parsed.text.size() - ( withCarriageReturn ? 1 : 0 ) );
    const auto size = static_cast<int>( parsed.text.size() );
    logsquirl::vector<AnsiColorSpan> spans;
    spans.reserve( parsed.spans.size() );
    for ( auto span : parsed.spans ) {
        const auto start = std::clamp( span.start - trimmedAtStart, 0, size );
        const auto end = std::clamp( span.start + span.length - trimmedAtStart, 0, size );
        if ( end > start ) {
            span.start = start;
            span.length = end - start;
            spans.push_back( span );
        }
    }
    parsed.spans = std::move( spans );
    return parsed;
}

ReadLogLine readLogLine( std::size_t request, std::string_view bytesRead, qint64 begin,
                         qint64 length, bool hideAnsiColorSequences )
{
    ReadLogLine line{ .request = request,
                      .bytes = {},
                      .warning = {},
                      .hideAnsiColorSequences = hideAnsiColorSequences };
    if ( length >= MaxLogLineLength ) {
        line.warning = LineTooLongWarning;
    }
    else if ( begin + length > static_cast<qint64>( bytesRead.size() ) ) {
        line.warning = FileReadFailedWarning;
    }
    else if ( length > 0 ) {
        line.bytes = bytesRead.substr( static_cast<std::size_t>( begin ),
                                       static_cast<std::size_t>( length ) );
    }
    return line;
}

std::string utf8LogLineTextsInRequestOrder( std::string&& decoded,
                                            const logsquirl::vector<Utf8LogLinePlace>& placed )
{
    // Log Lines asked for once each and in ascending order were read in the
    // order asked: what was decoded is the text already.
    std::size_t expectedBegin = 0;
    const bool isInOrder = std::all_of(
        placed.begin(), placed.end(), [ &expectedBegin ]( const Utf8LogLinePlace& place ) {
            const bool follows = place.isRead && place.begin == expectedBegin;
            expectedBegin += place.size;
            return follows;
        } );
    if ( isInOrder ) {
        return std::move( decoded );
    }

    std::string text;
    text.reserve( decoded.size() );
    for ( const auto& place : placed ) {
        if ( place.isRead ) {
            text.append( decoded, place.begin, place.size );
        }
        else {
            text.append( LinesNotReadWarning );
            text += '\n';
        }
    }
    return text;
}

std::string utf8NotEnoughMemory( std::size_t count )
{
    std::string text;
    for ( std::size_t request = 0; request < count; ++request ) {
        text.append( NotEnoughMemoryWarning );
        text += '\n';
    }
    return text;
}

namespace {

// Every Log Line of a block, decoded and made a Line by toLine( QString&&
// decodedLine, bool hideAnsiColorSequences ). A Log Line that cannot be
// decoded reads as a warning, and so does every one after it.
template <typename Line, typename ToLine>
logsquirl::vector<Line> decodeRawLines( const RawLines& rawLines, ToLine toLine )
{
    const auto& endOfLines = rawLines.endOfLines;
    if ( endOfLines.empty() ) {
        return logsquirl::vector<Line>();
    }

    logsquirl::vector<Line> decodedLines;
    decodedLines.reserve( endOfLines.size() );

    try {
        const std::string_view buffer( rawLines.buffer.data(), rawLines.buffer.size() );
        qint64 lineStart = 0;
        const auto& textDecoder = rawLines.textDecoder;
        const auto lineFeedWidth = textDecoder.encodingParams.lineFeedWidth;
        for ( const auto& lineEnd : endOfLines ) {
            const auto line = readLogLine( decodedLines.size(), buffer, lineStart,
                                           lineEnd - lineStart - lineFeedWidth,
                                           rawLines.hideAnsiColorSequences );
            if ( !line.warning.empty() ) {
                LOG_WARNING << "Log Line " << rawLines.startLine.get() + line.request
                            << " not decoded: " << line.warning;
                decodedLines.push_back( Line{ warningText( line.warning ) } );
                break;
            }

            // Not reset between the Log Lines of a block: the block is
            // decoded as it was read.
            decodedLines.push_back(
                toLine( textDecoder.decode( line.bytes.data(),
                                            static_cast<qsizetype>( line.bytes.size() ) ),
                        line.hideAnsiColorSequences ) );

            lineStart = lineEnd;
        }
    } catch ( const std::bad_alloc& ) {
        logNotEnoughMemory();
        decodedLines.push_back( Line{ warningText( NotEnoughMemoryWarning ) } );
    }

    decodedLines.reserve( endOfLines.size() - decodedLines.size() );
    while ( decodedLines.size() < endOfLines.size() ) {
        decodedLines.push_back( Line{ warningText( LinesNotDecodedWarning ) } );
    }

    return decodedLines;
}

} // namespace

logsquirl::vector<QString> RawLines::decodeLines() const
{
    return decodeRawLines<QString>( *this, logLineText );
}

logsquirl::vector<AnsiColoredText> RawLines::decodeAnsiColoredLines() const
{
    return decodeRawLines<AnsiColoredText>( *this, ansiColoredLogLineText );
}

namespace {

// The Encodings a Search converts to UTF-8 straight from the bytes of each Log
// Line, found from the known line ends, without decoding the block to a
// QString first (#291). Any other Encoding is decoded as a whole.
enum class DirectEncoding { None, Utf8, Latin1, Utf16LE, Utf16BE };

DirectEncoding directEncodingOf( const EncodingParameters& encodingParams )
{
    if ( encodingParams.isUtf8Compatible ) {
        return DirectEncoding::Utf8;
    }
    if ( encodingParams.isLatin1 ) {
        return DirectEncoding::Latin1;
    }
    if ( encodingParams.isUtf16LE ) {
        return DirectEncoding::Utf16LE;
    }
    if ( encodingParams.isUtf16BE ) {
        return DirectEncoding::Utf16BE;
    }
    return DirectEncoding::None;
}

bool isUtf16( DirectEncoding encoding )
{
    return encoding == DirectEncoding::Utf16LE || encoding == DirectEncoding::Utf16BE;
}

const char16_t* asUtf16( std::string_view bytes )
{
    return reinterpret_cast<const char16_t*>( bytes.data() );
}

char16_t codeUnitAt( std::string_view bytes, std::size_t index, DirectEncoding encoding )
{
    const auto first = static_cast<unsigned char>( bytes[ index ] );
    const auto second = static_cast<unsigned char>( bytes[ index + 1 ] );
    return encoding == DirectEncoding::Utf16LE ? static_cast<char16_t>( first | ( second << 8 ) )
                                               : static_cast<char16_t>( ( first << 8 ) | second );
}

std::string_view withoutLineFeed( std::string_view line, DirectEncoding encoding )
{
    if ( isUtf16( encoding ) ) {
        if ( line.size() >= 2 && codeUnitAt( line, line.size() - 2, encoding ) == u'\n' ) {
            line.remove_suffix( 2 );
        }
    }
    else if ( !line.empty() && line.back() == '\n' ) {
        line.remove_suffix( 1 );
    }
    return line;
}

bool containsEscape( std::string_view line, DirectEncoding encoding )
{
    // In an Encoding not read straight to UTF-8, the escape character need
    // not be an escape byte.
    if ( encoding == DirectEncoding::None ) {
        return true;
    }
    if ( !isUtf16( encoding ) ) {
        return line.find( '\x1B' ) != std::string_view::npos;
    }
    for ( std::size_t index = 0; index + 1 < line.size(); index += 2 ) {
        if ( codeUnitAt( line, index, encoding ) == u'\x1B' ) {
            return true;
        }
    }
    return false;
}

bool isValid( std::string_view line, DirectEncoding encoding )
{
    switch ( encoding ) {
    case DirectEncoding::Utf16LE:
        return simdutf::validate_utf16le( asUtf16( line ), line.size() / 2 );
    case DirectEncoding::Utf16BE:
        return simdutf::validate_utf16be( asUtf16( line ), line.size() / 2 );
    default:
        return true;
    }
}

QString decode( std::string_view line, DirectEncoding encoding )
{
    const auto size = static_cast<qsizetype>( line.size() );
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return QString::fromLatin1( QByteArrayView( line ) );
    case DirectEncoding::Utf16LE:
    case DirectEncoding::Utf16BE: {
        QString text( size / 2, Qt::Uninitialized );
        if ( encoding == DirectEncoding::Utf16LE ) {
            qFromLittleEndian<char16_t>( line.data(), size / 2, text.data() );
        }
        else {
            qFromBigEndian<char16_t>( line.data(), size / 2, text.data() );
        }
        return text;
    }
    default:
        return QString::fromUtf8( QByteArrayView( line ) );
    }
}

std::size_t utf8SizeOf( std::string_view line, DirectEncoding encoding )
{
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return simdutf::utf8_length_from_latin1( line.data(), line.size() );
    case DirectEncoding::Utf16LE:
        return simdutf::utf8_length_from_utf16le( asUtf16( line ), line.size() / 2 );
    case DirectEncoding::Utf16BE:
        return simdutf::utf8_length_from_utf16be( asUtf16( line ), line.size() / 2 );
    default:
        return line.size();
    }
}

// Converts a line valid in its encoding; returns the UTF-8 bytes written.
std::size_t convertToUtf8( std::string_view line, DirectEncoding encoding, char* utf8 )
{
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return simdutf::convert_latin1_to_utf8( line.data(), line.size(), utf8 );
    case DirectEncoding::Utf16LE:
        return simdutf::convert_valid_utf16le_to_utf8( asUtf16( line ), line.size() / 2, utf8 );
    case DirectEncoding::Utf16BE:
        return simdutf::convert_valid_utf16be_to_utf8( asUtf16( line ), line.size() / 2, utf8 );
    default:
        std::copy( line.begin(), line.end(), utf8 );
        return line.size();
    }
}

// The UTF-8 of text, in a buffer exactly its size. Invalid UTF-16 is mapped as
// QString::toUtf8() maps it.
QByteArray toUtf8( const QString& text )
{
    const auto* const utf16 = reinterpret_cast<const char16_t*>( text.constData() );
    const auto size = static_cast<std::size_t>( text.size() );
    if ( !simdutf::validate_utf16( utf16, size ) ) {
        return text.toUtf8();
    }
    QByteArray utf8( static_cast<qsizetype>( simdutf::utf8_length_from_utf16( utf16, size ) ),
                     Qt::Uninitialized );
    const auto written = simdutf::convert_valid_utf16_to_utf8( utf16, size, utf8.data() );
    utf8.truncate( static_cast<qsizetype>( written ) );
    return utf8;
}

// The per-line UTF-8 rule: the text of one Log Line in UTF-8, from its bytes
// without its line feed. Bytes isValidAsRead( bytes ) takes, without ANSI
// color sequences to hide, are its text as they are: they are not decoded,
// and the caller takes them as they were read, or converts them straight
// from Latin-1 or UTF-16. Any other Log Line is decoded by decodeLine( bytes
// ), its ANSI color sequences hidden, and converted to UTF-8, which is
// returned. Either way, the caller trims the UTF-8 to the Log Line's text.
template <typename IsValidAsRead, typename DecodeLine>
std::optional<QByteArray>
decodedUtf8LogLine( std::string_view bytes, DirectEncoding encoding, bool hideAnsiColorSequences,
                    const IsValidAsRead& isValidAsRead, const DecodeLine& decodeLine )
{
    const auto hasAnsiColorSequences = hideAnsiColorSequences && containsEscape( bytes, encoding );
    if ( !hasAnsiColorSequences && isValidAsRead( bytes ) ) {
        return std::nullopt;
    }

    auto text = decodeLine( bytes );
    if ( hasAnsiColorSequences ) {
        removeAnsiColorSequences( text );
    }
    return toUtf8( text );
}

// Converts a block of Log Lines in a Latin-1 or UTF-16 encoding to UTF-8 at
// once into utf8, and splits it at each line feed into the text of its Log
// Lines. Does nothing and returns false unless the block is valid in its
// encoding and has lineCount Log Lines.
bool convertValidBlock( std::string_view block, DirectEncoding encoding, std::size_t lineCount,
                        QByteArray& utf8, logsquirl::vector<std::string_view>& lines )
{
    if ( block.empty() ) {
        return false;
    }

    // Validated while converted, into room for the longest UTF-8 it can take:
    // one pass over the block rather than three.
    QByteArray converted( static_cast<qsizetype>( block.size() * 2 ), Qt::Uninitialized );
    std::size_t written = 0;
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        written = simdutf::convert_latin1_to_utf8( block.data(), block.size(), converted.data() );
        break;
    case DirectEncoding::Utf16LE:
        written = simdutf::convert_utf16le_to_utf8( asUtf16( block ), block.size() / 2,
                                                    converted.data() );
        break;
    case DirectEncoding::Utf16BE:
        written = simdutf::convert_utf16be_to_utf8( asUtf16( block ), block.size() / 2,
                                                    converted.data() );
        break;
    default:
        return false;
    }
    if ( written == 0 ) {
        return false;
    }
    converted.truncate( static_cast<qsizetype>( written ) );

    // A line feed is the only code unit whose UTF-8 has a line feed byte.
    logsquirl::vector<std::string_view> split;
    split.reserve( lineCount );
    std::string_view rest( converted.constData(), static_cast<std::size_t>( converted.size() ) );
    for ( auto lineFeed = rest.find( '\n' ); lineFeed != std::string_view::npos;
          lineFeed = rest.find( '\n' ) ) {
        split.push_back( trimToLogLineText( rest.substr( 0, lineFeed ) ) );
        rest.remove_prefix( lineFeed + 1 );
    }
    if ( !rest.empty() ) {
        split.push_back( trimToLogLineText( rest ) );
    }
    if ( split.size() != lineCount ) {
        return false;
    }

    utf8 = std::move( converted );
    lines = std::move( split );
    return true;
}

// Where a Log Line of the block goes in its UTF-8 view.
struct LineToConvert {
    // The Log Line's bytes in the block, without its line feed.
    std::string_view bytes;
    // Its UTF-8, when it had to be decoded first: to hide its ANSI color
    // sequences, or because it is not valid in its encoding.
    std::optional<QByteArray> decoded;
    std::size_t utf8Size{};
    std::size_t utf8Offset{};
};

} // namespace

void appendUtf8LogLineText( std::string& utf8, const ReadLogLine& line,
                            const TextDecoder& textDecoder, bool isUtf8 )
{
    if ( !line.warning.empty() ) {
        utf8.append( line.warning );
        return;
    }

    // What a decoder replaces is not taken as it was read: only ASCII, or
    // UTF-8 in a UTF-8 Log File.
    const auto encoding
        = textDecoder.encodingParams.isUtf8Compatible ? DirectEncoding::Utf8 : DirectEncoding::None;
    const auto decoded = decodedUtf8LogLine(
        line.bytes, encoding, line.hideAnsiColorSequences,
        [ encoding, isUtf8 ]( std::string_view bytes ) {
            return encoding == DirectEncoding::Utf8
                   && ( simdutf::validate_ascii( bytes.data(), bytes.size() )
                        || ( isUtf8 && simdutf::validate_utf8( bytes.data(), bytes.size() ) ) );
        },
        // Decoded on its own, as when it is read alone.
        [ &textDecoder ]( std::string_view bytes ) {
            textDecoder.decoder->resetState();
            return textDecoder.decode( bytes.data(), static_cast<qsizetype>( bytes.size() ) );
        } );

    utf8.append( trimToLogLineText(
        decoded
            ? std::string_view( decoded->constData(), static_cast<std::size_t>( decoded->size() ) )
            : line.bytes ) );
}

logsquirl::vector<std::string_view> RawLines::buildUtf8View() const
{
    logsquirl::vector<std::string_view> lines;
    if ( this->endOfLines.empty() || textDecoder.decoder == nullptr ) {
        return lines;
    }

    // However a Log Line gets into the view, it is trimmed to its text where it
    // is split off: a Search matches it as it is displayed (#522).
    const auto encoding = directEncodingOf( textDecoder.encodingParams );
    const auto codeUnitWidth = isUtf16( encoding ) ? 2 : 1;

    // The known line ends split the block only when they lie in it, on code
    // unit boundaries; a UTF-16 Log Line cut in the middle of a code unit is
    // decoded as a whole block is.
    auto lineEndsSplitTheBlock = encoding != DirectEncoding::None;
    qint64 previousLineEnd = 0;
    for ( const auto lineEnd : endOfLines ) {
        if ( lineEnd < previousLineEnd || lineEnd > logsquirl::ssize( buffer )
             || ( lineEnd - previousLineEnd ) % codeUnitWidth != 0 ) {
            lineEndsSplitTheBlock = false;
            break;
        }
        previousLineEnd = lineEnd;
    }

    try {
        lines.reserve( endOfLines.size() );

        // Every ANSI color sequence starts with the escape character, and in
        // each of these encodings an escape code unit has an escape byte.
        const auto hidesAnsiColorSequences
            = hideAnsiColorSequences
              && std::find( buffer.begin(), buffer.end(), '\x1B' ) != buffer.end();

        // Log Lines are converted one by one only where that is faster than
        // the block (#291): a UTF-8 block is searched where it was read; a
        // block without ANSI color sequences to hide, valid in its encoding,
        // is converted at once; a UTF-16 block whose sequences are hidden is
        // decoded at once, which removes them in one pass.
        const auto convertsLineByLine
            = lineEndsSplitTheBlock && ( !isUtf16( encoding ) || !hidesAnsiColorSequences );
        if ( convertsLineByLine && encoding != DirectEncoding::Utf8 && !hidesAnsiColorSequences
             && convertValidBlock(
                 std::string_view( buffer.data(), static_cast<std::size_t>( endOfLines.back() ) ),
                 encoding, endOfLines.size(), utf8Data_, lines ) ) {
            // Converted as a whole.
        }
        else if ( convertsLineByLine ) {
            std::vector<LineToConvert> toConvert( endOfLines.size() );
            std::size_t utf8Size = 0;
            std::size_t lineStart = 0;
            for ( std::size_t index = 0; index < endOfLines.size(); ++index ) {
                const auto lineEnd = static_cast<std::size_t>( endOfLines[ index ] );
                auto& line = toConvert[ index ];
                line.bytes = withoutLineFeed(
                    std::string_view( buffer.data() + lineStart, lineEnd - lineStart ), encoding );
                lineStart = lineEnd;

                line.decoded = decodedUtf8LogLine(
                    line.bytes, encoding, hidesAnsiColorSequences,
                    [ encoding ]( std::string_view bytes ) { return isValid( bytes, encoding ); },
                    [ encoding ]( std::string_view bytes ) { return decode( bytes, encoding ); } );
                if ( line.decoded ) {
                    line.utf8Size = static_cast<std::size_t>( line.decoded->size() );
                }
                else {
                    line.utf8Size = utf8SizeOf( line.bytes, encoding );
                }

                // A UTF-8 Log Line is searched as it was read, where it was read.
                if ( line.decoded || encoding != DirectEncoding::Utf8 ) {
                    line.utf8Offset = utf8Size;
                    utf8Size += line.utf8Size;
                }
            }

            utf8Data_ = QByteArray( static_cast<qsizetype>( utf8Size ), Qt::Uninitialized );
            for ( auto& line : toConvert ) {
                if ( line.decoded ) {
                    std::copy( line.decoded->cbegin(), line.decoded->cend(),
                               utf8Data_.data() + line.utf8Offset );
                    lines.push_back( trimToLogLineText( std::string_view(
                        utf8Data_.constData() + line.utf8Offset, line.utf8Size ) ) );
                }
                else if ( encoding == DirectEncoding::Utf8 ) {
                    lines.push_back( trimToLogLineText( line.bytes ) );
                }
                else {
                    const auto written
                        = convertToUtf8( line.bytes, encoding, utf8Data_.data() + line.utf8Offset );
                    lines.push_back( trimToLogLineText(
                        std::string_view( utf8Data_.constData() + line.utf8Offset, written ) ) );
                }
            }
        }
        else {
            auto utf16Data = textDecoder.decode( buffer.data(), logsquirl::isize( buffer ) );
            if ( hideAnsiColorSequences ) {
                removeAnsiColorSequences( utf16Data );
            }
            utf8Data_ = toUtf8( utf16Data );

            std::string_view wholeString( utf8Data_.constData(),
                                          static_cast<std::size_t>( utf8Data_.size() ) );
            auto nextLineFeed = wholeString.find( '\n' );
            while ( nextLineFeed != std::string_view::npos ) {
                lines.push_back( trimToLogLineText( wholeString.substr( 0, nextLineFeed ) ) );
                wholeString.remove_prefix( nextLineFeed + 1 );
                nextLineFeed = wholeString.find( '\n' );
            }

            if ( !wholeString.empty() ) {
                lines.push_back( trimToLogLineText( wholeString ) );
            }
        }

    } catch ( const std::exception& e ) {
        LOG_ERROR << "failed to transform lines to utf8 " << e.what();
        lines.clear();
        lines.resize( this->endOfLines.size() );
    }

    return lines;
}
