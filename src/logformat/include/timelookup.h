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

#include "linetypes.h"

#include <QDate>
#include <QDateTime>
#include <QString>

#include <functional>
#include <optional>

class AbstractLogData;
class TimestampReader;

// Where a time lies in a Log File, found by a binary search over the
// Timestamps of its Log Lines.
namespace timelookup {

// The Timestamp of a Log Line, none for a Log Line without one.
using TimestampAt = std::function<std::optional<QDateTime>( LineNumber )>;

enum class Position {
    // The Log Line is the first one whose Timestamp is at or after the time.
    AtOrAfter,
    // The time lies before every Timestamp: the line is the first Log Line.
    BeforeFirst,
    // The time lies after every Timestamp: the line is the last Log Line.
    AfterLast,
    // No Log Line has a Timestamp: the line is the first Log Line.
    NoTimestamps,
};

struct Result {
    LineNumber line{ 0 };
    Position position = Position::NoTimestamps;
};

// How far a probe looks for a Log Line with a Timestamp among Log Lines
// without one (a stack trace) before it counts the stretch as having none.
constexpr uint64_t MaxLinesWithoutTimestamp = 5'000;

// The first Log Line, among lineCount, whose Timestamp is at or after time.
// Log Lines without a Timestamp are skipped by scanning to the nearest one
// that has one. A Log File that is not in time order gives an approximate
// answer: the binary search assumes the order, and does not check it. The
// Log Lines are read O(log n) times; none if lineCount is 0.
std::optional<Result> firstLineAtOrAfter( const QDateTime& time, LinesCount lineCount,
                                          const TimestampAt& timestampAt );

// The Timestamp of the Log Line nearest to line, looking at it first, then at
// the lines after it and before it alternately, up to MaxLinesWithoutTimestamp
// in each direction; none if there is none.
std::optional<QDateTime> timestampNear( LineNumber line, LinesCount lineCount,
                                        const TimestampAt& timestampAt );

// The same over a Log File, reading its Log Lines with the reader.
std::optional<Result> firstLineAtOrAfter( const QDateTime& time, const AbstractLogData& logData,
                                          const TimestampReader& reader );
std::optional<QDateTime> timestampNear( LineNumber line, const AbstractLogData& logData,
                                        const TimestampReader& reader );

// Search Limits given as a time range, as Log Lines: the half-open range
// [start, end), like every Search Limits.
struct LimitsResult {
    enum class Outcome {
        // start and end are the Log Lines to limit the Search to.
        Limits,
        // The range lies entirely before the first Timestamp of the Log File.
        BeforeFile,
        // The range lies entirely after the last Timestamp of the Log File.
        AfterFile,
        // The Log File has no Log Line with a Timestamp, or no Log Lines.
        NoTimestamps,
        // The end is not after the start.
        EndNotAfterStart,
        // The range lies inside the Log File but no Log Line falls into it.
        NoLogLines,
    };
    Outcome outcome = Outcome::NoTimestamps;
    LineNumber start{ 0 };
    LineNumber end{ 0 };
};

// Converts a time range to Search Limits: start is the first Log Line with a
// Timestamp at or after startTime, end the first at or after endTime (the
// line count when there is none), so a Log Line without a Timestamp belongs
// to the range of the Log Line before it. A start before the Log File is the
// first Log Line. Only the Limits outcome carries lines.
LimitsResult searchLimitsForTimeRange( const QDateTime& startTime, const QDateTime& endTime,
                                       LinesCount lineCount, const TimestampAt& timestampAt );

// The same over a Log File, reading its Log Lines with the reader.
LimitsResult searchLimitsForTimeRange( const QDateTime& startTime, const QDateTime& endTime,
                                       const AbstractLogData& logData,
                                       const TimestampReader& reader );

// Reads a time a user typed: "14:02", "14:02:30", "14:02:30.250", optionally
// after a date, "2026-09-23 14:02" or "2026-09-23T14:02" (also with "/" in
// the date). Without a date the time is on defaultDate. None when the text
// is not a time. The result is a Timestamp as written, like the reader's.
std::optional<QDateTime> parseTimeInput( const QString& text, const QDate& defaultDate );

} // namespace timelookup
