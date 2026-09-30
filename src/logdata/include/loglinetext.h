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

#pragma once

#include "ansicolorsequences.h"
#include "containers.h"
#include "encodingdetector.h"

#include <QChar>
#include <QString>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

// The text of a Log Line: what the display shows (before untabifying), what a
// Search matches and what the grep CLI prints. It is the Log Line decoded,
// without its line feed and with its ANSI color sequences hidden if the
// Decoding Policy says so, then trimmed here:
//
// - a carriage return ending it is not part of it: a Log File with CRLF line
//   ends reads as the same Log File with LF line ends;
// - the byte order marks starting it are not part of it, on any Log Line:
//   they are never shown. A decoder drops one at the start of what it decodes
//   on its own; dropping every one that is left makes the text the same
//   however a reader decoded it, one Log Line or a whole block at a time.
//
// Every reader of Log Lines trims them with these, whether it decodes them to
// a QString or reads them as UTF-8, so all of them see the same text.
//
// The rest of this unit makes that text from the bytes of Log Lines, in the
// shapes the readers want: a QString, the same with the colors of its ANSI
// color sequences, or UTF-8. Whoever reads the bytes -- the log data, which
// looks at its Index under the Index's lock -- hands them here, a block of
// them (searchblocksource.h) or one Log Line at a time; nothing here takes a
// lock.

inline void trimToLogLineText( QString& decodedLine )
{
    if ( decodedLine.endsWith( QChar::CarriageReturn ) ) {
        decodedLine.chop( 1 );
    }
    qsizetype byteOrderMarks = 0;
    while ( byteOrderMarks < decodedLine.size()
            && decodedLine.at( byteOrderMarks ) == QChar::ByteOrderMark ) {
        ++byteOrderMarks;
    }
    if ( byteOrderMarks > 0 ) {
        decodedLine.remove( 0, byteOrderMarks );
    }
}

// The same, for a Log Line in UTF-8 (without its line feed): only the view
// changes, nothing is copied.
[[nodiscard]] constexpr std::string_view trimToLogLineText( std::string_view utf8Line )
{
    constexpr std::string_view Utf8ByteOrderMark = "\xEF\xBB\xBF";
    if ( !utf8Line.empty() && utf8Line.back() == '\r' ) {
        utf8Line.remove_suffix( 1 );
    }
    while ( utf8Line.starts_with( Utf8ByteOrderMark ) ) {
        utf8Line.remove_prefix( Utf8ByteOrderMark.size() );
    }
    return utf8Line;
}

// What a Log Line reads as when it cannot be read.
inline constexpr std::string_view LineTooLongWarning = "LOGSQUIRL WARNING: this line is too long";
inline constexpr std::string_view FileReadFailedWarning = "LOGSQUIRL WARNING: file read failed";
inline constexpr std::string_view NotEnoughMemoryWarning = "LOGSQUIRL WARNING: not enough memory";
inline constexpr std::string_view LinesNotReadWarning
    = "LOGSQUIRL WARNING: failed to read some lines before this one";
inline constexpr std::string_view LinesNotDecodedWarning
    = "LOGSQUIRL WARNING: failed to decode some lines before this one";

// A Log Line this long or longer is not decoded: it reads as
// LineTooLongWarning.
inline constexpr qint64 MaxLogLineLength = std::numeric_limits<int>::max() / 2;

// A warning as the text of a Log Line.
QString warningText( std::string_view warning );

// Tells the log that a Log Line could not be made for want of memory.
void logNotEnoughMemory();

// A Log Line decoded (without its line feed), as its text: its ANSI color
// sequences removed when hideAnsiColorSequences, then trimmed.
QString logLineText( QString&& decodedLine, bool hideAnsiColorSequences );

// A Log Line decoded, as its text under a Decoding Policy that hides ANSI
// color sequences, with the colors they ask for, whatever the Decoding
// Policy is: what the trimming cuts off is cut off the colors too. Takes
// hideAnsiColorSequences only to be called as logLineText() is.
AnsiColoredText ansiColoredLogLineText( QString&& decodedLine, bool hideAnsiColorSequences );

// One Log Line as a reader read it on its own, not in a block.
struct ReadLogLine {
    // Where it was asked for in the Log Lines read.
    std::size_t request = 0;
    // Its bytes, without its line feed; empty when it could not be read.
    std::string_view bytes;
    // What it reads as when it could not be read; empty otherwise.
    std::string_view warning;
    bool hideAnsiColorSequences = false;
};

// The Log Line that starts at begin in the bytesRead bytes read, and ends
// length bytes later (without its line feed): its bytes, or the warning it
// reads as when it is too long or reaches past what was read.
ReadLogLine readLogLine( std::size_t request, std::string_view bytesRead, qint64 begin,
                         qint64 length, bool hideAnsiColorSequences );

// The text of count Log Lines asked for and read one at a time: for each
// request, the Log Line readLines( onLine ) calls onLine( const ReadLogLine& )
// for, in any order, made a Line by toLine( QString&& decodedLine, bool
// hideAnsiColorSequences ) -- logLineText(), ansiColoredLogLineText() or one
// built on them. Each Log Line is decoded on its own, as when it is read
// alone: a character cut short at the end of one does not reach the next, and
// a byte order mark starting any of them is dropped. A Log Line not called
// for reads as LinesNotReadWarning; when memory runs out, every Log Line not
// made yet reads as NotEnoughMemoryWarning.
template <typename Line, typename ToLine, typename ReadLines>
logsquirl::vector<Line> decodeReadLogLines( const TextCodecHolder& codec, std::size_t count,
                                            ReadLines&& readLines, ToLine toLine )
{
    logsquirl::vector<Line> text( count );
    logsquirl::vector<bool> isRead( count, false );

    try {
        // One decoder for the whole read: making one costs a converter, dear
        // for the legacy Encodings. Its state is reset for every Log Line.
        const auto textDecoder = codec.makeDecoder();
        readLines( [ & ]( const ReadLogLine& line ) {
            if ( !line.warning.empty() ) {
                text[ line.request ] = Line{ warningText( line.warning ) };
            }
            else {
                textDecoder.decoder->resetState();
                text[ line.request ]
                    = toLine( textDecoder.decode( line.bytes.data(),
                                                  static_cast<qsizetype>( line.bytes.size() ) ),
                              line.hideAnsiColorSequences );
            }
            isRead[ line.request ] = true;
        } );
    } catch ( const std::bad_alloc& ) {
        logNotEnoughMemory();
        for ( std::size_t request = 0; request < count; ++request ) {
            if ( !isRead[ request ] ) {
                text[ request ] = Line{ warningText( NotEnoughMemoryWarning ) };
                isRead[ request ] = true;
            }
        }
    }

    for ( std::size_t request = 0; request < count; ++request ) {
        if ( !isRead[ request ] ) {
            text[ request ] = Line{ warningText( LinesNotReadWarning ) };
        }
    }

    return text;
}

// Where the UTF-8 text of one Log Line asked for lies in what was decoded.
struct Utf8LogLinePlace {
    std::size_t begin = 0;
    std::size_t size = 0;
    bool isRead = false;
};

// Appends the text of a Log Line read on its own, in UTF-8, to utf8: byte for
// byte its text as a QString converted to UTF-8. A Log Line that is UTF-8 in a
// UTF-8 Log File, or ASCII in an ASCII compatible one, is copied as it was
// read and never decoded; any other is decoded on its own with textDecoder.
void appendUtf8LogLineText( std::string& utf8, const ReadLogLine& line,
                            const TextDecoder& textDecoder, bool isUtf8 );

// The UTF-8 texts decoded, each where placed says, put in the order of the
// requests; a Log Line not read reads as LinesNotReadWarning.
std::string utf8LogLineTextsInRequestOrder( std::string&& decoded,
                                            const logsquirl::vector<Utf8LogLinePlace>& placed );

// Every one of count Log Lines as NotEnoughMemoryWarning, in UTF-8.
std::string utf8NotEnoughMemory( std::size_t count );

// As decodeReadLogLines(), in UTF-8: the text of each Log Line asked for,
// followed by a line feed, in the order asked.
template <typename ReadLines>
std::string decodeReadLogLinesToUtf8( const TextCodecHolder& codec, std::size_t count,
                                      ReadLines&& readLines )
{
    // The text of each Log Line lands in decoded in the order the Log Lines
    // are read; where each one lies is kept by request.
    std::string decoded;
    logsquirl::vector<Utf8LogLinePlace> placed( count );

    try {
        const bool isUtf8 = codec.mibEnum() == TextEncoding::Utf8Mib;
        const auto textDecoder = codec.makeDecoder();
        readLines( [ & ]( const ReadLogLine& line ) {
            const auto begin = decoded.size();
            appendUtf8LogLineText( decoded, line, textDecoder, isUtf8 );
            decoded += '\n';
            placed[ line.request ] = Utf8LogLinePlace{ begin, decoded.size() - begin, true };
        } );
    } catch ( const std::bad_alloc& ) {
        logNotEnoughMemory();
        return utf8NotEnoughMemory( count );
    }

    return utf8LogLineTextsInRequestOrder( std::move( decoded ), placed );
}
