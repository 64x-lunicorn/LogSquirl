/*
 * Copyright (C) 2026 LogSquirl contributors
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

#ifndef LOGSQUIRL_ICONVCONVERTER_H
#define LOGSQUIRL_ICONVCONVERTER_H

#include <QStringConverter>
#include <QStringDecoder>
#include <QStringEncoder>

#include <memory>

// The Qt macOS packages are built without ICU and iconv, so their
// QStringConverter knows only the Unicode Encodings and Latin-1. The Encodings
// the deprecated Qt 5 codec classes used to bring along (windows-125x, KOI8,
// Big5, Shift_JIS, ...) are converted with the iconv every macOS ships instead.
//
// Every Encoding of the table has its own converter functions, because a
// Qt converter function is handed nothing but its State and a State forgets
// what it was for once QStringDecoder::resetState() is called.
namespace iconv_converter {

// The index of the Encoding with this name (as TextEncoding names it) if
// iconv converts it, or -1.
int indexForName( const char* encodingName );

std::unique_ptr<QStringDecoder> makeDecoder( int index, QStringConverter::Flags flags );
QStringEncoder makeEncoder( int index );

} // namespace iconv_converter

#endif // LOGSQUIRL_ICONVCONVERTER_H
