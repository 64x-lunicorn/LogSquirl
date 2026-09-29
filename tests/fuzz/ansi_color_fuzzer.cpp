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

// Fuzz target: every line shown in a view goes through the ANSI color
// sequence filter (#319), or through the parser that keeps their colors,
// which leaves exactly the same text (#573).

#include "ansicolorsequences.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    auto text = QString::fromUtf8( reinterpret_cast<const char*>( data ),
                                   static_cast<qsizetype>( size ) );
    auto parsed = parseAnsiColorSequences( text );
    removeAnsiColorSequences( text );
    if ( parsed.text != text ) {
        std::abort();
    }
    for ( const auto& span : parsed.spans ) {
        if ( span.length <= 0 || span.start < 0 || span.start + span.length > text.size() ) {
            std::abort();
        }
    }
    return 0;
}
