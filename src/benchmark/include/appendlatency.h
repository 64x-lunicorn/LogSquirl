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

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "benchmarkreport.h"
#include "processclock.h"

namespace logsquirl::benchmark {

// How long a Log Line appended to a growing Log File takes to be shown (#670):
// from the moment it was appended to the end of the first paint that shows
// the Log File through it -- the Text View following the end, or a chart
// whose points reach it. A view that follows the end may scroll past a Log
// Line between two paints: it was shown by the paint that shows a later one.
//
// The appends are told in the order of their Log Lines; the paints are those
// a PaintProbe times. Nothing here reads a clock, and nothing is shared
// between threads: the writer's moments are handed over by the scenario.
class AppendLatency {
public:
    // Log Line `line` -- its number in the Log File, from 0 -- was appended
    // at `moment`. It waits for a paint that shows it.
    void appended( std::uint64_t line, Clock::time_point moment );

    // The Log Lines appended and not shown yet.
    std::size_t waitingCount() const;

    // A paint that ended at `ended` showed the Log File through Log Line
    // `through`. Returns the latency of each appended Log Line it showed
    // first, in milliseconds, oldest first.
    std::vector<double> shown( std::uint64_t through, Clock::time_point ended );

    // The last appended Log Line shown, if any.
    std::optional<std::uint64_t> lastShown() const;

    // The latency of every Log Line shown so far, in milliseconds, in the
    // order they were appended.
    const std::vector<double>& latencies() const;
    Distribution distribution() const;

private:
    struct Append {
        std::uint64_t line;
        Clock::time_point moment;
    };

    std::deque<Append> waiting_;
    std::vector<double> latencies_;
    std::optional<std::uint64_t> lastShown_;
};

} // namespace logsquirl::benchmark
