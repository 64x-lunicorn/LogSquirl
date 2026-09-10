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

#include <tuple>
#include <unordered_map>

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
// results, its progress, its cached results and the Context Lines around
// its matches. One request() supersedes whatever is in flight rather than
// waiting for it; callers no longer have to interrupt before starting, or
// sequence a clear before a run. A cache hit reaches the same completion
// path as a real run, so it rebuilds Context Lines exactly like one.
//
// Marks are deliberately not here: a Log Line can be marked with no
// Search having run, so Marks stay owned by LogFilteredData -- which
// pushes its current marks in via setMarks() whenever they change, since
// Context Lines surround a Match or a Mark alike.
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

    // Drops every cached search result (e.g. the file was truncated, so
    // previously-cached ranges no longer mean what they used to).
    void dropCache();

    // Replaces the marks Context Lines are built around. Call whenever
    // Marks change; does not itself trigger a rebuild (Marks changing
    // does, but via LogFilteredData calling rebuildContextLines() below,
    // same as before -- this just keeps the input current for whenever a
    // rebuild -- including one a completing run triggers on its own --
    // next happens).
    void setMarks( const SearchResultArray& marks );
    // Recomputes Context Lines from the current matches and marks. Safe
    // to call any time (e.g. after a Configuration change); a cache hit
    // and a real completion both already trigger this themselves.
    void rebuildContextLines();
    // Context Lines: Log Lines shown only because they neighbour a Match
    // or Mark, not because they matched themselves.
    const SearchResultArray& contextLines() const;

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
                  LineNumber endLine, bool isContinuation,
                  std::shared_ptr<const RegularExpression> compiledExpression );
    // Absorbs a worker result batch into matches_/pendingDelta_/maxLength_/
    // nbLinesProcessed_. Shared by handleSearchProgressed and
    // handleSearchFinished, which otherwise duplicate this exactly.
    void applyIncomingResults( const SearchResults& results );
    // Replaces state_ under stateMutex_ in one step, so each call site
    // builds one complete State value instead of hand-editing a handful
    // of fields (and risking missing one) under the lock.
    void applyState( State newState );
    // Adopts a cache hit for pattern over [startLine, endLine]: sets
    // matches_/maxLength_ from it and reaches the same completion path a
    // real run does (cache + Context Lines), rather than the shortcut a
    // cache hit used to take that skipped both.
    void adoptCacheHit( const RegularExpressionPattern& pattern, LineNumber startLine,
                        LineNumber endLine, const SearchResultArray& matches,
                        LineLength maxLength );
    // Interrupts the worker and invalidates the run we were waiting on,
    // so a late result for it is discarded rather than applied on top of
    // whatever this call is about to transition to. Shared by every
    // transition that isn't itself starting a new worker run.
    void invalidateCurrentRun();
    // Clears matches_/pendingDelta_/maxLength_/nbLinesProcessed_: the
    // reset shared by going idle, an invalid pattern, and starting a
    // fresh (non-continuation) run.
    void resetResults();

    const LogData& sourceLogData_;
    LogFilteredDataWorker workerThread_;

    // The compiled form of the run currently held (Running/Complete/
    // Interrupted, never a cache hit). A continuation reuses this instead
    // of recompiling an already-validated pattern -- worth caching since
    // autorefresh can call request() far more often, per file, than a
    // fresh pattern is ever typed.
    std::shared_ptr<const RegularExpression> compiledExpression_;

    SearchResultArray matches_;
    // Matches accumulated since the last takeNewMatches() call.
    SearchResultArray pendingDelta_;
    LineLength maxLength_{ 0 };
    LinesCount nbLinesProcessed_{ 0 };

    // The run whose progress and results we're currently waiting on. A
    // signal carrying any other id belongs to a run we've since
    // superseded, and is discarded rather than applied.
    SearchId currentSearchId_{ 0 };

    // Context Lines (breadcrumbs) around matches/marks, excluding
    // match/mark lines themselves.
    SearchResultArray contextLines_;
    // The current Marks, mirrored from LogFilteredData via setMarks() so
    // rebuildContextLines() can expand around a Match or a Mark alike.
    SearchResultArray currentMarks_;

    struct CachedSearchResult {
        SearchResultArray matching_lines;
        LineLength maxLength;
    };

    using SearchCacheKey = std::tuple<RegularExpressionPattern, LineNumber::UnderlyingType,
                                      LineNumber::UnderlyingType>;
    struct SearchCacheKeyHash {
        template <class T>
        void hash_combine( std::size_t& seed, const T& v ) const
        {
            seed ^= std::hash<T>()( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
        }
        std::size_t operator()( const SearchCacheKey& k ) const
        {
            size_t seed = qHash( std::get<0>( k ).pattern );

            hash_combine( seed, std::get<0>( k ).isPlainText );
            hash_combine( seed, std::get<0>( k ).isBoolean );
            hash_combine( seed, std::get<0>( k ).isCaseSensitive );
            hash_combine( seed, std::get<0>( k ).isExclude );
            hash_combine( seed, std::get<0>( k ).isPrefilter );
            hash_combine( seed, std::get<1>( k ) );
            hash_combine( seed, std::get<2>( k ) );
            return seed;
        }
    };

    static SearchCacheKey makeCacheKey( const RegularExpressionPattern& regExp,
                                        LineNumber startLine, LineNumber endLine )
    {
        return std::make_tuple( regExp, startLine.get(), endLine.get() );
    }
    static LineNumber getExpectedSearchEnd( const SearchCacheKey& cacheKey )
    {
        return LineNumber( std::get<2>( cacheKey ) );
    }
    // Stores matches_/maxLength_ under currentSearchKey_, evicting the
    // least-recently-inserted other entries first if that grows the
    // cache past its configured line budget. A no-op if the key is
    // empty (a continuation's completion, which resets it) or caching is
    // disabled.
    void updateSearchResultsCache();

    std::unordered_map<SearchCacheKey, CachedSearchResult, SearchCacheKeyHash> searchResultsCache_;
    SearchCacheKey currentSearchKey_;

    mutable Mutex stateMutex_;
    State state_;

    KDToolBox::KDSignalThrottler progressThrottler_;
};

Q_DECLARE_METATYPE( SearchSession::State )

#endif
