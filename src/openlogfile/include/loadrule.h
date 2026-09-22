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

#pragma once

#include "containers.h"
#include "linetypes.h"
#include "loadingstatus.h"

class SearchAutoRefresh;

// What a load, a change on disk and a reload mean for an Open Log File,
// decided in one place and without reading the Log File (#396): whether the
// load that finishes brings only lines that were added, whether the Marks are
// cleared and the Log Format is recognized again, whether the Marks saved with
// the Session are applied, and whether a Search waiting for the first load
// runs now. Whether a Search continues or starts again after a truncation it
// asks the Search's auto-refresh, which keeps deciding that.
//
// Like the auto-refresh it is a state machine without Qt, used on the Open
// Log File's thread only: each event goes in, a decision comes out, and the
// Open Log File carries it out.
class LoadRule {
public:
    // How the Search follows the Log Lines a finished load brought.
    enum class SearchRefresh {
        // It is not refreshed.
        None,
        // It continues over the Log Lines added.
        Continue,
        // It starts again over the Log File, which was truncated under it.
        Restart,
    };

    // What the users are told of a change on disk.
    enum class Change { Grew, Truncated };

    struct ReloadDecision {
        // The Search is dropped, with its cached results.
        bool dropSearch = false;
        bool clearMarks = false;
    };

    struct ChangeDecision {
        Change report = Change::Grew;
        bool clearMarks = false;
        // The Search's results and its cache no longer describe the Log File:
        // they are dropped, and the auto-refresh hears of the truncation.
        bool dropSearch = false;
        // The Log Format is forgotten; it is recognized again once the Log
        // File has loaded.
        bool forgetLogFormat = false;
    };

    struct LoadDecision {
        // Loaded from its start -- the Log File was opened or reloaded by
        // hand -- rather than loaded again after it changed on disk.
        bool fromStart = false;
        // Log Lines were only added at the end of the Log File since the last
        // load.
        bool onlyAppended = false;
        SearchRefresh searchRefresh = SearchRefresh::None;
        // The Marks saved with the Session, to apply now: after the first
        // load, once.
        logsquirl::vector<LineNumber> savedMarksToApply;
        // The Search requested before the first load runs now, over the whole
        // Log File.
        bool runWaitingSearch = false;
        // Format Recognition is taken.
        bool recognizeFormat = false;
    };

    // Marks saved with the Session for the Log File, applied after the first
    // load.
    void restoreMarks( const logsquirl::vector<LineNumber>& marks );

    // The Log File is loaded again from its start, by hand.
    ReloadDecision reload();

    // A check of the Log File on disk found it changed.
    ChangeDecision changedOnDisk( MonitoredFileStatus status );

    // A Search was requested, valid or not. Returns whether it waits for the
    // first load, as no load has finished yet.
    [[nodiscard]] bool searchRequested();
    // No Search is active any longer.
    void searchCleared();
    // A Search waiting for the first load no longer does: it was stopped, or
    // another one was made current.
    void waitingSearchDropped();

    // A load of the Log File finished with lineCount Log Lines, whatever its
    // outcome. autoRefresh is the current Search's, as it stands now.
    LoadDecision loadFinished( LoadingStatus status, LinesCount lineCount,
                               const SearchAutoRefresh& autoRefresh );

    // Whether the Log File is loading from its start: until the first load
    // has finished, and again after a reload until its load has.
    bool isLoadingFromStart() const;
    // The Marks saved with the Session that are still to be applied.
    const logsquirl::vector<LineNumber>& savedMarks() const;

private:
    // Whether a Search was requested since the last clearSearch or reload,
    // valid or not.
    bool searchRequested_ = false;
    // A Search was requested before the first load finished, and runs once it
    // has.
    bool searchWaitsForLoad_ = false;
    // Whether a load of the Log File has finished, whatever its outcome.
    bool loadFinishedOnce_ = false;
    // Whether the load from the start -- the first, or the one after a reload
    // -- has finished.
    bool firstLoadDone_ = false;
    // What changed on disk since the last load finished: whether Log Lines
    // were added, and whether the Log File was truncated, which a later
    // growth does not undo.
    bool grewSinceLoad_ = false;
    bool truncatedSinceLoad_ = false;
    logsquirl::vector<LineNumber> savedMarks_;
    // Whether the next load to finish is to recognize the Log Format: the
    // first load, and the one after a reload or a truncation.
    bool formatRecognitionPending_ = true;
};
