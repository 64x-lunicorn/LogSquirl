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

#include <utility>

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
    RegularExpression expression{ pattern };
    if ( !expression.isValid() ) {
        workerThread_.interrupt();
        currentSearchId_ = SearchId( 0 );

        // Nothing was run: the class's own contract, so results left over
        // from whatever request() preceded this one must not linger.
        matches_ = SearchResultArray();
        pendingDelta_ = SearchResultArray();
        maxLength_ = 0_length;
        nbLinesProcessed_ = 0_lcount;

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

    const auto previous = state();
    const bool isContinuation
        = ( previous.phase == Phase::Running || previous.phase == Phase::Complete
            || previous.phase == Phase::Interrupted )
          && pattern == previous.pattern && startLine == previous.startLine
          && endLine > previous.endLine;

    startRun( pattern, startLine, endLine, isContinuation );
}

void SearchSession::request( const RegularExpressionPattern& pattern )
{
    request( pattern, 0_lnum, LineNumber( sourceLogData_.getNbLine().get() ) );
}

void SearchSession::request()
{
    workerThread_.interrupt();
    currentSearchId_ = SearchId( 0 );

    matches_ = SearchResultArray();
    pendingDelta_ = SearchResultArray();
    maxLength_ = 0_length;
    nbLinesProcessed_ = 0_lcount;

    applyState( State{} );
    Q_EMIT stateChanged( state() );
}

void SearchSession::stop()
{
    workerThread_.interrupt();
    currentSearchId_ = SearchId( 0 );

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

void SearchSession::completeFromCache( const RegularExpressionPattern& pattern,
                                       LineNumber startLine, LineNumber endLine,
                                       const SearchResultArray& matches, LineLength maxLength )
{
    matches_ = matches;
    pendingDelta_ = SearchResultArray();
    maxLength_ = maxLength;
    nbLinesProcessed_ = LinesCount( endLine.get() );

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
                              LineNumber endLine, bool isContinuation )
{
    if ( !isContinuation ) {
        matches_ = SearchResultArray();
        maxLength_ = 0_length;
        nbLinesProcessed_ = 0_lcount;
    }
    // Whether continuing or starting over, nothing is pending yet for this
    // (about to be assigned) run's id.
    pendingDelta_ = SearchResultArray();

    State newState;
    newState.pattern = pattern;
    newState.startLine = startLine;
    newState.endLine = endLine;
    newState.matchCount = LinesCount( matches_.cardinality() );
    newState.phase = Phase::Running;
    newState.isContinuation = isContinuation;
    applyState( std::move( newState ) );

    sourceLogData_.attachReader();

    currentSearchId_ = isContinuation
                          ? workerThread_.updateSearch( pattern, startLine, endLine,
                                                        LineNumber( nbLinesProcessed_.get() ) )
                          : workerThread_.search( pattern, startLine, endLine );

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

SearchId SearchSession::currentSearchId() const
{
    return currentSearchId_;
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
