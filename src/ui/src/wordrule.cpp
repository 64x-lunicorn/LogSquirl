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

#include "wordrule.h"

namespace {

bool isWordCharacter( QChar character )
{
    return character.isLetterOrNumber() || character.category() == QChar::Punctuation_Connector;
}

} // namespace

std::optional<std::pair<int, int>> wordAt( QStringView text, int position )
{
    const auto length = static_cast<int>( text.size() );
    if ( position < 0 || position >= length || !isWordCharacter( text[ position ] ) ) {
        return std::nullopt;
    }

    int start = position;
    while ( start > 0 && isWordCharacter( text[ start - 1 ] ) ) {
        --start;
    }
    int end = position + 1;
    while ( end < length && isWordCharacter( text[ end ] ) ) {
        ++end;
    }
    return std::pair{ start, end };
}
