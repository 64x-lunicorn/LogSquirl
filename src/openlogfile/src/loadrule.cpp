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

#include "loadrule.h"

#include "searchautorefresh.h"

#include <utility>

void LoadRule::restoreMarks( const logsquirl::vector<LineNumber>& marks )
{
    savedMarks_.insert( savedMarks_.end(), marks.begin(), marks.end() );
}

LoadRule::ReloadDecision LoadRule::reload()
{
    searchRequested_ = false;
    searchWaitsForLoad_ = false;

    // A reload recognizes the Log Format again, so an edited user Log Format
    // is picked up by reloading.
    formatRecognitionPending_ = true;

    // A reload is loaded from its start, like the first load. The saved Marks
    // stay: a reload asked for before the first load finished leaves them to
    // the load that finishes.
    firstLoadDone_ = false;
    truncatedSinceLoad_ = true;

    ReloadDecision decision;
    decision.dropSearch = true;
    decision.clearMarks = true;
    return decision;
}

LoadRule::ChangeDecision LoadRule::changedOnDisk( MonitoredFileStatus status )
{
    ChangeDecision decision;
    switch ( status ) {
    case MonitoredFileStatus::Truncated:
        truncatedSinceLoad_ = true;
        decision.report = Change::Truncated;
        // Marks do not survive a truncation.
        decision.clearMarks = true;
        decision.dropSearch = searchRequested_;
        // Recognized again once the truncated Log File has loaded.
        decision.forgetLogFormat = true;
        formatRecognitionPending_ = true;
        break;
    case MonitoredFileStatus::DataAdded:
        grewSinceLoad_ = true;
        decision.report = Change::Grew;
        break;
    case MonitoredFileStatus::Unchanged:
        // Kept as it was: reported as grown, though nothing was added. The
        // log data tells only a check that found a change, so it does not
        // come here today.
        decision.report = Change::Grew;
        break;
    }
    return decision;
}

bool LoadRule::searchRequested()
{
    searchRequested_ = true;
    if ( loadFinishedOnce_ ) {
        return false;
    }
    // Nothing to search yet: it runs over the Log Lines once they have
    // loaded, rather than over none now.
    searchWaitsForLoad_ = true;
    return true;
}

void LoadRule::searchCleared()
{
    searchRequested_ = false;
    searchWaitsForLoad_ = false;
}

void LoadRule::waitingSearchDropped()
{
    searchWaitsForLoad_ = false;
}

LoadRule::LoadDecision LoadRule::loadFinished( LoadingStatus status, LinesCount lineCount,
                                               const SearchAutoRefresh& autoRefresh )
{
    LoadDecision decision;
    decision.fromStart = !firstLoadDone_;
    decision.onlyAppended = firstLoadDone_ && grewSinceLoad_ && !truncatedSinceLoad_;
    grewSinceLoad_ = false;
    truncatedSinceLoad_ = false;

    // The Search follows the Log Lines loaded: it continues over the ones
    // added, or starts again over a Log File truncated under it.
    if ( autoRefresh.isAutoRefreshAllowed() ) {
        decision.searchRefresh
            = autoRefresh.isFileTruncated() ? SearchRefresh::Restart : SearchRefresh::Continue;
    }

    if ( !firstLoadDone_ ) {
        firstLoadDone_ = true;
        // Applied once: a reload clears the Marks, and they stay cleared.
        decision.savedMarksToApply = std::exchange( savedMarks_, {} );
    }

    loadFinishedOnce_ = true;
    if ( std::exchange( searchWaitsForLoad_, false ) ) {
        // The Search requested while the Log File loaded runs over the whole
        // of it now; a Log File that did not load has nothing to search.
        if ( status == LoadingStatus::Successful ) {
            decision.runWaitingSearch = true;
        }
        else {
            searchRequested_ = false;
        }
    }

    // A Log File with no Log Lines yet has nothing to recognize from, so it
    // waits for a load that brings some.
    if ( formatRecognitionPending_ && lineCount.get() > 0 ) {
        formatRecognitionPending_ = false;
        decision.recognizeFormat = true;
    }

    return decision;
}

bool LoadRule::isLoadingFromStart() const
{
    return !firstLoadDone_;
}

const logsquirl::vector<LineNumber>& LoadRule::savedMarks() const
{
    return savedMarks_;
}
