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

#include "fronttabbinding.h"

#include <algorithm>

#include <QActionGroup>

#include "fronttabactions.h"
#include "mainwindow.h"

namespace {

// An action of the Time Navigation: its tooltip says why it is not
// available, or is its status tip when it is.
void applyOffered( QAction* action, const FrontTabActions::Offered& offered )
{
    action->setEnabled( offered.enabled );
    action->setToolTip( offered.unavailable.isEmpty() ? action->statusTip() : offered.unavailable );
}

} // namespace

FrontTabBinding::FrontTabBinding( MainWindow& window )
    : window_( window )
{
}

FrontTabBinding::~FrontTabBinding()
{
    disconnectAll();
}

CrawlerWidget* FrontTabBinding::crawler() const
{
    return crawler_.data();
}

void FrontTabBinding::disconnectAll()
{
    for ( const auto& connection : connections_ ) {
        QObject::disconnect( connection );
    }
    connections_.clear();
}

void FrontTabBinding::bind( CrawlerWidget* crawler )
{
    disconnectAll();
    crawler_ = crawler;

    if ( crawler != nullptr ) {
        auto& w = window_;
        connections_ = {
            // What the window asks of the Log File in front
            QObject::connect( &w, &MainWindow::followSet, crawler, &CrawlerWidget::followSet ),
            QObject::connect( &w, &MainWindow::textWrapSet, crawler, &CrawlerWidget::textWrapSet ),
            QObject::connect( &w, &MainWindow::valueNamesShownSet, crawler,
                              &CrawlerWidget::valueNamesShownSet ),
            QObject::connect( &w, &MainWindow::matchesOnlySet, crawler,
                              &CrawlerWidget::setMatchesOnly ),
            QObject::connect( &w, &MainWindow::enteringQuickFind, crawler,
                              &CrawlerWidget::enteringQuickFind ),
            QObject::connect( &w.quickFindWidget_, &QuickFindWidget::close, crawler,
                              &CrawlerWidget::exitingQuickFind ),
            QObject::connect( w.goToLineAction, &QAction::triggered, crawler,
                              &CrawlerWidget::goToLine ),
            QObject::connect( w.goToTimestampAction, &QAction::triggered, crawler,
                              &CrawlerWidget::goToTimestamp ),
            QObject::connect( w.searchLimitsTimeRangeAction, &QAction::triggered, crawler,
                              &CrawlerWidget::setSearchLimitsToTimeRange ),
            QObject::connect( w.searchLimitsAroundLineAction, &QAction::triggered, crawler,
                              &CrawlerWidget::setSearchLimitsAroundCurrentLine ),
            QObject::connect( w.reloadAction, &QAction::triggered, crawler,
                              &CrawlerWidget::reload ),
            QObject::connect( w.stopAction, &QAction::triggered, crawler,
                              &CrawlerWidget::stopLoading ),

            // What the Log File in front tells the window
            QObject::connect( crawler, &CrawlerWidget::followModeChanged, &w,
                              &MainWindow::changeFollowMode ),
            QObject::connect( crawler, &CrawlerWidget::matchesOnlyChanged, &w,
                              &MainWindow::changeMatchesOnly ),
            QObject::connect( crawler, &CrawlerWidget::newSelection, &w,
                              &MainWindow::lineNumberHandler ),
            QObject::connect( crawler, &CrawlerWidget::saveCurrentSearchAsPredefinedFilter, &w,
                              &MainWindow::newPredefinedFilterHandler ),
            QObject::connect( crawler, &CrawlerWidget::sendToScratchpad, &w,
                              &MainWindow::sendToScratchpad ),
            QObject::connect( crawler, &CrawlerWidget::nameValueRequested, &w,
                              &MainWindow::nameValue ),
            QObject::connect( crawler, &CrawlerWidget::replaceDataInScratchpad, &w,
                              &MainWindow::replaceDataInScratchpad ),
            QObject::connect( crawler, &CrawlerWidget::loadingProgressed, &w,
                              &MainWindow::updateLoadingProgress ),
            QObject::connect( crawler, &CrawlerWidget::loadingFinished, &w,
                              &MainWindow::handleLoadingFinished ),
            QObject::connect( crawler, &CrawlerWidget::statusMessage, &w,
                              &MainWindow::showStatusMessage ),
            QObject::connect( crawler, &CrawlerWidget::highlightThisRequested, &w,
                              &MainWindow::highlightThis ),
            QObject::connect( crawler, &CrawlerWidget::addAsPredefinedFilterRequested, &w,
                              &MainWindow::addAsPredefinedFilter ),
            // QuickFind searches the Filtered View now in front.
            QObject::connect(
                crawler, &CrawlerWidget::filteredViewChanged, &w,
                [ this, crawler ] { window_.quickFindMux_.registerSelector( crawler ); } ),
        };
    }

    window_.quickFindMux_.registerSelector( crawler );
    apply();
}

void FrontTabBinding::apply()
{
    auto& w = window_;
    const auto actions = frontTabActions( crawler_ != nullptr ? std::optional{ crawler_->state() }
                                                              : std::nullopt );

    const auto& encodingMib = actions.encodingMib;
    const auto encodingActions = w.encodingGroup->actions();
    const auto encodingItem = std::find_if(
        encodingActions.begin(), encodingActions.end(), [ &encodingMib ]( const auto* action ) {
            return ( !encodingMib && !action->data().isValid() )
                   || ( encodingMib && action->data().isValid()
                        && *encodingMib == action->data().toInt() );
        } );
    if ( encodingItem != encodingActions.end() ) {
        ( *encodingItem )->setChecked( true );
    }

    // Only a change goes through changeFollowMode(), which warns when the
    // Watch Policy watches nothing: not again on every load progress.
    if ( w.followAction->isChecked() != actions.follows ) {
        w.changeFollowMode( actions.follows );
    }
    w.textWrapAction->setChecked( actions.textWrap );
    w.showValueNamesAction->setChecked( actions.valueNamesShown );
    w.matchesOnlyAction->setEnabled( actions.matchesOnlyEnabled );
    w.changeMatchesOnly( actions.matchesOnly );
    w.exportAsCsvAction->setEnabled( actions.exportAsCsvEnabled );

    applyOffered( w.goToTimestampAction, actions.goToTimestamp );
    for ( auto* action : { w.searchLimitsTimeRangeAction, w.searchLimitsAroundLineAction } ) {
        applyOffered( action, actions.searchLimitsByTime );
    }

    w.stopAction->setEnabled( actions.stopEnabled );
    w.reloadAction->setEnabled( actions.reloadEnabled );

    w.editMenu->setEnabled( actions.logFileActionsEnabled );
    for ( auto* action : { w.copyPathToClipboardAction, w.openContainingFolderAction,
                           w.openInEditorAction, w.clearLogAction } ) {
        action->setEnabled( actions.logFileActionsEnabled );
    }
}
