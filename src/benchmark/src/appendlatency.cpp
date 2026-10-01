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

#include "appendlatency.h"

#include <chrono>

namespace logsquirl::benchmark {

void AppendLatency::appended( std::uint64_t line, Clock::time_point moment )
{
    waiting_.push_back( { line, moment } );
}

std::size_t AppendLatency::waitingCount() const
{
    return waiting_.size();
}

std::vector<double> AppendLatency::shown( std::uint64_t through, Clock::time_point ended )
{
    std::vector<double> shownFirst;
    // The appends wait in the order of their Log Lines.
    while ( !waiting_.empty() && waiting_.front().line <= through ) {
        shownFirst.push_back(
            std::chrono::duration<double, std::milli>( ended - waiting_.front().moment ).count() );
        lastShown_ = waiting_.front().line;
        waiting_.pop_front();
    }
    latencies_.insert( latencies_.end(), shownFirst.begin(), shownFirst.end() );
    return shownFirst;
}

std::optional<std::uint64_t> AppendLatency::lastShown() const
{
    return lastShown_;
}

const std::vector<double>& AppendLatency::latencies() const
{
    return latencies_;
}

Distribution AppendLatency::distribution() const
{
    return Distribution::of( latencies_ );
}

} // namespace logsquirl::benchmark
