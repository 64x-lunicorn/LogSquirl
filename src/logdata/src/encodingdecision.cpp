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

#include "encodingdecision.h"

#include "encodingdetector.h"
#include "textencoding.h"

namespace {

// How an Encoding splits a Log File into Log Lines: the width of its line
// feed and where the line feed byte sits in it.
EncodingParameters parameters( int mib )
{
    return EncodingParameters( TextEncoding::forMib( mib ) );
}

} // namespace

EncodingDecision decideEncoding( std::optional<int> settled, std::optional<int> chosen,
                                 std::optional<int> detected, std::optional<int> indexForced,
                                 std::optional<int> indexGuess )
{
    // The Encoding chosen, else the one detected; before anything is
    // detected, the locale's.
    const auto shown = chosen.value_or( detected.value_or( TextEncoding::forLocale()->mibEnum() ) );

    // Settled after every load: a Log File that only grew keeps what was
    // read in it.
    if ( settled == shown ) {
        return EncodingDecision{ shown, EncodingChange::Unchanged };
    }

    // The Index was built in the Encoding forced on it, else in the one it
    // guessed. Only an Encoding that splits the Log File into Log Lines
    // differently is worth indexing it again for; one that differs only in
    // name decodes the same Log Lines differently.
    const auto indexed = indexForced.has_value() ? indexForced : indexGuess;
    if ( indexed.has_value() && parameters( shown ) != parameters( *indexed ) ) {
        // Back to the Encoding detected: the index detects it anew rather
        // than being forced to it, so it follows the Log File as before.
        return EncodingDecision{ shown, shown == indexGuess ? EncodingChange::ReindexDetecting
                                                            : EncodingChange::ReindexForced };
    }

    return EncodingDecision{ shown, EncodingChange::Redecode };
}

std::optional<int> encodingMib( const TextEncoding* encoding )
{
    if ( encoding == nullptr ) {
        return std::nullopt;
    }
    return encoding->mibEnum();
}
