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
#include <utility>
#include <vector>

#include "abstractlogdata.h"
#include "linemapping.h"

// Shows the Log Lines in shown, in order, of a Log File: a Filtered View's
// LineMapping without a Search. The Log Lines in marks are Marks, shown or
// not; every other one is a Match.
class VectorLines : public LineMapping {
public:
    VectorLines( const AbstractLogData* logFile, std::vector<uint64_t> shown,
                 std::vector<uint64_t> marks = {} )
        : logFile_( logFile )
        , shown_( std::move( shown ) )
        , marks_( std::move( marks ) )
    {
    }

    OptionalLineNumber logLineAt( LineNumber position ) const override
    {
        if ( position.get() < shown_.size() ) {
            return LineNumber( shown_[ position.get() ] );
        }
        return std::nullopt;
    }

    LineNumber nearestPositionOf( LineNumber logLine ) const override
    {
        const auto after = std::upper_bound( shown_.begin(), shown_.end(), logLine.get() );
        const auto shownUpTo = static_cast<uint64_t>( after - shown_.begin() );
        return LineNumber( shownUpTo > 0 ? shownUpTo - 1 : 0 );
    }

    LineType lineType( LineNumber logLine ) const override
    {
        return std::ranges::find( marks_, logLine.get() ) != marks_.end()
                   ? LineType{ AbstractLogData::LineTypeFlags::Mark }
                   : LineType{ AbstractLogData::LineTypeFlags::Match };
    }

    LinesCount logLineCount() const override
    {
        return logFile_->getNbLine();
    }

    OptionalLineNumber markAfter( LineNumber logLine ) const override
    {
        const auto mark = std::upper_bound( marks_.begin(), marks_.end(), logLine.get() );
        return mark != marks_.end() ? OptionalLineNumber( LineNumber( *mark ) ) : std::nullopt;
    }

    OptionalLineNumber markBefore( LineNumber logLine ) const override
    {
        const auto mark = std::lower_bound( marks_.begin(), marks_.end(), logLine.get() );
        return mark != marks_.begin() ? OptionalLineNumber( LineNumber( *std::prev( mark ) ) )
                                      : std::nullopt;
    }

    const AbstractLogData& logFile() const override
    {
        return *logFile_;
    }

    QuickFindLines quickFindLines() const override
    {
        SearchResultArray lines;
        for ( const auto line : shown_ ) {
            lines.add( line );
        }
        return QuickFindLines::someLogLines( *logFile_, std::move( lines ) );
    }

    DisplayedLinesReader linesToSave() const override
    {
        return [ this ]( LineNumber first, LinesCount count ) {
            logsquirl::vector<QString> text;
            for ( auto position = first; position < first + count; ++position ) {
                const auto logLine = logLineAt( position );
                text.push_back( logLine ? logFile_->getLineString( *logLine ) : QString{} );
            }
            return text;
        };
    }

private:
    const AbstractLogData* logFile_;
    std::vector<uint64_t> shown_;
    std::vector<uint64_t> marks_;
};
