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

#include "keptsearches.h"

#include "filteredview.h"
#include "logfiltereddata.h"
#include "openlogfile.h"
#include "viewset.h"

#include <algorithm>
#include <iterator>
#include <utility>

KeptSearches::KeptSearches( std::shared_ptr<OpenLogFile> openLogFile, ViewSet& viewSet,
                            BuildView buildView )
    : openLogFile_( std::move( openLogFile ) )
    , viewSet_( viewSet )
    , buildView_( std::move( buildView ) )
{
    // Only the current Search's progress is reported, by the Open Log File,
    // which follows the current Search. It is told on queued, so that what a
    // Search tells while it is requested reaches the listener once the
    // request is done; what was told before another Search was made current
    // is dropped then.
    connect( openLogFile_.get(), &OpenLogFile::searchUpdated, this,
             [ this ]( const SearchSessionState& state ) {
                 QMetaObject::invokeMethod(
                     this,
                     [ this, state, toldOf = currentChanges_ ] {
                         if ( toldOf == currentChanges_ ) {
                             Q_EMIT currentSearchUpdated( state );
                         }
                     },
                     Qt::QueuedConnection );
             } );
}

KeptSearches::~KeptSearches() = default;

FilteredView* KeptSearches::showCurrentSearch()
{
    return add( openLogFile_->filteredData() );
}

FilteredView* KeptSearches::startAnother()
{
    auto search = openLogFile_->startAnotherSearch();
    // Reports the Search that was current sent until now are stale; showing
    // the current Search changes nothing and keeps its queued reports.
    ++currentChanges_;
    return add( std::move( search ) );
}

FilteredView* KeptSearches::add( std::shared_ptr<LogFilteredData> search )
{
    auto* view = buildView_( search.get() );

    searches_.push_back( Kept{ view, search, {} } );
    current_ = view;

    // A view added is the current Search's.
    viewSet_.addFilteredView( view );
    viewSet_.makeSearchCurrent( view, search.get() );
    return view;
}

void KeptSearches::makeCurrent( FilteredView* view )
{
    if ( view == nullptr || view == current_ ) {
        return;
    }
    const auto found = find( view );
    if ( found == searches_.end() ) {
        return;
    }

    current_ = view;
    openLogFile_->makeSearchCurrent( found->search );
    ++currentChanges_;
    viewSet_.makeSearchCurrent( view, found->search.get() );
}

bool KeptSearches::drop( FilteredView* view )
{
    auto found = find( view );
    if ( found == searches_.end() ) {
        return false;
    }

    if ( view == current_ ) {
        if ( searches_.size() == 1 ) {
            // A Log File always has a current Search.
            return false;
        }
        const auto next
            = std::next( found ) != searches_.end() ? std::next( found ) : std::prev( found );
        makeCurrent( next->view );
    }

    auto search = std::move( found->search );
    searches_.erase( found );

    // The view reads the Search until it is gone.
    connect( view, &QObject::destroyed, [ keptUntilGone = std::move( search ) ]() {} );
    view->deleteLater();
    return true;
}

FilteredView* KeptSearches::currentView() const
{
    return current_;
}

std::size_t KeptSearches::count() const
{
    return searches_.size();
}

SearchSessionState KeptSearches::requestCurrent( const RegularExpressionPattern& pattern )
{
    if ( const auto current = find( current_ ); current != searches_.end() ) {
        current->requested = pattern;
    }
    return openLogFile_->requestSearch( pattern );
}

void KeptSearches::clearCurrent()
{
    if ( const auto current = find( current_ ); current != searches_.end() ) {
        current->requested = {};
    }
    openLogFile_->clearSearch();
}

KeptSearches::Requested KeptSearches::requested() const
{
    Requested requested;
    for ( std::size_t index = 0; index < searches_.size(); ++index ) {
        requested.patterns.push_back( searches_[ index ].requested );
        if ( searches_[ index ].view == current_ ) {
            requested.current = index;
        }
    }
    return requested;
}

std::vector<FilteredView*> KeptSearches::restore( const Requested& saved )
{
    std::vector<FilteredView*> views;
    if ( saved.patterns.empty() || searches_.size() != 1 ) {
        return views;
    }

    // Built as the user builds them, before any of them runs: starting
    // another Search stops the current one.
    views.push_back( searches_.front().view );
    for ( std::size_t index = 1; index < saved.patterns.size(); ++index ) {
        views.push_back( startAnother() );
    }
    const auto current = saved.current < views.size() ? saved.current : 0;
    makeCurrent( views[ current ] );

    for ( std::size_t index = 0; index < views.size(); ++index ) {
        auto& kept = searches_[ index ];
        kept.requested = saved.patterns[ index ];
        if ( kept.requested.pattern.isEmpty() ) {
            continue;
        }
        restoredRuns_.push_back( kept.search );
        restoredRunConnections_.push_back(
            connect( kept.search.get(), &LogFilteredData::searchStateChanged, this,
                     [ this ]() { tellWhenRestoredSearchesFinished(); } ) );
        if ( index != current ) {
            openLogFile_->requestKeptSearch( kept.search, kept.requested );
        }
    }

    // Told once the caller has had them: also when none runs.
    restoredRunsTold_ = false;
    QMetaObject::invokeMethod(
        this, [ this ] { tellWhenRestoredSearchesFinished(); }, Qt::QueuedConnection );
    return views;
}

void KeptSearches::tellWhenRestoredSearchesFinished()
{
    if ( restoredRunsTold_ ) {
        return;
    }
    for ( const auto& run : restoredRuns_ ) {
        const auto search = run.lock();
        if ( !search ) {
            continue;
        }
        // Idle: not requested yet, or waiting for the first load.
        const auto phase = search->searchState().phase;
        if ( phase == SearchSessionPhase::Idle || phase == SearchSessionPhase::Running ) {
            return;
        }
    }

    restoredRunsTold_ = true;
    for ( const auto& connection : std::exchange( restoredRunConnections_, {} ) ) {
        disconnect( connection );
    }
    restoredRuns_.clear();
    Q_EMIT restoredSearchesFinished();
}

std::vector<KeptSearches::Kept>::iterator KeptSearches::find( const FilteredView* view )
{
    return std::find_if( searches_.begin(), searches_.end(),
                         [ view ]( const Kept& kept ) { return kept.view == view; } );
}
