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

// Fuzz target: a Log Format file is a JSON document from anywhere, and the
// regular expressions in it run over every line of a Log File (#319). The
// input is the document, then a NUL, then the line to extract fields from.

#include "logfieldextractor.h"
#include "logformatparser.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    const auto* bytes = reinterpret_cast<const char*>( data );
    const auto* separator = static_cast<const char*>( std::memchr( bytes, '\0', size ) );
    const auto documentSize = separator ? static_cast<std::size_t>( separator - bytes ) : size;

    const std::string document( bytes, documentSize );
    const auto line = separator ? QString::fromUtf8( separator + 1, static_cast<qsizetype>( size - documentSize - 1 ) )
                                : QString();

    const auto formats = LogFormatParser::parseJsonString( document.c_str() );
    for ( const auto& format : formats ) {
        const LogFieldExtractor extractor( format );
        extractor.columnNames();
        extractor.extractFields( line );
    }
    return 0;
}
