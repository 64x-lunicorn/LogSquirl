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

#include "marklengths.h"

void MarkLengths::add( LineNumber line, LineLength length )
{
    remove( line );
    lengths_.emplace( line.get(), length.get() );
    ++countsByLength_[ length.get() ];
}

void MarkLengths::remove( LineNumber line )
{
    const auto remembered = lengths_.find( line.get() );
    if ( remembered == lengths_.end() ) {
        return;
    }

    const auto count = countsByLength_.find( remembered->second );
    if ( --count->second == 0 ) {
        countsByLength_.erase( count );
    }
    lengths_.erase( remembered );
}

void MarkLengths::clear()
{
    lengths_.clear();
    countsByLength_.clear();
}

bool MarkLengths::contains( LineNumber line ) const
{
    return lengths_.contains( line.get() );
}

LineLength MarkLengths::longest() const
{
    return countsByLength_.empty() ? 0_length : LineLength( countsByLength_.rbegin()->first );
}
