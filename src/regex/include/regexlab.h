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
#include <functional>
#include <optional>

#include <QString>
#include <QStringList>

#include "containers.h"
#include "regexpengine.h"
#include "regularexpressionpattern.h"

// What the Regex Lab shows of a pattern on sample Log Lines (#659), worked
// out without a widget so that whoever opens the Lab -- the menu, and later
// the Highlighter and Predefined Filter editors, the Search Line and plugins
// -- sees the same.
//
// Whether a line matches is decided by the Search's own matcher, compiled
// from the same pattern and options on the same engine, on the whole line:
// what the Lab counts is what a Search selects. Where a line matches, and its
// capture groups, come from the pattern read as a QRegularExpression, with
// its groups capturing. Under Inverse match a line matches where the pattern
// does not, so the lines counted are those without marks.
namespace regexlab {

// How far an evaluation goes. It runs off the UI thread, but a pattern that
// backtracks badly must still come to an end, and report that it stopped.
struct Bounds {
    // Sample lines evaluated at most; the rest of the sample is left out.
    std::size_t maxLines = 1000;
    // Characters of each line marked and shown at most. Whether a line
    // matches is decided on the whole line, as a Search decides it.
    qsizetype maxLineLength = 10'000;
    // Matches marked at most, in a line and over the whole sample: each one
    // costs the window that shows it, and many in one line cost the most.
    std::size_t maxMarksPerLine = 100;
    std::size_t maxMarks = 10'000;
    // Evaluation stops at the first line that starts after this much time.
    std::chrono::milliseconds timeLimit{ 2000 };
    // An evaluation that took longer, or ran out of time, is reported as
    // slow: a hint at a pattern that backtracks catastrophically.
    std::chrono::milliseconds slowThreshold{ 250 };
    // A single line whose verdict took longer is reported as slow, and so is
    // one the engine gave up on once it had backtracked too much, however
    // fast it did (#689).
    std::chrono::milliseconds slowLine{ 20 };
};

// The line cut to at most length characters, never between the two halves
// of a surrogate pair.
QString cutLine( const QString& line, qsizetype length );

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

// Whether a line matches, and what of it is marked, as decided by whoever
// opened the Lab instead of by the Search: a Highlighter decides and colors
// by its own rules (#660). The spans to mark, none for a line that does not
// match. Called off the UI thread, on each line's whole text.
using LineDecision
    = std::function<std::optional<logsquirl::vector<MatchSpan>>( const QString& line )>;

struct LineResult {
    // The Search's verdict on the line.
    bool isMatch = false;
    // Only the first Bounds::maxLineLength characters are marked.
    bool isCut = false;
    // Deciding the line took longer than Bounds::slowLine: what the Search,
    // or whoever decides, spent on it -- not what the Lab spends marking it.
    bool isSlow = false;
    logsquirl::vector<MatchSpan> matches;
    logsquirl::vector<CaptureGroup> groups;
    // For a logical combination: whether each of Result::subPatterns matches
    // the line, as the Search's engine finds it on the whole line -- what the
    // verdict is decided from (#661). Empty for any other pattern, and when
    // whoever opened the Lab decides.
    logsquirl::vector<bool> subPatternMatches;
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
    // The sub-patterns of a logical combination, in the order written, as
    // the Search reads them; empty for any other pattern.
    QStringList subPatterns;
    // One per sample line evaluated, in order: fewer than the sample when
    // the evaluation stopped.
    logsquirl::vector<LineResult> lines;
    // Sample lines within Bounds::maxLines.
    std::size_t sampleLines = 0;
    std::size_t matchingLines = 0;
    Stop stop = Stop::None;
    std::chrono::milliseconds elapsed{ 0 };
    bool isSlow = false;
    // Lines that took longer than Bounds::slowLine.
    std::size_t slowLines = 0;
    // Bounds::maxMarksPerLine or Bounds::maxMarks was reached: not every
    // match is marked.
    bool isMarkingCut = false;
};

// Evaluates the pattern on each sample line, as a Search with the same
// pattern and options on the same engine would, within the bounds -- or, with
// a decision, as that decides. Stops as soon as cancelled is set. Safe to
// call off the UI thread.
Result evaluate( const RegularExpressionPattern& pattern, RegexpEngine engine,
                 const logsquirl::vector<QString>& sample, const Bounds& bounds,
                 const std::atomic<bool>& cancelled, const LineDecision& decision = {} );

} // namespace regexlab

#endif
