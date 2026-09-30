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

#ifndef LOGSQUIRL_SEARCH_RESULTS_H
#define LOGSQUIRL_SEARCH_RESULTS_H

// What a Search run is known by and what it finds, without the worker that
// runs it: whoever only holds or reads Matches and Displayed Lines includes
// this rather than the worker's header.

#ifndef Q_MOC_RUN
#include <roaring64map.hh>
#endif

#include "linetypes.h"
#include "runid.h"

// Identifies one Search run: the id of the Background Run it is. A new run
// gets a fresh id; whatever owns the worker compares an incoming result's id
// against the id it is currently waiting on to tell a result belonging to a
// superseded run from a live one.
using SearchId = RunId;

// This is an array of matching lines.
// It shall be implemented for random lookup speed, so
// a fixed "in-place" array (vector) is probably fine.
using SearchResultArray = roaring::Roaring64Map;

// The line at position in lines, counting from 0 in their order; none at or
// past their end.
inline OptionalLineNumber lineAtPosition( const SearchResultArray& lines, LineNumber position )
{
    LineNumber::UnderlyingType line = {};
    if ( !lines.select( position.get(), &line ) ) {
        return {};
    }
    return LineNumber( line );
}

#endif
