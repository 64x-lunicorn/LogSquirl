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

#ifndef LOGSQUIRL_REGEXLAB_H
#define LOGSQUIRL_REGEXLAB_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>

#include <QString>

#include "containers.h"
#include "regexpengine.h"
#include "regularexpressionpattern.h"

// What the Regex Lab shows of a pattern on sample Log Lines (#659), worked
// out without a widget so that whoever opens the Lab -- the menu, and later
// the Highlighter and Predefined Filter editors, the Search Line and plugins
// -- sees the same.
//
// Whether a line matches is decided by the Search's own matcher, compiled
// from the same pattern and options on the same engine: what the Lab counts
// is what a Search selects. Where a line matches, and its capture groups,
// come from the pattern read as a QRegularExpression, with its groups
// capturing.
namespace regexlab {

// How far an evaluation goes. It runs off the UI thread, but a pattern that
// backtracks badly must still come to an end, and report that it stopped.
struct Bounds {
    // Sample lines evaluated at most; the rest of the sample is left out.
    std::size_t maxLines = 1000;
    // Characters of each line evaluated at most; a longer line is cut there.
    qsizetype maxLineLength = 10'000;
    // Evaluation stops at the first line that starts after this much time.
    std::chrono::milliseconds timeLimit{ 2000 };
    // An evaluation that took longer, or ran out of time, is reported as
    // slow: a hint at a pattern that backtracks catastrophically.
    std::chrono::milliseconds slowThreshold{ 250 };
};

// A pattern the Search would not run: what it says, and where in the pattern
// as written, when that is known.
struct PatternError {
    QString message;
    // Index into the pattern, or -1 when it is not known.
    qsizetype position = -1;
};

// Where a pattern matched a line, in characters of the line.
struct MatchSpan {
    qsizetype start = 0;
    qsizetype length = 0;
    // The sub-pattern of a logical combination that matched, in the order
    // written; 0 for a pattern that is no combination.
    int subPattern = 0;
};

// One capture group of the first match of a (sub-)pattern in a line.
struct CaptureGroup {
    int subPattern = 0;
    // 0 is the whole match.
    int number = 0;
    // Empty for a group without a name.
    QString name;
    QString text;
    // -1 when the group took no part in the match.
    qsizetype start = -1;
};

struct LineResult {
    // The Search's verdict on the line.
    bool isMatch = false;
    // Only the first Bounds::maxLineLength characters were evaluated.
    bool isCut = false;
    logsquirl::vector<MatchSpan> matches;
    logsquirl::vector<CaptureGroup> groups;
};

enum class Stop {
    // Every sample line within Bounds::maxLines was evaluated.
    None,
    // Bounds::timeLimit ran out.
    TimeLimit,
    // Whoever asked let go of the evaluation.
    Cancelled,
};

struct Result {
    // Set when the pattern is invalid; nothing was evaluated then.
    std::optional<PatternError> error;
    // One per sample line evaluated, in order: fewer than the sample when
    // the evaluation stopped.
    logsquirl::vector<LineResult> lines;
    // Sample lines within Bounds::maxLines.
    std::size_t sampleLines = 0;
    std::size_t matchingLines = 0;
    Stop stop = Stop::None;
    std::chrono::milliseconds elapsed{ 0 };
    bool isSlow = false;
};

// Why the Search would not run this pattern, if it would not: the message the
// Search shows, and where in the pattern the error is.
std::optional<PatternError> patternError( const RegularExpressionPattern& pattern,
                                          RegexpEngine engine );

// Evaluates the pattern on each sample line, as a Search with the same
// pattern and options on the same engine would, within the bounds. Stops as
// soon as cancelled is set. Safe to call off the UI thread.
Result evaluate( const RegularExpressionPattern& pattern, RegexpEngine engine,
                 const logsquirl::vector<QString>& sample, const Bounds& bounds,
                 const std::atomic<bool>& cancelled );

} // namespace regexlab

#endif
