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

#include "searchsession.h"

#include <numeric>
#include <utility>

#include "configuration.h"
#include "log.h"
#include "logdata.h"
#include "regularexpression.h"

SearchSession::SearchSession( const LogData& sourceLogData )
    : sourceLogData_( sourceLogData )
    , workerThread_( sourceLogData )
{
    connect( &workerThread_, &LogFilteredDataWorker::searchProgressed, this,
             &SearchSession::handleSearchProgressed );
    connect( &workerThread_, &LogFilteredDataWorker::searchFinished, this,
             &SearchSession::handleSearchFinished );

    progressThrottler_.setTimeout( 100 );
    connect( this, &SearchSession::resultsReady, &progressThrottler_,
             &KDToolBox::KDGenericSignalThrottler::throttle );
    connect( &progressThrottler_, &KDToolBox::KDGenericSignalThrottler::triggered, this,
             &SearchSession::emitThrottledStateChanged );
}

// Disconnects and invalidates the in-flight run's identity before any
// member (in particular workerThread_ and progressThrottler_) is torn
// down. A result still in flight after this point is stale by id
// regardless of the disconnects below, which additionally guard against
// the throttler's final emission during its own destruction.
SearchSession::~SearchSession()
{
    currentSearchId_ = SearchId( 0 );
    disconnect();
    progressThrottler_.disconnect();
    workerThread_.disconnect();
}

void SearchSession::request( const RegularExpressionPattern& pattern, LineNumber startLine,
                             LineNumber endLine )
{
    const auto previous = state();
    // A cache hit never touches the worker, so the persistent SearchData
    // it accumulates into across calls is never reseeded to match
    // whatever pattern the cache hit adopted -- resuming on top of it
    // (via updateSearch()) could resume on another pattern's leftovers.
    // Only a real run's own results are safe to continue from.
    const bool isContinuation
        = ( previous.phase == Phase::Running || previous.phase == Phase::Complete
            || previous.phase == Phase::Interrupted )
          && !previous.fromCache && pattern == previous.pattern
          && startLine == previous.startLine && endLine > previous.endLine;

    if ( isContinuation ) {
        // Same pattern as the run being continued, already validated and
        // compiled for it (compiledExpression_): autorefresh calls this
        // far more often, per file, than a genuinely new pattern is ever
        // typed, so re-validating -- a Hyperscan compile -- on every tick
        // would be a self-inflicted, avoidable cost on the calling thread.
        startRun( pattern, startLine, endLine, true, compiledExpression_ );
        return;
    }

    RegularExpression expression{ pattern };
    if ( !expression.isValid() ) {
        invalidateCurrentRun();
        resetResults();
        contextLines_ = SearchResultArray();

        State newState;
        newState.pattern = pattern;
        newState.startLine = startLine;
        newState.endLine = endLine;
        newState.phase = Phase::InvalidPattern;
        newState.errorString = expression.errorString();
        applyState( std::move( newState ) );
        Q_EMIT stateChanged( state() );
        return;
    }

    if ( Configuration::get().useSearchResultsCache() ) {
        const auto key = makeCacheKey( pattern, startLine, endLine );
        const auto cached = searchResultsCache_.find( key );
        if ( cached != std::end( searchResultsCache_ ) ) {
            adoptCacheHit( pattern, startLine, endLine, cached->second.matching_lines,
                          cached->second.maxLength );
            return;
        }
    }

    // Handed to the worker rather than recompiled there: expression above
    // already paid the (Hyperscan) compile cost to answer isValid(). Kept
    // (not just moved into the worker call) so a later continuation of
    // this same run can reuse it too.
    compiledExpression_ = std::make_shared<const RegularExpression>( std::move( expression ) );
    startRun( pattern, startLine, endLine, false, compiledExpression_ );
}

void SearchSession::request( const RegularExpressionPattern& pattern )
{
    request( pattern, 0_lnum, LineNumber( sourceLogData_.getNbLine().get() ) );
}

void SearchSession::request()
{
    invalidateCurrentRun();
    resetResults();
    contextLines_ = SearchResultArray();
    currentSearchKey_ = SearchCacheKey{};
    compiledExpression_.reset();

    applyState( State{} );
    Q_EMIT stateChanged( state() );
}

void SearchSession::stop()
{
    invalidateCurrentRun();

    bool wasRunning = false;
    {
        ScopedLock lock( stateMutex_ );
        wasRunning = ( state_.phase == Phase::Running );
        if ( wasRunning ) {
            state_.phase = Phase::Interrupted;
        }
    }

    if ( wasRunning ) {
        Q_EMIT stateChanged( state() );
    }
}

void SearchSession::adoptCacheHit( const RegularExpressionPattern& pattern, LineNumber startLine,
                                   LineNumber endLine, const SearchResultArray& matches,
                                   LineLength maxLength )
{
    // A real run may still be in flight for a different pattern (the user
    // retyping a previously-searched, now-cached pattern before a newer
    // search finished): supersede it exactly like starting a real run
    // would, so its late results don't land on top of this cache hit.
    invalidateCurrentRun();

    matches_ = matches;
    pendingDelta_ = SearchResultArray();
    maxLength_ = maxLength;
    nbLinesProcessed_ = LinesCount( endLine.get() );
    currentSearchKey_ = makeCacheKey( pattern, startLine, endLine );

    // Same completion path a real run takes for Context Lines -- rebuilding
    // them here (rather than skipping it, as a cache hit used to) is
    // exactly what keeps them from belonging to whatever ran previously.
    // Not re-inserting into the cache: entry is already there (that's
    // what a hit means), so writing it back would just redo
    // updateSearchResultsCache()'s full-cache accounting for nothing.
    rebuildContextLines();

    State newState;
    newState.pattern = pattern;
    newState.startLine = startLine;
    newState.endLine = endLine;
    newState.matchCount = LinesCount( matches.cardinality() );
    newState.progress = 100;
    newState.phase = Phase::Complete;
    newState.fromCache = true;
    applyState( std::move( newState ) );
    Q_EMIT stateChanged( state() );
}

void SearchSession::startRun( const RegularExpressionPattern& pattern, LineNumber startLine,
                              LineNumber endLine, bool isContinuation,
                              std::shared_ptr<const RegularExpression> compiledExpression )
{
    if ( !isContinuation ) {
        resetResults();
    }
    else {
        // Whether continuing or starting over, nothing is pending yet for
        // this (about to be assigned) run's id.
        pendingDelta_ = SearchResultArray();
    }

    // A continuation's eventual completion must not be cached under a key
    // that promises the whole (originally requested) range was searched
    // from scratch; only a fresh run's key is kept.
    currentSearchKey_
        = isContinuation ? SearchCacheKey{} : makeCacheKey( pattern, startLine, endLine );

    State newState;
    newState.pattern = pattern;
    newState.startLine = startLine;
    newState.endLine = endLine;
    newState.matchCount = LinesCount( matches_.cardinality() );
    newState.phase = Phase::Running;
    newState.isContinuation = isContinuation;
    applyState( std::move( newState ) );

    sourceLogData_.attachReader();

    currentSearchId_
        = isContinuation
              ? workerThread_.updateSearch( compiledExpression, startLine, endLine,
                                            LineNumber( nbLinesProcessed_.get() ) )
              : workerThread_.search( compiledExpression, startLine, endLine );

    Q_EMIT stateChanged( state() );
}

SearchSession::State SearchSession::state() const
{
    ScopedLock lock( stateMutex_ );
    return state_;
}

SearchResultArray SearchSession::matches() const
{
    return matches_;
}

LineLength SearchSession::maxLength() const
{
    return maxLength_;
}

LinesCount SearchSession::processedLines() const
{
    return nbLinesProcessed_;
}

void SearchSession::dropCache()
{
    searchResultsCache_.clear();
}

SearchId SearchSession::currentSearchId() const
{
    return currentSearchId_;
}

const SearchResultArray& SearchSession::contextLines() const
{
    return contextLines_;
}

void SearchSession::setMarks( const SearchResultArray& marks )
{
    currentMarks_ = marks;
}

void SearchSession::rebuildContextLines()
{
    const auto& config = Configuration::get();
    const int contextCount = config.contextLinesCount();

    contextLines_ = SearchResultArray();

    if ( contextCount <= 0 ) {
        return;
    }

    const auto totalLines = sourceLogData_.getNbLine().get();
    if ( totalLines == 0 ) {
        return;
    }

    // Expand each match/mark +-contextCount lines
    const auto base = matches_ | currentMarks_;

    struct ExpandParams {
        SearchResultArray* result;
        int n;
        uint64_t maxLine;
        const SearchResultArray* baseSet;
    };

    ExpandParams params{ &contextLines_, contextCount, totalLines, &base };

    base.iterate(
        []( uint64_t line, void* ctx ) -> bool {
            auto* p = static_cast<ExpandParams*>( ctx );
            const auto start = ( line > static_cast<uint64_t>( p->n ) )
                                   ? ( line - static_cast<uint64_t>( p->n ) )
                                   : 0ULL;
            const auto end = std::min( line + static_cast<uint64_t>( p->n ), p->maxLine - 1 );
            for ( auto i = start; i <= end; ++i ) {
                if ( !p->baseSet->contains( static_cast<uint64_t>( i ) ) ) {
                    p->result->add( static_cast<uint64_t>( i ) );
                }
            }
            return true;
        },
        static_cast<void*>( &params ) );
}

void SearchSession::updateSearchResultsCache()
{
    const auto& config = Configuration::get();
    if ( !config.useSearchResultsCache() ) {
        return;
    }

    if ( currentSearchKey_ == SearchCacheKey{} ) {
        return;
    }

    const uint64_t maxCacheLines = config.searchResultsCacheLines();

    if ( matches_.cardinality() > maxCacheLines ) {
        LOG_DEBUG << "SearchSession: too many matches to place in cache";
        return;
    }

    LOG_INFO << "SearchSession: caching results for key " << std::get<0>( currentSearchKey_ ).pattern
             << "_" << std::get<1>( currentSearchKey_ ) << "_" << std::get<2>( currentSearchKey_ );

    searchResultsCache_[ currentSearchKey_ ] = { matches_, maxLength_ };
    auto cacheSize = std::accumulate( searchResultsCache_.cbegin(), searchResultsCache_.cend(),
                                      uint64_t{ 0 }, []( const auto& acc, const auto& next ) {
                                          return acc + next.second.matching_lines.cardinality();
                                      } );

    LOG_INFO << "SearchSession: cache size " << cacheSize;

    auto cachedResult = std::begin( searchResultsCache_ );
    while ( cachedResult != std::end( searchResultsCache_ ) && cacheSize > maxCacheLines ) {

        if ( cachedResult->first == currentSearchKey_ ) {
            ++cachedResult;
            continue;
        }

        cacheSize -= cachedResult->second.matching_lines.cardinality();
        cachedResult = searchResultsCache_.erase( cachedResult );
    }
}

SearchResultArray SearchSession::takeNewMatches()
{
    SearchResultArray delta;
    std::swap( delta, pendingDelta_ );
    return delta;
}

void SearchSession::applyState( State newState )
{
    ScopedLock lock( stateMutex_ );
    state_ = std::move( newState );
}

void SearchSession::invalidateCurrentRun()
{
    workerThread_.interrupt();
    currentSearchId_ = SearchId( 0 );
}

void SearchSession::resetResults()
{
    matches_ = SearchResultArray();
    pendingDelta_ = SearchResultArray();
    maxLength_ = 0_length;
    nbLinesProcessed_ = 0_lcount;
}

void SearchSession::applyIncomingResults( const SearchResults& results )
{
    matches_ |= results.newMatches;
    pendingDelta_ |= results.newMatches;
    maxLength_ = results.maxLength;
    nbLinesProcessed_ = results.processedLines;
}

void SearchSession::handleSearchProgressed( LinesCount nbMatches, int progress,
                                            LineNumber /*initialLine*/, SearchId searchId )
{
    if ( searchId != currentSearchId_ ) {
        // Progress from a run we've since superseded; its results are stale.
        return;
    }

    applyIncomingResults( workerThread_.getSearchResults() );

    {
        ScopedLock lock( stateMutex_ );
        state_.matchCount = nbMatches;
        state_.progress = progress;
    }

    Q_EMIT resultsReady();
}

void SearchSession::handleSearchFinished( SearchId searchId, LinesCount nbMatches,
                                          LineNumber /*initialLine*/, bool interrupted )
{
    // Every request()/completeFromCache() that reached the worker did
    // exactly one attachReader(); this is its matching detachReader(),
    // and it must happen regardless of whether this run's results end up
    // applied below -- a superseded run must not leak the attach just
    // because its results are discarded.
    sourceLogData_.detachReader();

    if ( searchId != currentSearchId_ ) {
        // A superseded (or explicitly stopped) run finishing late;
        // discard rather than apply.
        return;
    }

    applyIncomingResults( workerThread_.getSearchResults() );

    if ( interrupted ) {
        // Report neither completion nor 100%: an interrupted run is not a
        // finished one. In practice this run's id will already have been
        // superseded (by a new request()) or invalidated (by stop()) by
        // the time this arrives, so the early return above already
        // handles it -- this remains as a defensive fallback.
        LOG_INFO << "Search run interrupted before completion";
        return;
    }

    if ( nbLinesProcessed_.get() == getExpectedSearchEnd( currentSearchKey_ ).get() ) {
        updateSearchResultsCache();
    }
    rebuildContextLines();

    {
        ScopedLock lock( stateMutex_ );
        state_.matchCount = nbMatches;
        state_.progress = 100;
        state_.phase = Phase::Complete;
    }

    Q_EMIT resultsReady();
}

void SearchSession::emitThrottledStateChanged()
{
    Q_EMIT stateChanged( state() );
}
