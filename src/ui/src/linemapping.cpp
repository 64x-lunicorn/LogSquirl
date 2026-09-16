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

#include "linemapping.h"

#include <memory>

#include "logdata.h"
#include "logfiltereddata.h"

bool LineMapping::shows( LineNumber logLine ) const
{
    return logLineAt( nearestPositionOf( logLine ) ) == logLine;
}

OptionalLineNumber LineMapping::nearestShownLogLine( LineNumber logLine ) const
{
    return logLineAt( nearestPositionOf( logLine ) );
}

std::optional<std::pair<LineNumber, LineNumber>>
LineMapping::positionsFromTo( LineNumber first, LineNumber last ) const
{
    if ( last < first ) {
        return std::nullopt;
    }

    auto firstPosition = nearestPositionOf( first );
    const auto atFirst = logLineAt( firstPosition );
    if ( !atFirst.has_value() ) {
        return std::nullopt;
    }
    if ( *atFirst < first ) {
        // The last Log Line shown before first: the one after it is the first
        // shown from first on.
        firstPosition = firstPosition + 1_lcount;
    }

    const auto lastPosition = nearestPositionOf( last );
    const auto atLast = logLineAt( lastPosition );
    if ( !atLast.has_value() || *atLast > last || lastPosition < firstPosition ) {
        return std::nullopt;
    }

    return std::pair{ firstPosition, lastPosition };
}

logsquirl::vector<LineNumber> LineMapping::shownLogLinesFromTo( LineNumber first,
                                                                LineNumber last ) const
{
    logsquirl::vector<LineNumber> logLines;

    const auto positions = positionsFromTo( first, last );
    if ( !positions.has_value() ) {
        return logLines;
    }

    const auto [ firstPosition, lastPosition ] = *positions;
    logLines.reserve( ( lastPosition - firstPosition ).get() + 1 );
    for ( auto position = firstPosition; position <= lastPosition; ++position ) {
        const auto logLine = logLineAt( position );
        if ( logLine.has_value() ) {
            logLines.push_back( *logLine );
        }
    }

    return logLines;
}

OptionalLineNumber LineMapping::shownMarkAfter( LineNumber logLine ) const
{
    auto mark = markAfter( logLine );
    while ( mark.has_value() && !shows( *mark ) ) {
        mark = markAfter( *mark );
    }
    return mark;
}

OptionalLineNumber LineMapping::shownMarkBefore( LineNumber logLine ) const
{
    auto mark = markBefore( logLine );
    while ( mark.has_value() && !shows( *mark ) ) {
        mark = markBefore( *mark );
    }
    return mark;
}

EveryLogLine::EveryLogLine( const AbstractLogData* logFile, const LogFilteredData* filteredData )
    : logFile_( logFile )
    , filteredData_( filteredData )
{
}

OptionalLineNumber EveryLogLine::logLineAt( LineNumber position ) const
{
    if ( position < logFile_->getNbLine() ) {
        return position;
    }
    return std::nullopt;
}

LineNumber EveryLogLine::nearestPositionOf( LineNumber logLine ) const
{
    // Every Log Line has its own position, also one the Log File hasn't
    // indexed yet: a view can be sent to it before it arrives.
    return logLine;
}

EveryLogLine::LineType EveryLogLine::lineType( LineNumber logLine ) const
{
    if ( filteredData_ != nullptr ) {
        return filteredData_->lineTypeByLine( logLine );
    }
    return AbstractLogData::LineTypeFlags::Plain;
}

LinesCount EveryLogLine::logLineCount() const
{
    return logFile_->getNbLine();
}

OptionalLineNumber EveryLogLine::markAfter( LineNumber logLine ) const
{
    if ( filteredData_ != nullptr ) {
        return filteredData_->getMarkAfter( logLine );
    }
    return std::nullopt;
}

OptionalLineNumber EveryLogLine::markBefore( LineNumber logLine ) const
{
    if ( filteredData_ != nullptr ) {
        return filteredData_->getMarkBefore( logLine );
    }
    return std::nullopt;
}

const AbstractLogData& EveryLogLine::logFile() const
{
    return *logFile_;
}

QuickFindLines EveryLogLine::quickFindLines() const
{
    return QuickFindLines::everyLogLine( *logFile_ );
}

DisplayedLinesReader EveryLogLine::linesToSave() const
{
    return [ logFile = logFile_ ]( LineNumber first, LinesCount count ) {
        return logFile->getLines( first, count );
    };
}

FilteredViewLines::FilteredViewLines( const LogFilteredData* filteredData )
    : filteredData_( filteredData )
{
}

OptionalLineNumber FilteredViewLines::logLineAt( LineNumber position ) const
{
    if ( position < filteredData_->getNbLine() ) {
        return filteredData_->getMatchingLineNumber( position );
    }
    return std::nullopt;
}

LineNumber FilteredViewLines::nearestPositionOf( LineNumber logLine ) const
{
    return filteredData_->getLineIndexNumber( logLine );
}

FilteredViewLines::LineType FilteredViewLines::lineType( LineNumber logLine ) const
{
    return filteredData_->lineTypeByLine( logLine );
}

LinesCount FilteredViewLines::logLineCount() const
{
    return filteredData_->getNbTotalLines();
}

OptionalLineNumber FilteredViewLines::markAfter( LineNumber logLine ) const
{
    return filteredData_->getMarkAfter( logLine );
}

OptionalLineNumber FilteredViewLines::markBefore( LineNumber logLine ) const
{
    return filteredData_->getMarkBefore( logLine );
}

const AbstractLogData& FilteredViewLines::logFile() const
{
    return filteredData_->sourceLogData();
}

QuickFindLines FilteredViewLines::quickFindLines() const
{
    // The worker reads the Log File's text, which is safe off the UI thread,
    // and never the LogFilteredData, which the UI thread goes on changing.
    return QuickFindLines::someLogLines( filteredData_->sourceLogData(),
                                         filteredData_->copyDisplayedLines() );
}

DisplayedLinesReader FilteredViewLines::linesToSave() const
{
    // As for QuickFind, the save reads the Log File's text and never the
    // LogFilteredData, which the UI thread goes on changing.
    return [ logFile = &filteredData_->sourceLogData(),
             lines = std::make_shared<const SearchResultArray>(
                 filteredData_->copyDisplayedLines() ) ]( LineNumber first, LinesCount count ) {
        logsquirl::vector<QString> text;
        text.reserve( count.get() );
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            const auto logLine = lineAtPosition( *lines, LineNumber( position ) );
            text.push_back( logLine.has_value() ? logFile->getLineString( *logLine ) : QString{} );
        }
        return text;
    };
}
