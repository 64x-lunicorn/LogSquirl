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

#ifndef LINEMAPPING_H
#define LINEMAPPING_H

#include <optional>
#include <utility>

#include "abstractlogdata.h"
#include "containers.h"
#include "linessaver.h"
#include "linetypes.h"
#include "quickfind.h"

class LogFilteredData;

// Which Log Line each position of a text view shows, and what the view needs
// to know about the Log Lines it shows. The text view's counterpart of the
// Table View's RowMapping: the main view shows every Log Line at its own
// position, the Filtered View its Displayed Lines one after another.
//
// Only the Scroll Position counts positions (ADR 0001); the selection, Marks,
// Search Limits, QuickFind and the Line Verdicts are Log Lines, and the text
// view asks this mapping wherever one meets the other.
class LineMapping {
public:
    using LineType = AbstractLogData::LineType;

    virtual ~LineMapping() = default;

    // The Log Line shown at position, if there is one.
    virtual OptionalLineNumber logLineAt( LineNumber position ) const = 0;
    // The position of logLine when it is shown. For a Log Line not shown, the
    // position of the last one shown before it, or the first position when
    // none is -- or, where every Log Line has a position of its own, its own.
    virtual LineNumber nearestPositionOf( LineNumber logLine ) const = 0;

    // Whether a Log Line is a Match, a Mark or a Context Line.
    virtual LineType lineType( LineNumber logLine ) const = 0;
    // How many Log Lines the Log File has: the largest line number the view
    // can show.
    virtual LinesCount logLineCount() const = 0;
    // The first Mark strictly after logLine, and the last strictly before
    // it, whether shown or not.
    virtual OptionalLineNumber markAfter( LineNumber logLine ) const = 0;
    virtual OptionalLineNumber markBefore( LineNumber logLine ) const = 0;

    // The Log File the text of the Log Lines is read from.
    virtual const AbstractLogData& logFile() const = 0;
    // The Log Lines a QuickFind searches: a copy of those shown, taken on the
    // UI thread when it starts (ADR 0002).
    virtual QuickFindLines quickFindLines() const = 0;
    // Reads the text of the lines shown, by position, off the UI thread: taken
    // on the UI thread when a save starts, it doesn't see what is shown
    // afterwards.
    virtual DisplayedLinesReader linesToSave() const = 0;

    // Whether logLine is shown.
    bool shows( LineNumber logLine ) const;
    // The shown Log Line nearest to logLine: itself when shown, else the last
    // one shown before it, else the first one shown. None if nothing is shown.
    OptionalLineNumber nearestShownLogLine( LineNumber logLine ) const;
    // The positions of the Log Lines shown from first through last, both
    // Log Lines included; none when no Log Line in between is shown.
    std::optional<std::pair<LineNumber, LineNumber>> positionsFromTo( LineNumber first,
                                                                      LineNumber last ) const;
    // The Log Lines shown from first through last, in order.
    logsquirl::vector<LineNumber> shownLogLinesFromTo( LineNumber first, LineNumber last ) const;
    // The nearest Mark shown after logLine, and before it.
    OptionalLineNumber shownMarkAfter( LineNumber logLine ) const;
    OptionalLineNumber shownMarkBefore( LineNumber logLine ) const;
};

// Every Log Line of a Log File at its own position: the main view. Its Marks
// and line types are those of a Search's Displayed Lines, when there is one.
class EveryLogLine : public LineMapping {
public:
    // logFile, and filteredData when not null, must outlive this mapping.
    explicit EveryLogLine( const AbstractLogData* logFile,
                           const LogFilteredData* filteredData = nullptr );

    OptionalLineNumber logLineAt( LineNumber position ) const override;
    LineNumber nearestPositionOf( LineNumber logLine ) const override;
    LineType lineType( LineNumber logLine ) const override;
    LinesCount logLineCount() const override;
    OptionalLineNumber markAfter( LineNumber logLine ) const override;
    OptionalLineNumber markBefore( LineNumber logLine ) const override;
    const AbstractLogData& logFile() const override;
    QuickFindLines quickFindLines() const override;
    DisplayedLinesReader linesToSave() const override;

private:
    const AbstractLogData* logFile_;
    const LogFilteredData* filteredData_;
};

// The Displayed Lines of a Search, one after another: the Filtered View.
class FilteredViewLines : public LineMapping {
public:
    // filteredData must outlive this mapping.
    explicit FilteredViewLines( const LogFilteredData* filteredData );

    OptionalLineNumber logLineAt( LineNumber position ) const override;
    LineNumber nearestPositionOf( LineNumber logLine ) const override;
    LineType lineType( LineNumber logLine ) const override;
    LinesCount logLineCount() const override;
    OptionalLineNumber markAfter( LineNumber logLine ) const override;
    OptionalLineNumber markBefore( LineNumber logLine ) const override;
    const AbstractLogData& logFile() const override;
    QuickFindLines quickFindLines() const override;
    DisplayedLinesReader linesToSave() const override;

private:
    const LogFilteredData* filteredData_;
};

#endif
