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

#include "abstractlogdata.h"
#include "containers.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"

#include <cstdint>
#include <functional>
#include <optional>

// Walks displayed Log Lines forwards and backwards from a position. It finds
// the Log Line at that position with one select() and then steps from Log
// Line to Log Line, so walking n of them costs n steps rather than n selects,
// each of which is linear in the number of containers of the bitmap.
//
// It reads the lines in place, as the Displayed Lines' lines() or a copy of
// them (ADR 0002): they must neither change nor go away while it walks them.
class DisplayedLinesCursor {
public:
    // Stands on the Log Line at position, or on none when position is past
    // the last one.
    DisplayedLinesCursor( const SearchResultArray& lines, LineNumber position );

    // Whether the cursor stands on a Log Line: not once it stepped past the
    // last one or before the first one.
    bool hasLine() const;
    // The position the cursor stands on; only while it hasLine().
    LineNumber position() const;
    // The Log Line the cursor stands on; only while it hasLine().
    LineNumber logLine() const;

    // Steps to the next Log Line; past the last one it stands on none. From
    // before the first one it steps onto the first one.
    void next();
    // Steps to the previous Log Line; before the first one it stands on none.
    // From past the last one it steps onto the last one.
    void previous();

    // The Log Line the cursor stands on and those after it, at most count of
    // them in ascending order; the cursor is left on the one after them.
    logsquirl::vector<LineNumber> takeForward( LinesCount count );
    // The Log Line the cursor stands on and those before it, at most count of
    // them in ascending order; the cursor is left on the one before them.
    logsquirl::vector<LineNumber> takeBackward( LinesCount count );

private:
    // Puts iterator_ on the Log Line at position_, which must be one.
    void selectPosition();

    const SearchResultArray* lines_;
    std::int64_t count_;
    // -1 before the first Log Line, count_ past the last one.
    std::int64_t position_;
    SearchResultArray::const_iterator iterator_;
};

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

    // The Matches were replaced, or changed in a way not told, while a Search
    // runs or when it stopped. The Context Lines stay as they are until the
    // Search completes.
    void matchesArrived();
    // The Matches grew by newMatches, none of which was a Match before, while
    // a Search runs or when it stopped. Costs as much as newMatches, not as
    // all the Matches; the Context Lines stay as they are until the Search
    // completes.
    void matchesArrived( const SearchResultArray& newMatches );
    // The Search completed (from a real run or from the cache): the Context
    // Lines are rebuilt around its Matches.
    void searchCompleted();
    // The Search completed after the Matches grew by newMatches since they
    // were last told: the Context Lines are brought up to date around every
    // Log Line that became a Match since they were last built.
    void searchCompleted( const SearchResultArray& newMatches );
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
    // A cursor on the Log Line displayed at position, to walk the displayed
    // lines from there without looking up each position. Valid while nothing
    // displayed changes.
    DisplayedLinesCursor cursorAt( LineNumber position ) const;

    // How many of the Log Lines displayed in [first, end) are Matches, and how
    // many are not (Marks and Context Lines). Costs as much as a few ranks of
    // the bitmaps, not as the Log Lines counted -- unless the Matches are
    // hidden, when the Marks displayed there are looked at one by one.
    struct Count {
        uint64_t matches = 0;
        uint64_t others = 0;
    };
    Count countIn( LineNumber first, LineNumber end ) const;

    // Changes whenever the displayed Log Lines, or what they are, change other
    // than by Matches added after every Log Line that was displayed, Marked or
    // a Context Line: while it stays the same, whoever counted the displayed
    // lines up to the last one needs to count only past it.
    uint64_t rewrites() const;

private:
    // Rebuilds contextLines_ around the Matches and the Marks.
    void rebuildContextLines();
    // Brings contextLines_ up to date around every Match and Mark, rebuilding
    // them only when they were not kept up to date. Returns the Log Lines
    // whose type may have changed, or nothing when everything may have.
    std::optional<SearchResultArray> updateContextLines();
    // The Context Lines around lines, within the Log File's nbLogLines: the
    // neighbours of each, merged into ranges, that are neither a Match nor a
    // Mark.
    SearchResultArray contextLinesAround( const SearchResultArray& lines,
                                          uint64_t nbLogLines ) const;
    // Adds or removes one Mark's Context Lines and updates what is displayed.
    void markToggled( uint64_t line, bool added );

    // Tells rewrites() that the displayed Log Lines, or what they are,
    // changed other than by Matches added after everything displayed.
    void rewritten();

    // Rebuilds combinedLines_ (or drops it) and picks the displayed set.
    void refreshLines();
    // Updates combinedLines_ for a change limited to the Log Lines in
    // changed, or refreshes them all when the displayed set changes.
    void refreshLinesAt( const SearchResultArray& changed );

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
    // Which set lines() should return for what is shown now.
    Source pickSource() const;

    // Counts the changes rewrites() tells. Invariant: every change to the
    // displayed Log Lines, or to what they are, calls rewritten() --
    // except Matches added after every Log Line displayed, which
    // whoever counted up to the last one can count on from there. Changed only
    // through rewritten().
    uint64_t rewrites_ = 0;
    // Whether newMatches, just added to the Matches, all come after every Log
    // Line that was a Match, a Mark or a Context Line before; formerEnd is set
    // past the last of those (0 when there was none).
    bool comeAfterEverything( const SearchResultArray& newMatches, uint64_t& formerEnd ) const;

    // Whether contextLines_ surround every Match and Mark, except the Matches
    // in matchesWithoutContextLines_, within the first contextLinesEnd_ Log
    // Lines. Until then, the Context Lines are rebuilt rather than updated.
    bool contextLinesUpToDate_ = false;
    // Matches that arrived since the Context Lines were last brought up to
    // date.
    SearchResultArray matchesWithoutContextLines_;
    // How many Log Lines the Log File had when the Context Lines were last
    // brought up to date: the Context Lines of the Matches and Marks near its
    // end stop there.
    uint64_t contextLinesEnd_ = 0;
};
