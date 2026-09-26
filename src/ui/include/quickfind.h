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

/*
 * Copyright (C) 2017 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QUICKFIND_H
#define QUICKFIND_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QPoint>
#include <QTime>

#include "atomicflag.h"
#include "displayedlines.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"
#include "qfnotifications.h"
#include "quickfindpattern.h"
#include "selection.h"

class QuickFindPattern;
class AbstractLogData;

// Handle "long processing" notifications to the UI.
// reset() shall be called at the beginning of the search
// and then ping() should be called periodically during the processing.
// The notify() signal should be forwarded to the UI.
class SearchingNotifier : public QObject {
    Q_OBJECT

public:
    SearchingNotifier()
        : dotToDisplay_{ 0 }
    {
    }

    // Reset internal timers at the beiginning of the processing
    void reset();
    // Shall be called frequently during processing, send the notification
    // and call the event loop when appropriate.
    // Pass the current line number and total number of line so that
    // a progress percentage is calculated and displayed.
    // (line shall be negative if ging in reverse)
    inline void ping( LineNumber line, LinesCount nb_lines, bool backward )
    {
        if ( startTime_.msecsTo( QTime::currentTime() ) > 1000 )
            sendNotification( line, nb_lines, backward );
    }

Q_SIGNALS:
    // Sent when the UI shall display a message to the user.
    void notify( const QFNotification& message );

private:
    void sendNotification( LineNumber current_line, LinesCount nb_lines, bool backward );

    QTime startTime_;
    int dotToDisplay_;
};

// The Log Lines one QuickFind searches, in order, and the Log File their text
// is read from.
//
// A text view hands one over each time a QuickFind starts, taken on the UI
// thread. It is a value: nothing the UI thread changes afterwards reaches it,
// so the worker thread can search it while Marks change, a Search adds
// Matches or Context Lines are rebuilt. See
// docs/adr/0002-quickfind-searches-a-copy-of-the-displayed-lines.md.
class QuickFindLines {
public:
    // Walks the Log Lines to search a block at a time, from a position.
    // Valid while the QuickFindLines it came from lives.
    class Cursor {
    public:
        // The Log Line the cursor stands on and those after it, at most count
        // of them in ascending order; the cursor is left on the one after them.
        logsquirl::vector<LineNumber> takeForward( LinesCount count );
        // The Log Line the cursor stands on and those before it, at most count
        // of them in ascending order; the cursor is left on the one before
        // them.
        logsquirl::vector<LineNumber> takeBackward( LinesCount count );
        // Steps to the previous Log Line. From past the last one it steps onto
        // the last one; before the first one it stands on none.
        void previous();

    private:
        friend class QuickFindLines;
        Cursor( LinesCount count, LineNumber position );
        Cursor( const SearchResultArray& lines, LineNumber position );

        // Set when some Log Lines are searched.
        std::optional<DisplayedLinesCursor> lines_;
        // When every Log Line is searched: -1 before the first one, count_
        // past the last one.
        std::int64_t count_ = 0;
        std::int64_t position_ = 0;
    };

    // Keeps a reader attached to the Log File while it lives, so the file
    // stays open between the reads of one QuickFind even when it is kept
    // closed otherwise.
    class AttachedReader {
    public:
        explicit AttachedReader( const AbstractLogData& logFile );
        ~AttachedReader();

        AttachedReader( const AttachedReader& ) = delete;
        AttachedReader& operator=( const AttachedReader& ) = delete;

    private:
        const AbstractLogData& logFile_;
    };

    // Every Log Line of logFile, as many as it has now.
    static QuickFindLines everyLogLine( const AbstractLogData& logFile );
    // The Log Lines in lines, whose text is read from logFile. Reading
    // logFile must be safe off the UI thread.
    static QuickFindLines someLogLines( const AbstractLogData& logFile, SearchResultArray lines );

    // How many Log Lines there are to search.
    LinesCount count() const;
    // How many of them come before logLine: the position of logLine if it is
    // one of them, otherwise of the first one after it.
    LineNumber positionOf( LineNumber logLine ) const;
    // The Log Line at a position, below count().
    LineNumber logLineAt( LineNumber position ) const;
    // A cursor on the Log Line at position, or on none when position is at or
    // past count().
    Cursor cursorAt( LineNumber position ) const;
    // The text of Log Lines, with tabs expanded, in one sparse read.
    logsquirl::vector<QString> expandedLinesText( std::span<const LineNumber> logLines ) const;
    // Attaches a reader to the Log File until the result goes away.
    AttachedReader attachReader() const;

private:
    QuickFindLines( const AbstractLogData& logFile, LinesCount count,
                    std::shared_ptr<const SearchResultArray> lines );

    const AbstractLogData* logFile_;
    LinesCount count_;
    // Null when every Log Line is searched.
    std::shared_ptr<const SearchResultArray> lines_;
};

// Represents a search made with Quick Find (without its results).
//
// QuickFind works in Log Line numbers: its positions, the selections it is
// given and returns, and the Portion of searchDone(). A view whose lines are
// numbered differently (the Filtered View) converts on the way in and out.
//
// Each search runs on a worker thread over the QuickFindLines the view hands
// over when it starts, including each restart of an incremental search. When
// the lines the view displays change while a search runs, the search keeps
// running on its copy. When its result arrives on a Log Line the view no
// longer displays, QuickFind does not report it: it goes on from that Log Line
// in the same direction, over a fresh copy, so it never reports a line that
// isn't a displayed match.
class QuickFind : public QObject {
    Q_OBJECT

public:
    // Construct a search. Both functions are only called on the UI thread:
    // copyDisplayedLines when a search starts, isDisplayed when its result
    // arrives.
    QuickFind( std::function<QuickFindLines()> copyDisplayedLines,
               std::function<bool( LineNumber )> isDisplayed );

    // Set the starting point that will be used by the next search
    void setSearchStartPoint( QPoint startPoint );

    // Make the object forget the 'no more match' flag.
    void resetLimits();

public Q_SLOTS:
    // Used for incremental searches
    // Return the first occurrence of the passed pattern from the starting
    // point.  These searches don't change the starting point.
    void incrementallySearchForward( Selection selection, QuickFindMatcher matcher );
    void incrementallySearchBackward( Selection selection, QuickFindMatcher matcher );

    // Stop the currently ongoing incremental search, leave the selection
    // where it is if a match has been found, restore the old one
    // if not. Also throw away the start point associated with
    // the search.
    Selection incrementalSearchStop();

    // Throw away the current search and restore the initial
    // position/selection
    Selection incrementalSearchAbort();

    // Idem but ignore the direction and always search in the
    // specified direction
    void searchForward( Selection selection, QuickFindMatcher matcher );
    void searchBackward( Selection selection, QuickFindMatcher matcher );

    void stopSearch();

Q_SIGNALS:
    // Sent when the UI shall display a message to the user.
    void notify( const QFNotification& message );
    // Sent when the UI shall clear the notification.
    void clearNotification();
    // Sent when search is completed, with selection on a Log Line. Sent on
    // the UI thread right after checking that the Log Line is displayed:
    // connect it directly, so nothing changes the displayed lines before the
    // receiver converts it.
    void searchDone( bool hasMatch, Portion selection );

private Q_SLOTS:
    void sendNotification( QFNotification notification );
    void onSearchFutureReady();

private:
    enum QFDirection {
        None,
        Forward,
        Backward,
    };

    class LastMatchPosition {
    public:
        void set( LineNumber line, LineColumn column );
        void set( const FilePosition& position );
        void reset()
        {
            line_ = {};
            column_ = LineColumn{ -1 };
        }
        // Does the passed position come after the recorded one
        bool isLater( OptionalLineNumber line, LineColumn column ) const;
        bool isLater( const FilePosition& position ) const;
        // Does the passed position come before the recorded one
        bool isSooner( OptionalLineNumber line, LineColumn column ) const;
        bool isSooner( const FilePosition& position ) const;

    private:
        OptionalLineNumber line_;
        LineColumn column_{ -1 };
    };

    class IncrementalSearchStatus {
    public:
        /* Constructors */
        IncrementalSearchStatus() = default;

        IncrementalSearchStatus( QFDirection direction, const FilePosition& position,
                                 const Selection& initial_selection )
            : ongoing_( direction )
            , position_( position )
            , initialSelection_( initial_selection )
        {
        }

        bool isOngoing() const
        {
            return ( ongoing_ != None );
        }
        QFDirection direction() const
        {
            return ongoing_;
        }
        FilePosition position() const
        {
            return position_;
        }
        Selection initialSelection() const
        {
            return initialSelection_;
        }

    private:
        QFDirection ongoing_{ None };
        FilePosition position_;
        Selection initialSelection_;
    };

    // What the worker thread hands back.
    struct SearchResult {
        // Valid when a match was found.
        Portion match;
        // Whether the search went all the way to the end (or the beginning)
        // without a match, and wasn't interrupted.
        bool reachedLimit = false;
    };

    // The search running, or the last one run. Only touched on the UI thread.
    struct RunningSearch {
        QFDirection direction = None;
        Selection selection;
        QuickFindMatcher matcher;
        // limitsGeneration_ when the search started.
        uint64_t limitsGeneration = 0;
    };

    std::function<QuickFindLines()> copyDisplayedLines_;
    std::function<bool( LineNumber )> isDisplayed_;

    // Owned objects

    // Position of the last match in the file
    // (to avoid searching multiple times where there is no result)
    // Both only read and written on the UI thread.
    LastMatchPosition lastMatch_;
    LastMatchPosition firstMatch_;
    // Counts resetLimits() calls, so a search that started before one does
    // not record a limit that no longer holds.
    uint64_t limitsGeneration_ = 0;

    SearchingNotifier searchingNotifier_;

    // Incremental search status
    IncrementalSearchStatus incrementalSearchStatus_;

    RunningSearch runningSearch_;

    // Private functions
    // Interrupts the search in flight and waits until its worker is done.
    void interruptSearch();
    // Starts a search on the worker thread over a fresh copy of the displayed
    // lines, from start_position.
    void startSearch( QFDirection direction, const FilePosition& start_position,
                      const Selection& selection, const QuickFindMatcher& matcher );
    SearchResult doSearchForward( const QuickFindLines& lines, const FilePosition& start_position,
                                  const QuickFindMatcher& matcher, bool afterLastMatch );
    SearchResult doSearchBackward( const QuickFindLines& lines, const FilePosition& start_position,
                                   const QuickFindMatcher& matcher, bool beforeFirstMatch );

    AtomicFlag interruptRequested_;
    QFuture<SearchResult> operationFuture_;
    QFutureWatcher<SearchResult> operationWatcher_;
};

#endif
