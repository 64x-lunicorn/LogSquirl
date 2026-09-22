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
void ViewSet::forEachFilteredView( Fn&& fn ) const
{
    for ( const auto& view : filteredViews_ ) {
        if ( !view.isNull() ) {
            fn( view.data() );
        }
    }
}

void ViewSet::addPresentation( LogPresentation* presentation )
{
    presentations_.push_back( presentation );
    seed( presentation );
}

void ViewSet::addFilteredView( FilteredView* view )
{
    std::erase_if( filteredViews_, []( const auto& held ) { return held.isNull(); } );
    filteredViews_.emplace_back( view );
    currentFilteredView_ = view;
    seed( view );
}

void ViewSet::makeFilteredViewCurrent( FilteredView* view )
{
    currentFilteredView_ = view;
}

void ViewSet::setOverview( Overview* overview )
{
    overview_ = overview;
}

void ViewSet::setDecorationPolicy( const DecorationPolicy& policy )
{
    decorationPolicy_ = policy;

    for ( auto* presentation : presentations_ ) {
        presentation->setDecorationPolicy( policy );
    }
    forEachFilteredView( [ & ]( FilteredView* view ) { view->setDecorationPolicy( policy ); } );
}

void ViewSet::setPresentationPolicy( const PresentationPolicy& policy )
{
    presentationPolicy_ = policy;

    for ( auto* presentation : presentations_ ) {
        presentation->setPresentationPolicy( policy );
    }
    forEachFilteredView( [ & ]( FilteredView* view ) {
        view->setPresentationPolicy( policy );
        view->setLineNumbersVisible( policy.filteredLineNumbersVisible );
    } );
}

void ViewSet::setQuickFindPolicy( const QuickFindPolicy& policy )
{
    quickFindPolicy_ = policy;

    for ( auto* presentation : presentations_ ) {
        presentation->setQuickFindPolicy( policy );
    }
}

void ViewSet::setFollowAllowed( bool allowed )
{
    followAllowed_ = allowed;

    for ( auto* presentation : presentations_ ) {
        presentation->allowFollowMode( allowed );
    }
    forEachFilteredView( [ & ]( FilteredView* view ) { view->allowFollowMode( allowed ); } );
}

void ViewSet::setFont( const QFont& font )
{
    font_ = font;

    for ( auto* presentation : presentations_ ) {
        presentation->updateFont( font );
    }
    forEachFilteredView( [ & ]( FilteredView* view ) { view->updateFont( font ); } );
}

void ViewSet::setColorLabels( const ColorLabels& labels )
{
    colorLabels_ = labels;

    for ( auto* presentation : presentations_ ) {
        presentation->setColorLabels( labels );
    }
    forEachFilteredView( [ & ]( FilteredView* view ) { view->setQuickHighlighters( labels ); } );
}

void ViewSet::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchLimits_ = std::make_pair( startLine, endLine );

    for ( auto* presentation : presentations_ ) {
        presentation->setSearchLimits( startLine, endLine );
    }
    forEachFilteredView(
        [ & ]( FilteredView* view ) { view->setSearchLimits( startLine, endLine ); } );
}

void ViewSet::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;

    for ( auto* presentation : presentations_ ) {
        presentation->setSearchPattern( pattern );
    }
    if ( !currentFilteredView_.isNull() ) {
        currentFilteredView_->setSearchPattern( pattern );
    }
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
    for ( auto* presentation : presentations_ ) {
        presentation->updateDecorations();
    }
}

void ViewSet::applyHighlighterSetChange()
{
    // A Color Label's color comes from the Highlighter Set Collection and is
    // cached alongside its words, so handing the words over again is what
    // picks up a color the user just changed.
    setColorLabels( colorLabels_ );

    // Every view reads the active Highlighter Sets when it paints, so all a
    // change takes is painting again.
    for ( auto* presentation : presentations_ ) {
        presentation->updateDecorations();
    }
    forEachFilteredView( []( FilteredView* view ) { view->updateDecorations(); } );
}

void ViewSet::registerShortcuts()
{
    for ( auto* presentation : presentations_ ) {
        presentation->registerShortcuts();
    }
    forEachFilteredView( []( FilteredView* view ) { view->registerShortcuts(); } );
}

void ViewSet::rereadLogLines()
{
    // The views keep the Log Lines they read until told otherwise, and every
    // Log Line may read differently now.
    for ( auto* presentation : presentations_ ) {
        presentation->rereadLogLines();
    }
    forEachFilteredView( []( FilteredView* view ) { view->rereadLogLines(); } );
}

void ViewSet::seed( LogPresentation* presentation ) const
{
    presentation->setPresentationPolicy( presentationPolicy_ );
    presentation->setQuickFindPolicy( quickFindPolicy_ );
    presentation->setDecorationPolicy( decorationPolicy_ );
    presentation->allowFollowMode( followAllowed_ );
    if ( font_ ) {
        presentation->updateFont( *font_ );
    }
    presentation->setColorLabels( colorLabels_ );
    if ( searchLimits_ ) {
        presentation->setSearchLimits( searchLimits_->first, searchLimits_->second );
    }
    if ( searchPattern_ ) {
        presentation->setSearchPattern( *searchPattern_ );
    }
}

void ViewSet::seed( FilteredView* view ) const
{
    view->setPresentationPolicy( presentationPolicy_ );
    view->setLineNumbersVisible( presentationPolicy_.filteredLineNumbersVisible );
    view->setDecorationPolicy( decorationPolicy_ );
    view->allowFollowMode( followAllowed_ );
    if ( font_ ) {
        view->updateFont( *font_ );
    }
    view->setQuickHighlighters( colorLabels_ );
    if ( searchLimits_ ) {
        view->setSearchLimits( searchLimits_->first, searchLimits_->second );
    }
    // A Filtered View added is the current Search's.
    if ( searchPattern_ ) {
        view->setSearchPattern( *searchPattern_ );
    }
}
