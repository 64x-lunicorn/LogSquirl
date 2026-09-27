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
}

KeptSearches::~KeptSearches() = default;

FilteredView* KeptSearches::showCurrentSearch()
{
    return add( openLogFile_->filteredData() );
}

FilteredView* KeptSearches::startAnother()
{
    return add( openLogFile_->startAnotherSearch() );
}

FilteredView* KeptSearches::add( std::shared_ptr<LogFilteredData> search )
{
    auto* view = buildView_( search.get() );

    // Queued, so that what a Search tells while it is requested reaches the
    // listener once the request is done; the Search it came from is asked
    // for when it arrives, since another may have been made current since.
    auto updates = connect(
        search.get(), &LogFilteredData::searchStateChanged, this,
        [ this, told = std::weak_ptr<const LogFilteredData>( search ) ](
            const SearchSession::State& state ) {
            const auto teller = told.lock();
            if ( teller != nullptr && teller == openLogFile_->filteredData() ) {
                Q_EMIT currentSearchUpdated( state );
            }
        },
        Qt::QueuedConnection );

    searches_.push_back( Kept{ view, search, updates } );
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

    disconnect( found->updates );
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

std::vector<KeptSearches::Kept>::iterator KeptSearches::find( const FilteredView* view )
{
    return std::find_if( searches_.begin(), searches_.end(),
                         [ view ]( const Kept& kept ) { return kept.view == view; } );
}
