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

#include "linetypes.h"

#include <cstddef>
#include <map>
#include <unordered_map>

// The length of each marked Log Line, remembered when it was marked, so that
// the longest Mark -- how wide the Filtered View may have to scroll -- is known
// again when a Mark is removed without reading any marked Log Line.
class MarkLengths {
public:
    // Remembers the length of a marked Log Line, replacing the one remembered
    // for it before.
    void add( LineNumber line, LineLength length );
    // Forgets the length of a Log Line no longer marked; nothing if none is
    // remembered for it.
    void remove( LineNumber line );
    void clear();

    bool contains( LineNumber line ) const;
    // The length of the longest Mark; 0 without Marks.
    LineLength longest() const;

private:
    std::unordered_map<LineNumber::UnderlyingType, LineLength::UnderlyingType> lengths_;
    // How many Marks have each length.
    std::map<LineLength::UnderlyingType, std::size_t> countsByLength_;
};
