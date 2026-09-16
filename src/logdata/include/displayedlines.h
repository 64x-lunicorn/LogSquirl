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

#ifndef LOGSQUIRL_DISPLAYED_LINES_H
#define LOGSQUIRL_DISPLAYED_LINES_H

#include <functional>

#include "abstractlogdata.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"

// The Displayed Lines: the Log Lines the Filtered View shows, in order --
// the Matches, the Marks and, while they are shown, the Context Lines around
// them. The Marks and the Context Lines are owned here; the Matches are the
// Search Session's, read in place through a reference and never copied.
//
// A plain object for the UI thread: it neither locks nor signals. Whoever
// changes the Matches it reads tells it so (matchesArrived(),
// searchCompleted(), searchDiscarded()) before anything reads it again, and
// a reader on another thread takes a copy of lines() on the UI thread first
// (ADR 0002).
class DisplayedLines {
public:
    using LineType = AbstractLogData::LineType;
    using LineTypeFlags = AbstractLogData::LineTypeFlags;

    // matches must outlive this object. nbLogLines tells how many Log Lines
    // the Log File has now, so Context Lines never reach past its end.
    DisplayedLines( const SearchResultArray& matches, std::function<LinesCount()> nbLogLines,
                    int contextLinesCount );

    // Which kinds of Log Line are displayed: Match, Mark and Context. With
    // Matches hidden the Marks are displayed, whatever the Mark flag says.
    // Matches and Marks are shown until told otherwise.
    void setShown( LineType shown );
    LineType shown() const;

    // How many Log Lines either side of a Match or Mark are Context Lines.
    // Rebuilds them if -- and only if -- the count changed.
    void setContextLinesCount( int contextLinesCount );

    // The Matches grew or were replaced while a Search runs, or were kept
    // when it stopped. The Context Lines stay as they are until the Search
    // completes.
    void matchesArrived();
    // The Search completed (from a real run or from the cache): the Context
    // Lines are rebuilt around its Matches.
    void searchCompleted();
    // The Search was cleared, its pattern was invalid or it failed: there
    // are no Matches, and the Context Lines are dropped with them.
    void searchDiscarded();

    // Adds a Mark; false if the Log Line was already marked.
    bool addMark( LineNumber line );
    // Removes a Mark; false if the Log Line was not marked.
    bool removeMark( LineNumber line );
    void clearMarks();
    const SearchResultArray& marks() const;
    // The first Mark strictly after line.
    OptionalLineNumber markAfter( LineNumber line ) const;
    // The last Mark strictly before line.
    OptionalLineNumber markBefore( LineNumber line ) const;

    // Whether a Log Line is a Match, a Mark or both, or else a Context Line,
    // whether or not it is displayed.
    LineType lineType( LineNumber line ) const;

    // The displayed Log Lines, in order.
    const SearchResultArray& lines() const;
    LinesCount count() const;
    // The Log Line displayed at position, if there is one.
    OptionalLineNumber logLineAt( LineNumber position ) const;
    // The position of a displayed Log Line. For a Log Line not displayed,
    // the position of the last displayed one before it (0 if there is none).
    LineNumber positionOf( LineNumber line ) const;

private:
    // Rebuilds contextLines_ around the Matches and the Marks.
    void rebuildContextLines();
    // Rebuilds combinedLines_ (or drops it) and picks the displayed set.
    // Called on every change to what is displayed.
    void refreshLines();

    // Which bitmap lines() returns: one of the inputs as it is, or the union
    // of several built into combinedLines_.
    enum class Source { Matches, Marks, Combined };

    const SearchResultArray& matches_;
    std::function<LinesCount()> nbLogLines_;
    int contextLinesCount_;

    LineType shown_ = LineType{ LineTypeFlags::Match } | LineTypeFlags::Mark;

    SearchResultArray marks_;
    // Log Lines displayed only because they neighbour a Match or a Mark;
    // never a Match or a Mark itself.
    SearchResultArray contextLines_;
    // The displayed lines when they combine more than one set; empty
    // otherwise, so a single set is never copied.
    SearchResultArray combinedLines_;
    Source source_ = Source::Matches;
};

#endif
