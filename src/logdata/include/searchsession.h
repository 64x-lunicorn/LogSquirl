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

#ifndef LOGSQUIRL_SEARCH_SESSION_H
#define LOGSQUIRL_SEARCH_SESSION_H

#include <QObject>
#include <QString>

#include <KDSignalThrottler.h>

#include "linetypes.h"
#include "logfiltereddataworker.h"
#include "regularexpressionpattern.h"
#include "synchronization.h"

class LogData;

// The Search Session: the owner of everything whose correctness depends on
// the ordering of a Search -- the current pattern, the run in flight, its
// results and its progress. One request() supersedes whatever is in
// flight rather than waiting for it; callers no longer have to interrupt
// before starting, or sequence a clear before a run.
//
// Marks are deliberately not here: a Log Line can be marked with no
// Search having run, so Marks stay owned by LogFilteredData.
class SearchSession : public QObject {
    Q_OBJECT

  public:
    enum class Phase {
        Idle,          // no pattern requested (or request() with no pattern)
        Running,       // a run is in flight
        Interrupted,   // stop() cut a run short; results kept are partial
        Complete,      // the requested range has been fully searched
        InvalidPattern // the pattern failed to compile; nothing was run
    };
    Q_ENUM( Phase )

    struct State {
        RegularExpressionPattern pattern;
        LineNumber startLine{ 0 };
        LineNumber endLine{ 0 };
        LinesCount matchCount{ 0 };
        int progress = 0;
        Phase phase = Phase::Idle;
        bool fromCache = false;
        // True when this run continues a previous one (same pattern and
        // startLine, a grown endLine) rather than starting fresh -- e.g.
        // autorefresh extending the range as the file grows.
        bool isContinuation = false;
        QString errorString;
    };

    explicit SearchSession( const LogData& sourceLogData );
    ~SearchSession() override;

    SearchSession( const SearchSession& ) = delete;
    SearchSession& operator=( const SearchSession& ) = delete;

    // Request results for pattern over [startLine, endLine). Supersedes
    // whatever run is currently in flight. When the pattern and startLine
    // match the currently held run and endLine only grows, continues that
    // run from where it left off rather than starting over.
    void request( const RegularExpressionPattern& pattern, LineNumber startLine,
                 LineNumber endLine );
    // Shortcut: the whole file.
    void request( const RegularExpressionPattern& pattern );
    // Go idle: no pattern, no results, no Context Lines.
    void request();

    // Stop the in-flight run, if any, keeping whatever has been found so
    // far. A no-op (no phase change) if nothing is running.
    void stop();

    // Adopt a cache hit: results already known for `pattern` over
    // [startLine, endLine], without starting a run. Used by
    // LogFilteredData, which still owns the search results cache.
    void completeFromCache( const RegularExpressionPattern& pattern, LineNumber startLine,
                            LineNumber endLine, const SearchResultArray& matches,
                            LineLength maxLength );

    State state() const;
    // The cumulative matches found so far (or ever, once Complete).
    SearchResultArray matches() const;
    LineLength maxLength() const;
    LinesCount processedLines() const;
    // Identifies the run the current state belongs to (0 when Idle or
    // InvalidPattern). Stable across every notification of the same run
    // -- including a continuation's, which keeps its predecessor's
    // results -- so callers can tell "another tick of the run I already
    // have a partial copy of" from "a different run" without re-deriving
    // the pattern/range comparison Session already made.
    SearchId currentSearchId() const;
    // Matches accumulated since the last call to takeNewMatches() (or
    // since the current run started, whichever is more recent). Cheap to
    // apply incrementally on every progress tick, unlike matches().
    SearchResultArray takeNewMatches();

  Q_SIGNALS:
    void stateChanged( SearchSession::State state );

  private Q_SLOTS:
    void handleSearchProgressed( LinesCount nbMatches, int progress, LineNumber initialLine,
                                 SearchId searchId );
    void handleSearchFinished( SearchId searchId, LinesCount nbMatches, LineNumber initialLine,
                               bool interrupted );
    void emitThrottledStateChanged();

  Q_SIGNALS:
    // Internal: feeds the throttler. Not for external use.
    void resultsReady();

  private:
    void startRun( const RegularExpressionPattern& pattern, LineNumber startLine,
                  LineNumber endLine, bool isContinuation );
    // Absorbs a worker result batch into matches_/pendingDelta_/maxLength_/
    // nbLinesProcessed_. Shared by handleSearchProgressed and
    // handleSearchFinished, which otherwise duplicate this exactly.
    void applyIncomingResults( const SearchResults& results );
    // Replaces state_ under stateMutex_ in one step, so each call site
    // builds one complete State value instead of hand-editing a handful
    // of fields (and risking missing one) under the lock.
    void applyState( State newState );

    const LogData& sourceLogData_;
    LogFilteredDataWorker workerThread_;

    SearchResultArray matches_;
    // Matches accumulated since the last takeNewMatches() call.
    SearchResultArray pendingDelta_;
    LineLength maxLength_{ 0 };
    LinesCount nbLinesProcessed_{ 0 };

    // The run whose progress and results we're currently waiting on. A
    // signal carrying any other id belongs to a run we've since
    // superseded, and is discarded rather than applied.
    SearchId currentSearchId_{ 0 };

    mutable Mutex stateMutex_;
    State state_;

    KDToolBox::KDSignalThrottler progressThrottler_;
};

Q_DECLARE_METATYPE( SearchSession::State )

#endif
