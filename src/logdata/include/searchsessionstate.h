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

#ifndef LOGSQUIRL_SEARCH_SESSION_STATE_H
#define LOGSQUIRL_SEARCH_SESSION_STATE_H

#include <QMetaType>
#include <QString>

#include "linetypes.h"
#include "regularexpressionpattern.h"

// The state of a Search Session as it reports it, and the phase it is in: all
// that whoever only shows or passes on a Search needs, without the Search
// Session, its worker and its throttler. The Search Session knows them as
// SearchSession::Phase and SearchSession::State.

enum class SearchSessionPhase {
    Idle,           // no pattern requested (or request() with no pattern)
    Running,        // a run is in flight
    Interrupted,    // stop() cut a run short; results kept are partial
    Complete,       // the requested range has been fully searched
    InvalidPattern, // the pattern failed to compile; nothing was run
    Failed          // the run failed; errorString describes why, no results are kept
};

struct SearchSessionState {
    RegularExpressionPattern pattern;
    LineNumber startLine{ 0 };
    LineNumber endLine{ 0 };
    LinesCount matchCount{ 0 };
    // The Log Lines searched that the regex engine gave up on: whether they
    // match is not known (#689).
    LinesCount undecidedCount{ 0 };
    int progress = 0;
    SearchSessionPhase phase = SearchSessionPhase::Idle;
    bool fromCache = false;
    // True when this run continues a previous one (same pattern and
    // startLine, a grown endLine) rather than starting fresh -- e.g.
    // autorefresh extending the range as the file grows.
    bool isContinuation = false;
    QString errorString;
};

Q_DECLARE_METATYPE( SearchSessionState )

#endif
