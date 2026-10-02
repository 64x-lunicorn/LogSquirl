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

#include <deque>
#include <vector>

#include "benchmarkreport.h"
#include "processclock.h"

namespace logsquirl::benchmark {

// How long input takes to be answered on screen (#668): from the moment an
// input was handled -- a keystroke handed to the QuickFind bar -- to the end of
// the first paint of the view that started after it. That paint shows what
// the input changed; a paint already under way when the input came, or one
// before it, shows what was there before and answers nothing.
//
// The paints are those a PaintProbe times; nothing here reads a clock.
class InputLatency {
public:
    // An input was handled at moment; it waits for the next paint that starts
    // after it.
    void inputHandled( Clock::time_point moment );

    // Whether an input waits for its paint.
    bool waiting() const;

    // A paint of the view, from started to ended. Returns the latency of each
    // input it answered, in milliseconds, oldest first: every waiting input
    // handled before the paint started.
    std::vector<double> painted( Clock::time_point started, Clock::time_point ended );

    // The latency of every input answered so far, in milliseconds, in the
    // order they were answered.
    const std::vector<double>& latencies() const;
    Distribution distribution() const;

private:
    std::deque<Clock::time_point> waiting_;
    std::vector<double> latencies_;
};

} // namespace logsquirl::benchmark
