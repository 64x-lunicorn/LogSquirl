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
#include <optional>

#include <QJsonObject>

#include "processclock.h"

namespace logsquirl::benchmark {

// The CPU time this process has spent so far, user and system, every thread
// of it together (#686): CLOCK_PROCESS_CPUTIME_ID on Linux and macOS, the
// kernel and user times of GetProcessTimes on Windows (counted in 100 ns, but
// advanced only at the scheduler's tick, about 15.6 ms). None where the
// platform does not tell.
std::optional<std::chrono::nanoseconds> processCpuTime();

// How much of the machine a stretch of a run kept busy (#686): its wall time,
// and the CPU time the process spent in it, every thread together. Their
// ratio, the parallelism, is how many cores were busy on average: work done
// on one thread is about 1, work spread over four cores about 4.
//
// The CPU time is the whole process's: in a stretch of indexing it also has
// the UI thread's, which reads and paints meanwhile.
struct ProcessWork {
    double wallMs = 0.0;
    // None when the CPU time is not known at either end.
    std::optional<double> cpuMs;

    // The stretch from started to ended, with the process's CPU time read at
    // each (processCpuTime()).
    static ProcessWork between( Clock::time_point started, Clock::time_point ended,
                                std::optional<std::chrono::nanoseconds> cpuStarted,
                                std::optional<std::chrono::nanoseconds> cpuEnded );

    // cpuMs over wallMs; none when either is unknown or no wall time passed.
    std::optional<double> parallelism() const;

    // {"wall_ms", "cpu_ms", "parallelism"}; the last two only when known.
    QJsonObject toJson() const;

    // Both clocks, read one right after the other.
    struct Moment {
        Clock::time_point wall;
        std::optional<std::chrono::nanoseconds> cpu;

        static Moment now();
        // The stretch from this moment to a later one.
        ProcessWork until( const Moment& ended ) const;
    };
};

} // namespace logsquirl::benchmark
