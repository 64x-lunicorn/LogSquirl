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

#ifndef SEARCHAUTOREFRESH_H
#define SEARCHAUTOREFRESH_H

// Whether the Search of an Open Log File follows the Log File as it changes
// on disk: the auto-refresh state machine. It decides whether a Search may be
// refreshed when Log Lines were loaded again, and whether the Log File was
// truncated under it; what refreshing means is up to the Open Log File.
class SearchAutoRefresh {
public:
    enum class State {
        // No Search is active.
        NoSearch,
        // A Search is active and is not refreshed.
        Static,
        // A Search is active and is refreshed as the Log File grows.
        Autorefreshing,
        // The Log File was truncated under a Search that is not refreshed.
        FileTruncated,
        // The Log File was truncated under a Search that is refreshed: it
        // starts again once the Log File has loaded.
        TruncatedAutorefreshing,
    };

    // No Search is active.
    void resetState();
    // The user asked for auto-refresh, or no longer does.
    void setAutoRefresh( bool refresh );
    // The Log File was truncated.
    void truncateFile();
    // The Search pattern was changed: auto-refresh is suspended.
    void changeExpression();
    // The Search was stopped: auto-refresh is suspended.
    void stopSearch();
    // A Search was started: auto-refreshed if the user asked for it.
    void startSearch();

    State state() const
    {
        return state_;
    }

    bool isAutoRefreshRequested() const
    {
        return autoRefreshRequested_;
    }

    // Whether the Search is to follow Log Lines loaded again.
    bool isAutoRefreshAllowed() const
    {
        return state_ == State::Autorefreshing || state_ == State::TruncatedAutorefreshing;
    }

    bool isFileTruncated() const
    {
        return state_ == State::FileTruncated || state_ == State::TruncatedAutorefreshing;
    }

private:
    State state_ = State::NoSearch;
    bool autoRefreshRequested_ = false;
};

#endif
