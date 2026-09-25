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

#include <QChar>
#include <QString>

#include <string_view>

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
constexpr std::string_view trimToLogLineText( std::string_view utf8Line )
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
