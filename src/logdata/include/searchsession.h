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
#include "settingspolicies.h"
#include "synchronization.h"

class SearchBlockSource;

// The Search Session: the owner of everything whose correctness depends on
// the ordering of a Search -- the current pattern, the run in flight, its
// Matches, its progress and its cached results. One request() supersedes
// whatever is in flight rather than waiting for it; callers no longer have
// to interrupt before starting, or sequence a clear before a run. A cache
// hit reaches the same completion path as a real run.
//
// The Matches are all it keeps of what the Filtered View shows: the Marks
// and the Context Lines belong to the Displayed Lines, which read the
// Matches in place (matches()) whenever this reports a state change.
class SearchSession : public QObject {
    Q_OBJECT

public:
    enum class Phase {
        Idle,           // no pattern requested (or request() with no pattern)
        Running,        // a run is in flight
        Interrupted,    // stop() cut a run short; results kept are partial
        Complete,       // the requested range has been fully searched
        InvalidPattern, // the pattern failed to compile; nothing was run
        Failed          // the run failed; errorString describes why, no results are kept
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

    // The Search Policy is everything this object knows about the
    // settings: which regex engine to compile on, whether and how far to
    // cache results, and how far Context Lines reach. It reads none
    // itself.
    SearchSession( const SearchBlockSource& blockSource, const SearchPolicy& searchPolicy );
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
    // Go idle: no pattern, no results.
    void request();

    // Stop the in-flight run, if any, keeping whatever has been found so
    // far. A no-op (no phase change) if nothing is running.
    void stop();

    // Replaces the Search Policy. Runs started from now on use it; a run
    // already in flight keeps the one it started with.
    void setSearchPolicy( const SearchPolicy& searchPolicy );

    // Drops every cached search result (e.g. the file was truncated, so
    // previously-cached ranges no longer mean what they used to).
    void dropCache();

    State state() const;
    // The cumulative Matches found so far (or ever, once Complete). They
    // change only on the thread this object lives on, and only together with
    // a stateChanged() -- so a reader there can hold on to the reference
    // instead of copying, and catch up whenever it is told of a change.
    const SearchResultArray& matches() const;
    // While stateChanged() is emitted: the Matches that joined matches() with
    // it, none of which was a Match before -- so whoever follows the Matches
    // can update by them alone. nullptr when the Matches were replaced since
    // the previous stateChanged() (a new run, a cache hit, a reset): then only
    // all of them tell what changed.
    const SearchResultArray* newMatches() const;
    LineLength maxLength() const;
    LinesCount processedLines() const;

Q_SIGNALS:
    void stateChanged( SearchSession::State state );

private Q_SLOTS:
    void handleSearchProgressed( int progress, LineNumber initialLine, SearchId searchId );
    void handleSearchFinished( SearchId searchId, LineNumber initialLine, bool interrupted,
                               const QString& failure );
    // Emits the state change the throttler held back, unless one has been
    // reported directly since.
    void emitThrottledStateChanged();

Q_SIGNALS:
    // Internal: feeds the throttler. Not for external use.
    void resultsReady();

private:
    void startRun( const RegularExpressionPattern& pattern, LineNumber startLine,
                   LineNumber endLine, bool isContinuation,
                   std::shared_ptr<const RegularExpression> compiledExpression );
    // Absorbs a worker result batch into arrivedMatches_/maxLength_/
    // nbLinesProcessed_, and counts the Matches anew. Shared by
    // handleSearchProgressed and handleSearchFinished, which otherwise
    // duplicate this exactly.
    void applyIncomingResults( SearchResults results );
    // Replaces state_ under stateMutex_ in one step, so each call site
    // builds one complete State value instead of hand-editing a handful
    // of fields (and risking missing one) under the lock.
    void applyState( State newState );
    // Moves arrivedMatches_ into matches_, and into newMatches_ unless the
    // Matches were replaced since the last state change was reported.
    void publishArrivedMatches();
    // Publishes the Matches that arrived and reports the current state: the
    // one way stateChanged() is emitted. Progress goes through the throttler
    // first; a run starting, stopping, completing or failing is reported at
    // once.
    void notifyStateChanged();
    // Adopts a cache hit for pattern over [startLine, endLine]: sets
    // matches_/maxLength_ from it and reaches the same completion path a
    // real run does, rather than the shortcut a cache hit used to take.
    void adoptCacheHit( const RegularExpressionPattern& pattern, LineNumber startLine,
                        LineNumber endLine, const SearchResultArray& matches,
                        LineLength maxLength );
    // Interrupts the worker and invalidates the run we were waiting on,
    // so a late result for it is discarded rather than applied on top of
    // whatever this call is about to transition to. Shared by every
    // transition that isn't itself starting a new worker run.
    void invalidateCurrentRun();
    // Clears matches_/maxLength_/nbLinesProcessed_: the
    // reset shared by going idle, an invalid pattern, and starting a
    // fresh (non-continuation) run.
    void resetResults();

    const SearchBlockSource& blockSource_;
    SearchPolicy searchPolicy_;
    LogFilteredDataWorker workerThread_;

    // The compiled form of the run currently held (Running/Complete/
    // Interrupted, never a cache hit). A continuation reuses this instead
    // of recompiling an already-validated pattern -- worth caching since
    // autorefresh can call request() far more often, per file, than a
    // fresh pattern is ever typed.
    std::shared_ptr<const RegularExpression> compiledExpression_;

    SearchResultArray matches_;
    // Matches the worker reported since the last state change was; they
    // join matches_ when the next one is (notifyStateChanged()), so matches_
    // never changes behind a reader's back. None of them is in matches_
    // already, so the match count is the size of both together: it is
    // counted from the Matches, never incremented beside them, and a Log Line
    // searched again is not counted twice.
    SearchResultArray arrivedMatches_;
    // The Matches that joined matches_ since the last state change was
    // reported, kept only while it is (newMatches()).
    SearchResultArray newMatches_;
    // matches_ were replaced rather than grown since the last state change
    // was reported.
    bool matchesReplaced_ = true;
    // A progress tick is waiting in the throttler to be reported.
    bool stateChangePending_ = false;
    LineLength maxLength_{ 0 };
    LinesCount nbLinesProcessed_{ 0 };

    // The run whose progress and results we're currently waiting on. A
    // signal carrying any other id belongs to a run we've since
    // superseded, and is discarded rather than applied.
    SearchId currentSearchId_{ 0 };

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
