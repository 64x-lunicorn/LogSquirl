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

#include <chrono>
#include <cstdint>
#include <optional>

namespace logsquirl::benchmark {

// Every time a benchmark run reports is read from this clock (#666).
using Clock = std::chrono::steady_clock;

// How long ago the operating system started this process, from the start time
// it keeps for the process: on Linux in clock ticks (usually 10 ms), on macOS
// in microseconds, on Windows in 100 ns. None where it cannot say.
std::optional<std::chrono::nanoseconds> timeSinceProcessStart();

// The most memory this process has had resident at once so far, in bytes: the
// peak resident set size on Linux and macOS, the peak working set on Windows.
// None where the platform does not tell.
std::optional<std::int64_t> peakResidentBytes();

// Turns a moment of this run into milliseconds since the process started.
//
// main() reads Clock::now() as the first thing it does and hands it here; how
// long before that the process started is asked of the operating system once.
// Every later moment is measured with the steady clock from main(), so only
// the part before main() has the operating system's resolution.
class ProcessClock {
public:
    // Asks the operating system when this process started. Where it cannot
    // say, the times count from mainEntered instead.
    static ProcessClock measure( Clock::time_point mainEntered );

    // mainEnteredSinceProcessStart is how long after the process started
    // main() was entered; none counts from mainEntered.
    ProcessClock( Clock::time_point mainEntered,
                  std::optional<std::chrono::nanoseconds> mainEnteredSinceProcessStart );

    double millisecondsSinceProcessStart( Clock::time_point moment ) const;

    // When main() was entered, in milliseconds since the process started: 0
    // when the process start is not known.
    double mainEnteredMilliseconds() const;

    // Whether the times count from the process start the operating system
    // reported, rather than from main().
    bool knowsProcessStart() const;

private:
    Clock::time_point mainEntered_;
    std::chrono::nanoseconds mainEnteredSinceProcessStart_;
    bool knowsProcessStart_;
};

} // namespace logsquirl::benchmark
