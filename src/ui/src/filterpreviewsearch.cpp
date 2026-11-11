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

#include "filterpreviewsearch.h"

#include <QTimer>

#include <utility>

#include "logdata.h"
#include "logfiltereddata.h"
#include "searchresults.h"

FilterPreviewSearch::FilterPreviewSearch( std::shared_ptr<const LogData> logData,
                                          PreparePattern prepare, QObject* parent )
    : QObject( parent )
    , logData_( std::move( logData ) )
    , search_( logData_->getNewFilteredData() )
    , prepare_( std::move( prepare ) )
    , settle_( new QTimer( this ) )
{
    settle_->setSingleShot( true );
    settle_->setInterval( SettleMilliseconds );
    connect( settle_, &QTimer::timeout, this, &FilterPreviewSearch::startSearch );
    connect( search_.get(), &LogFilteredData::searchStateChanged, this,
             &FilterPreviewSearch::searchStateChanged );
}

FilterPreviewSearch::~FilterPreviewSearch()
{
    // Nothing is told while the Search goes.
    search_->disconnect( this );
    search_->stop();
}

void FilterPreviewSearch::setPattern( const RegularExpressionPattern& pattern )
{
    // Prepared now, not when it is searched for: what the Search tells is
    // told with the pattern it was requested with.
    pattern_ = prepare_ && !pattern.pattern.isEmpty() ? prepare_( pattern ) : pattern;
    // What runs is of a pattern no longer wanted.
    waiting_ = true;
    search_->stop();
    if ( pattern.pattern.isEmpty() ) {
        settle_->stop();
        waiting_ = false;
        search_->dropResults();
        setResult( Result{} );
        return;
    }
    Result searching;
    searching.state = Result::State::Searching;
    setResult( searching );
    settle_->start();
}

void FilterPreviewSearch::startSearch()
{
    waiting_ = false;
    search_->request( pattern_ );
}

void FilterPreviewSearch::searchStateChanged( const SearchSessionState& state )
{
    if ( waiting_ || !( state.pattern == pattern_ ) ) {
        return;
    }
    Result result;
    result.matchCount = state.matchCount;
    result.progress = state.progress;
    switch ( state.phase ) {
    case SearchSessionPhase::Idle:
        return;
    case SearchSessionPhase::Running:
    case SearchSessionPhase::Interrupted:
        result.state = Result::State::Searching;
        break;
    case SearchSessionPhase::Complete:
        result.state = Result::State::Done;
        break;
    case SearchSessionPhase::InvalidPattern:
    case SearchSessionPhase::Failed:
        result.state = Result::State::Failed;
        result.error = state.errorString;
        break;
    }
    if ( result.state != Result::State::Failed ) {
        result.firstMatch = lineAtPosition( search_->matches(), 0_lnum );
        // Read once, not on every progress the Search tells.
        if ( result.firstMatch && result.firstMatch == result_.firstMatch ) {
            result.firstLine = result_.firstLine;
        }
        else if ( result.firstMatch ) {
            result.firstLine = logData_->getLineString( *result.firstMatch );
        }
    }
    setResult( std::move( result ) );
}

void FilterPreviewSearch::setResult( Result result )
{
    result_ = std::move( result );
    Q_EMIT resultChanged();
}
