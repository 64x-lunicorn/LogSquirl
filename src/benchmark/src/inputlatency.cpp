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

#include "inputlatency.h"

#include <chrono>

namespace logsquirl::benchmark {

void InputLatency::inputHandled( Clock::time_point moment )
{
    waiting_.push_back( moment );
}

bool InputLatency::waiting() const
{
    return !waiting_.empty();
}

std::vector<double> InputLatency::painted( Clock::time_point started, Clock::time_point ended )
{
    std::vector<double> answered;
    // The inputs wait in the order they were handled.
    while ( !waiting_.empty() && waiting_.front() < started ) {
        answered.push_back( millisecondsBetween( waiting_.front(), ended ) );
        waiting_.pop_front();
    }
    latencies_.insert( latencies_.end(), answered.begin(), answered.end() );
    return answered;
}

const std::vector<double>& InputLatency::latencies() const
{
    return latencies_;
}

Distribution InputLatency::distribution() const
{
    return Distribution::of( latencies_ );
}

} // namespace logsquirl::benchmark
