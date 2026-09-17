/*
 * Copyright (C) 2010, 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements Selection.
// This class implements the selection handling. No check is made on
// the validity of the selection, it must be handled by the caller.
// There are three types of selection, only one type might be active
// at any time.

#include <algorithm>
#include <numeric>

#include "abstractlogdata.h"
#include "containers.h"
#include "linemapping.h"
#include "linetypes.h"
#include "log.h"
#include "selection.h"

Selection::Selection()
{
    selectedPartial_.startColumn = 0_lcol;
    selectedPartial_.endColumn = 0_lcol;

    selectedRange_.endLine = 0_lnum;
}

void Selection::selectPortion( LineNumber line, LineColumn startColumn, LineColumn endColumn )
{
    // First unselect any whole line or range
    selectedLine_ = {};
    selectedRange_.startLine = {};

    selectedPartial_.line = line;
    selectedPartial_.startColumn = std::min( startColumn, endColumn );
    selectedPartial_.endColumn = std::max( startColumn, endColumn );
}

void Selection::selectRange( LineNumber startLine, LineNumber endLine )
{
    // First unselect any whole line and portion
    selectedLine_ = {};
    selectedPartial_.line = {};

    selectedRange_.startLine = std::min( startLine, endLine );
    selectedRange_.endLine = std::max( startLine, endLine );

    selectedRange_.firstLine = startLine;
}

void Selection::selectRangeFromPrevious( LineNumber line )
{
    LineNumber previous_line;

    if ( selectedLine_.has_value() )
        previous_line = *selectedLine_;
    else if ( selectedRange_.startLine.has_value() )
        previous_line = selectedRange_.firstLine;
    else if ( selectedPartial_.line.has_value() )
        previous_line = *selectedPartial_.line;
    else
        previous_line = 0_lnum;

    selectRange( previous_line, line );
}

void Selection::crop( LineNumber last_line )
{
    if ( selectedLine_.has_value() && *selectedLine_ > last_line )
        selectedLine_ = {};

    if ( selectedPartial_.line.has_value() && *selectedPartial_.line > last_line )
        selectedPartial_.line = {};

    if ( selectedRange_.endLine > last_line )
        selectedRange_.endLine = last_line;

    if ( selectedRange_.startLine.has_value() && *selectedRange_.startLine > last_line )
        selectedRange_.startLine = last_line;
}

Portion Selection::getPortionForLine( LineNumber line ) const
{
    if ( selectedPartial_.line.has_value() && *selectedPartial_.line == line ) {
        return Portion( *selectedPartial_.line, selectedPartial_.startColumn,
                        selectedPartial_.endColumn );
    }

    return {};
}

bool Selection::isLineSelected( LineNumber line ) const
{
    if ( selectedLine_.has_value() && line == *selectedLine_ )
        return true;
    else if ( selectedRange_.startLine.has_value() )
        return ( ( line >= *selectedRange_.startLine ) && ( line <= selectedRange_.endLine ) );
    else
        return false;
}

bool Selection::isPortionSelected( LineNumber line, LineColumn startColumn,
                                   LineColumn endColumn ) const
{
    if ( isLineSelected( line ) ) {
        return true;
    }

    const auto portion = getPortionForLine( line );
    if ( !portion.isValid() ) {
        return false;
    }

    return startColumn >= portion.startColumn() && endColumn <= portion.endColumn();
}

OptionalLineNumber Selection::selectedLine() const
{
    return selectedLine_;
}

logsquirl::vector<LineNumber> Selection::getLines( const LineMapping& lines ) const
{
    logsquirl::vector<LineNumber> selection;

    if ( selectedLine_.has_value() ) {
        selection.push_back( *selectedLine_ );
    }
    else if ( selectedPartial_.line.has_value() ) {
        selection.push_back( *selectedPartial_.line );
    }
    else if ( selectedRange_.startLine.has_value() ) {
        selection = lines.shownLogLinesFromTo( *selectedRange_.startLine, selectedRange_.endLine );
    }

    return selection;
}

LinesCount Selection::getSelectedLinesCount( const LineMapping& lines ) const
{
    if ( !selectedRange_.startLine.has_value() ) {
        return 0_lcount;
    }

    const auto positions
        = lines.positionsFromTo( *selectedRange_.startLine, selectedRange_.endLine );
    return positions.has_value() ? ( positions->second - positions->first ) + 1_lcount : 0_lcount;
}

std::optional<std::pair<LineNumber, LineNumber>>
Selection::getSelectedPositions( const LineMapping& lines ) const
{
    if ( !selectedRange_.startLine.has_value() ) {
        return std::nullopt;
    }
    return lines.positionsFromTo( *selectedRange_.startLine, selectedRange_.endLine );
}

// The tab behaviour is a bit odd at the moment, full lines are not expanded
// but partials (part of line) are, they probably should not ideally.
QString Selection::getSelectedText( const LineMapping& lines, const AbstractLogData& shownLines,
                                    bool lineNumbers ) const
{
    const auto selectionData = getSelectionWithLineNumbers( lines, shownLines );

    QString text;

    const auto selectionSizeEstimate = std::accumulate(
        selectionData.begin(), selectionData.end(), logsquirl::isize( selectionData ),
        []( const auto& acc, const auto& next ) { return acc + next.second.size(); } );

    text.reserve( selectionSizeEstimate );

    for ( const auto& [ lineNumber, line ] : selectionData ) {
        if ( !text.isEmpty() ) {
#if defined( Q_OS_WIN )
            text.append( QChar::CarriageReturn );
#endif
            text.append( QChar::LineFeed );
        }

        if ( lineNumbers ) {
            text.append( QStringLiteral( "%1: %2" ).arg( lineNumber.get() ).arg( line ) );
        }
        else {
            text.append( line );
        }
    }

    return text;
}

std::map<LineNumber, QString>
Selection::getSelectionWithLineNumbers( const LineMapping& lines,
                                        const AbstractLogData& shownLines ) const
{
    std::map<LineNumber, QString> selectionData;

    if ( selectedLine_.has_value() ) {
        selectionData.emplace( *selectedLine_, lines.logFile().getLineString( *selectedLine_ ) );
    }
    else if ( selectedPartial_.line.has_value() ) {
        selectionData.emplace(
            *selectedPartial_.line,
            lines.logFile()
                .getExpandedLineString( *selectedPartial_.line )
                .mid( selectedPartial_.startColumn.get(), selectedPartial_.size().get() ) );
    }
    else if ( selectedRange_.startLine.has_value() ) {
        const auto positions
            = lines.positionsFromTo( *selectedRange_.startLine, selectedRange_.endLine );
        if ( !positions.has_value() ) {
            return selectionData;
        }

        // Read in one go by position, as the view shows them.
        const auto [ firstPosition, lastPosition ] = *positions;
        const auto text
            = shownLines.getLines( firstPosition, ( lastPosition - firstPosition ) + 1_lcount );
        auto position = firstPosition;
        for ( const auto& line : text ) {
            const auto logLine = lines.logLineAt( position );
            if ( logLine.has_value() ) {
                selectionData.emplace( *logLine, line );
            }
            ++position;
        }
    }

    return selectionData;
}

LineLength SelectedTextLength::of( const Selection& selection, const LineMapping& lines,
                                   const AbstractLogData& shownLines )
{
    if ( selection.isSingleLine() || selection.isPortion() ) {
        // One Log Line: its text is read once either way.
        return LineLength( static_cast<LineLength::UnderlyingType>(
            selection.getSelectedText( lines, shownLines ).size() ) );
    }

    const auto positions = selection.getSelectedPositions( lines );
    if ( !positions.has_value() ) {
        return 0_length;
    }
    const auto [ first, last ] = *positions;

    const bool overlapsMeasured = measured_.has_value() && measured_->shownLines == &shownLines
                                  && first <= measured_->last && measured_->first <= last;
    if ( overlapsMeasured ) {
        auto& measured = *measured_;
        // Add what the range gained and take away what it lost at either end.
        const auto grow = [ & ]( LineNumber from, LineNumber to ) {
            const auto [ count, length ] = measure( shownLines, from, to );
            measured.lines = LinesCount( measured.lines.get() + count.get() );
            measured.length += length;
        };
        const auto shrink = [ & ]( LineNumber from, LineNumber to ) {
            const auto [ count, length ] = measure( shownLines, from, to );
            measured.lines = LinesCount( measured.lines.get() - count.get() );
            measured.length -= length;
        };
        if ( first < measured.first ) {
            grow( first, measured.first - 1_lcount );
        }
        else if ( first > measured.first ) {
            shrink( measured.first, first - 1_lcount );
        }
        if ( last > measured.last ) {
            grow( measured.last + 1_lcount, last );
        }
        else if ( last < measured.last ) {
            shrink( last + 1_lcount, measured.last );
        }
        if ( first != measured.first ) {
            measured.leadingEmptyLines.reset();
        }
        measured.first = first;
        measured.last = last;
    }
    else {
        const auto [ count, length ] = measure( shownLines, first, last );
        measured_ = Measured{ &shownLines, first, last, count, length, std::nullopt };
    }

    if ( measured_->length == 0 ) {
        return 0_length;
    }

    // getSelectedText() joins the Log Lines with a line ending, but only once
    // the text is not empty: none follows the empty Log Lines it starts with.
    if ( !measured_->leadingEmptyLines.has_value() ) {
        measured_->leadingEmptyLines = leadingEmptyLines( shownLines, first, last );
    }
#if defined( Q_OS_WIN )
    constexpr uint64_t LineEndingLength = 2;
#else
    constexpr uint64_t LineEndingLength = 1;
#endif
    const auto lineEndings = measured_->lines.get() - 1 - *measured_->leadingEmptyLines;
    return LineLength( static_cast<LineLength::UnderlyingType>(
        measured_->length + lineEndings * LineEndingLength ) );
}

void SelectedTextLength::forget()
{
    measured_.reset();
}

uint64_t SelectedTextLength::leadingEmptyLines( const AbstractLogData& shownLines, LineNumber first,
                                                LineNumber last )
{
    uint64_t empty = 0;
    for ( auto position = first; position <= last; ++position ) {
        if ( !shownLines.getLineString( position ).isEmpty() ) {
            break;
        }
        ++empty;
    }
    return empty;
}

std::pair<LinesCount, uint64_t> SelectedTextLength::measure( const AbstractLogData& shownLines,
                                                             LineNumber first, LineNumber last )
{
    // Read in bounded batches, so a long range never holds all its text at once.
    constexpr uint64_t BatchLines = 1024;

    uint64_t count = 0;
    uint64_t length = 0;
    for ( auto position = first.get(); position <= last.get(); position += BatchLines ) {
        const auto batch = std::min( BatchLines, last.get() - position + 1 );
        const auto text = shownLines.getLines( LineNumber( position ), LinesCount( batch ) );
        for ( const auto& line : text ) {
            length += static_cast<uint64_t>( line.size() );
        }
        count += text.size();
        if ( text.size() < batch ) {
            break;
        }
    }
    return { LinesCount( count ), length };
}

FilePosition Selection::getNextPosition() const
{
    LineNumber line;
    LineColumn column = 0_lcol;

    if ( selectedLine_.has_value() ) {
        line = *selectedLine_ + 1_lcount;
    }
    else if ( selectedRange_.startLine.has_value() ) {
        line = selectedRange_.endLine + 1_lcount;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = selectedPartial_.endColumn + 1_length;
    }

    return FilePosition( line, column );
}

FilePosition Selection::getPreviousPosition() const
{
    LineNumber line = 0_lnum;
    LineColumn column = 0_lcol;

    if ( selectedLine_.has_value() ) {
        line = *selectedLine_;
    }
    else if ( selectedRange_.startLine.has_value() ) {
        line = *selectedRange_.startLine;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = selectedPartial_.startColumn - 1_length;
    }

    return FilePosition( line, column );
}
