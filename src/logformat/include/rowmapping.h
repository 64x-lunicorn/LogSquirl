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

#include <algorithm>
#include <climits>
#include <optional>

#include "linetypes.h"

// Which Log Line each Row of the Table View shows. The one place a Row
// becomes a Log Line and a Log Line its Row: the Table View, its model and
// its delegate all ask the same mapping, so a Row index is never taken for a
// Log Line.
class RowMapping {
public:
    virtual ~RowMapping() = default;

    // How many Rows there are while the Log File has logLines Log Lines.
    virtual int rowCount( LinesCount logLines ) const = 0;
    // The Log Line the Row shows.
    virtual LineNumber logLineAt( int row ) const = 0;
    // The Row showing the Log Line, if any Row does.
    virtual std::optional<int> rowOf( LineNumber logLine ) const = 0;
};

// Every Log Line in a Row of its own, in order: Row n shows Log Line n.
class OneRowPerLogLine : public RowMapping {
public:
    int rowCount( LinesCount logLines ) const override
    {
        return static_cast<int>( std::min( logLines.get(), static_cast<uint64_t>( INT_MAX ) ) );
    }

    LineNumber logLineAt( int row ) const override
    {
        return LineNumber( static_cast<uint64_t>( std::max( row, 0 ) ) );
    }

    std::optional<int> rowOf( LineNumber logLine ) const override
    {
        if ( logLine.get() > static_cast<uint64_t>( INT_MAX ) ) {
            return std::nullopt;
        }
        return static_cast<int>( logLine.get() );
    }
};
