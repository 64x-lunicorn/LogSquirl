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

#include "logfiltereddataworker.h"

// What changed in a Search's Matches with one state change of its Search
// Session: how the run stands, and which Matches joined and left them.
//
// It points into the Search Session's own bitmaps, so it is valid only for
// the length of the call it is handed to: whoever needs any of it later
// copies what it needs there. It never travels in the Search's State, which
// is queued to the user interface.
struct MatchesDelta {
    enum class Outcome {
        // The Search was cleared, its pattern was invalid or it failed: there
        // are no Matches.
        Discarded,
        // Matches arrived while a Search runs, or when it was stopped.
        Arrived,
        // The Search completed, from a real run or from the cache.
        Completed,
    };

    Outcome outcome;
    // The Matches that joined them, none of which was a Match before; nullptr
    // when the Matches were replaced (a new run, a cache hit, a reset), so
    // that only all of them tell what changed.
    const SearchResultArray* added;
    // The Matches that left them, each of which was a Match before: a Search
    // continued over a grown Log File searches its previously last Log Line
    // again, and drops its Match when it no longer matches. Empty whenever
    // added is nullptr.
    const SearchResultArray& removed;
};
