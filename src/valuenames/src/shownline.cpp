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

#include "shownline.h"

#include <algorithm>
#include <iterator>

namespace logsquirl::valuenames {

ShownLine::ShownLine( QStringView rawLine, QList<NamedValue> namedValues )
    : text_( shownLine( rawLine, namedValues ) )
    , namedValues_( std::move( namedValues ) )
{
    shownStarts_.reserve( static_cast<size_t>( namedValues_.size() ) );
    qsizetype shift = 0;
    for ( const auto& value : namedValues_ ) {
        shownStarts_.push_back( value.start + shift );
        shift += value.shown.size() - value.length;
    }
}

qsizetype ShownLine::shownStart( qsizetype index ) const
{
    return shownStarts_[ static_cast<size_t>( index ) ];
}

qsizetype ShownLine::shownEnd( qsizetype index ) const
{
    return shownStart( index ) + namedValues_[ index ].shown.size();
}

qsizetype ShownLine::toShown( qsizetype rawColumn, Snap snap ) const
{
    // The first Named Value that ends after the column: every one before it
    // lies wholly before the column.
    const auto next = std::upper_bound(
        namedValues_.cbegin(), namedValues_.cend(), rawColumn,
        []( qsizetype column, const NamedValue& value ) { return column < value.end(); } );
    const auto index = std::distance( namedValues_.cbegin(), next );

    if ( next != namedValues_.cend() && next->start < rawColumn ) {
        return snap == Snap::ToStart ? shownStart( index ) : shownEnd( index );
    }
    if ( index == 0 ) {
        return rawColumn;
    }
    return rawColumn - namedValues_[ index - 1 ].end() + shownEnd( index - 1 );
}

qsizetype ShownLine::firstShownEndingAfter( qsizetype shownColumn ) const
{
    // The texts shown are ordered as the values are, so their ends are too.
    qsizetype first = 0;
    qsizetype last = namedValues_.size();
    while ( first < last ) {
        const auto middle = first + ( last - first ) / 2;
        if ( shownEnd( middle ) <= shownColumn ) {
            first = middle + 1;
        }
        else {
            last = middle;
        }
    }
    return first;
}

qsizetype ShownLine::toRaw( qsizetype shownColumn, Snap snap ) const
{
    const auto index = firstShownEndingAfter( shownColumn );
    if ( index < namedValues_.size() && shownStart( index ) < shownColumn ) {
        const auto& value = namedValues_[ index ];
        return snap == Snap::ToStart ? value.start : value.end();
    }
    if ( index == 0 ) {
        return shownColumn;
    }
    return shownColumn - shownEnd( index - 1 ) + namedValues_[ index - 1 ].end();
}

qsizetype ShownLine::valueAtRaw( qsizetype rawColumn ) const
{
    const auto next = std::upper_bound(
        namedValues_.cbegin(), namedValues_.cend(), rawColumn,
        []( qsizetype column, const NamedValue& value ) { return column < value.end(); } );
    if ( next == namedValues_.cend() || next->start > rawColumn ) {
        return -1;
    }
    return std::distance( namedValues_.cbegin(), next );
}

qsizetype ShownLine::valueAtShown( qsizetype shownColumn ) const
{
    const auto index = firstShownEndingAfter( shownColumn );
    return index < namedValues_.size() && shownStart( index ) <= shownColumn ? index : -1;
}

std::pair<qsizetype, qsizetype> ShownLine::wholeRawRange( qsizetype start, qsizetype end ) const
{
    if ( end <= start ) {
        return { start, end };
    }
    const auto first = valueAtRaw( start );
    const auto last = valueAtRaw( end - 1 );
    return { first >= 0 ? namedValues_[ first ].start : start,
             last >= 0 ? namedValues_[ last ].end() : end };
}

} // namespace logsquirl::valuenames
