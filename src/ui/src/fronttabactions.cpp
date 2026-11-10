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

#include "fronttabactions.h"

namespace {

FrontTabActions::Offered offered( const QString& unavailable )
{
    return { unavailable.isEmpty(), unavailable };
}

} // namespace

FrontTabActions frontTabActions( const std::optional<CrawlerWidget::State>& state )
{
    FrontTabActions actions;
    if ( !state.has_value() ) {
        return actions;
    }

    actions.encodingMib = state->encodingMib;
    actions.follows = state->follows;
    actions.textWrap = state->textWrap;
    actions.valueNamesShown = state->valueNamesShown;

    actions.matchesOnlyEnabled = state->hasTableView;
    actions.matchesOnly = state->matchesOnly;
    actions.exportAsCsvEnabled = state->hasTableView;

    actions.goToTimestamp = offered( state->goToTimestampUnavailable );
    actions.searchLimitsByTime = offered( state->searchLimitsByTimeUnavailable );

    // A load that failed or ran out of memory closes the tab; until it is
    // gone, the tab is shown as it was while loading.
    const auto& status = state->loadStatus;
    const bool loaded = status.has_value()
                        && ( *status == LoadingStatus::Successful
                             || ( *status == LoadingStatus::Interrupted && state->hasLoaded ) );
    actions.reloadEnabled = loaded;
    actions.stopEnabled = !loaded;

    actions.logFileActionsEnabled = true;
    return actions;
}
