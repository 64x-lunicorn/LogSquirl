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

#include "viewset.h"

#include "filteredview.h"
#include "logpresentation.h"

#include <algorithm>

template <class Fn>
void ViewSet::forEachView( Fn&& fn ) const
{
    for ( const auto& held : views_ ) {
        if ( !held.isGone() ) {
            fn( held );
        }
    }
}

void ViewSet::addPresentation( LogPresentation* presentation, LogFileView* view )
{
    presentations_.push_back( presentation );
    views_.push_back( HeldView{ view, std::nullopt } );
    seed( *view );
    if ( currentSearch_ != nullptr ) {
        presentation->setCurrentSearch( currentSearch_ );
    }
}

void ViewSet::addFilteredView( FilteredView* view )
{
    std::erase_if( views_, []( const HeldView& held ) { return held.isGone(); } );
    views_.push_back( HeldView{ view, QPointer<FilteredView>( view ) } );
    // A Filtered View added is the current Search's, so the search pattern
    // seeded reaches it.
    currentFilteredView_ = view;
    seed( *view );
}

void ViewSet::makeSearchCurrent( FilteredView* view, const LogFilteredData* search )
{
    currentFilteredView_ = view;
    currentSearch_ = search;

    for ( auto* presentation : presentations_ ) {
        presentation->setCurrentSearch( search );
    }
    if ( overview_ != nullptr ) {
        overview_->setFilteredData( search );
    }
}

void ViewSet::setOverview( Overview* overview )
{
    overview_ = overview;
    if ( overview_ != nullptr && currentSearch_ != nullptr ) {
        overview_->setFilteredData( currentSearch_ );
    }
}

void ViewSet::setDecorationPolicy( const DecorationPolicy& policy )
{
    decorationPolicy_ = policy;
    forEachView( [ & ]( const HeldView& held ) { held.view->setDecorationPolicy( policy ); } );
}

void ViewSet::setPresentationPolicy( const PresentationPolicy& policy )
{
    presentationPolicy_ = policy;
    forEachView( [ & ]( const HeldView& held ) { held.view->setPresentationPolicy( policy ); } );
}

void ViewSet::setFollowAllowed( bool allowed )
{
    followAllowed_ = allowed;
    forEachView( [ & ]( const HeldView& held ) { held.view->allowFollowMode( allowed ); } );
}

void ViewSet::setFont( const QFont& font )
{
    font_ = font;
    forEachView( [ & ]( const HeldView& held ) { held.view->updateFont( font ); } );
}

void ViewSet::setColorLabels( const ColorLabels& labels )
{
    colorLabels_ = labels;
    forEachView( [ & ]( const HeldView& held ) { held.view->setColorLabels( labels ); } );
}

void ViewSet::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchLimits_ = std::make_pair( startLine, endLine );
    forEachView(
        [ & ]( const HeldView& held ) { held.view->setSearchLimits( startLine, endLine ); } );
}

void ViewSet::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;

    // A kept Search's Filtered View colors the pattern it ran with.
    forEachView( [ & ]( const HeldView& held ) {
        if ( held.isPresentation() || *held.filteredView == currentFilteredView_ ) {
            held.view->setSearchPattern( pattern );
        }
    } );
}

void ViewSet::refreshMatchesAndMarks( LinesCount logFileLines, Overview::UpdatePace pace )
{
    if ( !currentFilteredView_.isNull() ) {
        currentFilteredView_->updateData();
    }

    if ( overview_ != nullptr ) {
        overview_->updateData( logFileLines, pace );
    }

    // The Presentations draw a bullet for each Match and Mark.
    forEachView( []( const HeldView& held ) {
        if ( held.isPresentation() ) {
            held.view->updateDecorations();
        }
    } );
}

void ViewSet::applyHighlighterSetChange()
{
    // A Color Label's color comes from the Highlighter Set Collection and is
    // cached alongside its words, so handing the words over again is what
    // picks up a color the user just changed.
    setColorLabels( colorLabels_ );

    // Every view reads the active Highlighter Sets when it paints, so all a
    // change takes is painting again.
    forEachView( []( const HeldView& held ) { held.view->updateDecorations(); } );
}

void ViewSet::registerShortcuts()
{
    forEachView( []( const HeldView& held ) { held.view->registerShortcuts(); } );
}

void ViewSet::rereadLogLines()
{
    // The views keep the Log Lines they read until told otherwise, and every
    // Log Line may read differently now.
    forEachView( []( const HeldView& held ) { held.view->rereadLogLines(); } );
}

void ViewSet::seed( LogFileView& view ) const
{
    view.setPresentationPolicy( presentationPolicy_ );
    view.setDecorationPolicy( decorationPolicy_ );
    view.allowFollowMode( followAllowed_ );
    if ( font_ ) {
        view.updateFont( *font_ );
    }
    view.setColorLabels( colorLabels_ );
    if ( searchLimits_ ) {
        view.setSearchLimits( searchLimits_->first, searchLimits_->second );
    }
    if ( searchPattern_ ) {
        view.setSearchPattern( *searchPattern_ );
    }
}
