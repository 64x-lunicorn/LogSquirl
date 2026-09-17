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

#include "searchautorefresh.h"

void SearchAutoRefresh::resetState()
{
    state_ = State::NoSearch;
}

void SearchAutoRefresh::setAutoRefresh( bool refresh )
{
    autoRefreshRequested_ = refresh;

    if ( refresh ) {
        // A truncated Log File is not followed again by asking for it: only
        // a new Search is.
        if ( state_ == State::Static ) {
            state_ = State::Autorefreshing;
        }
    }
    else {
        if ( state_ == State::Autorefreshing ) {
            state_ = State::Static;
        }
        else if ( state_ == State::TruncatedAutorefreshing ) {
            state_ = State::FileTruncated;
        }
    }
}

void SearchAutoRefresh::truncateFile()
{
    if ( state_ == State::Autorefreshing || state_ == State::TruncatedAutorefreshing ) {
        state_ = State::TruncatedAutorefreshing;
    }
    else {
        state_ = State::FileTruncated;
    }
}

void SearchAutoRefresh::changeExpression()
{
    if ( state_ == State::Autorefreshing ) {
        state_ = State::Static;
    }
}

void SearchAutoRefresh::stopSearch()
{
    if ( state_ == State::Autorefreshing ) {
        state_ = State::Static;
    }
}

void SearchAutoRefresh::startSearch()
{
    state_ = autoRefreshRequested_ ? State::Autorefreshing : State::Static;
}
