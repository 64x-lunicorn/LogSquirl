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
#include <cstdint>
#include <optional>
#include <vector>

#include "abstractlogdata.h"
#include "linetypes.h"

class LogFilteredData;

// Which Log Line each Row of the Table View shows. The one place a Row
// becomes a Log Line and a Log Line its Row: the Table View, its model and
// its delegate all ask the same mapping, so a Row index is never taken for a
// Log Line. The Table View's counterpart of the text view's LineMapping.
class RowMapping {
public:
    using LineType = AbstractLogData::LineType;

    virtual ~RowMapping() = default;

    // How many Rows there are while the Log File has logLines Log Lines.
    virtual int rowCount( LinesCount logLines ) const = 0;
    // The Log Line the Row shows.
    virtual LineNumber logLineAt( int row ) const = 0;
    // The Row showing the Log Line, if any Row does.
    virtual std::optional<int> rowOf( LineNumber logLine ) const = 0;

    // Whether a Log Line is a Match, a Mark or a Context Line of the current
    // Search; plain without one.
    LineType lineType( const LogFilteredData* currentSearch, LineNumber logLine ) const;
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

// The Rows in a Sort Order: Row n shows the n-th Log Line of the order it was
// built with, and a Log Line finds its Row through a reverse index. The order
// is whatever it is handed, so the set of Rows and their order are separate
// concerns, both the builder's: the Rows of a base mapping (every Log Line,
// or the ones a mapping narrows them to, as Matches Only would) sorted by a
// column's keys (sortedLogLines() in tablesortkeys.h). How many Log Lines the
// Log File has makes no difference: a Log Line not in the order has no Row
// until it is merged in.
class SortedRows : public RowMapping {
public:
    // Row n shows logLines[n]; a Log Line at most once.
    explicit SortedRows( std::vector<uint32_t> logLines );

    int rowCount( LinesCount ) const override
    {
        return static_cast<int>( logLines_.size() );
    }

    // The Log Line of a Row; for a Row beyond the last, the last Row's.
    LineNumber logLineAt( int row ) const override;
    std::optional<int> rowOf( LineNumber logLine ) const override;

    // The Log Lines in Row order.
    const std::vector<uint32_t>& logLines() const
    {
        return logLines_;
    }

private:
    std::vector<uint32_t> logLines_;
    // By Log Line, the Row showing it; -1 for none.
    std::vector<int32_t> rowOfLogLine_;
};
