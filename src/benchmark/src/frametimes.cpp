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

#include "frametimes.h"

#include <algorithm>
#include <chrono>

namespace logsquirl::benchmark {

FrameTimes::FrameTimes( double budgetMs )
    : budgetMs_( budgetMs )
{
}

void FrameTimes::painted( Clock::time_point started, Clock::time_point ended )
{
    milliseconds_.push_back( std::chrono::duration<double, std::milli>( ended - started ).count() );
}

double FrameTimes::budgetMs() const
{
    return budgetMs_;
}

std::size_t FrameTimes::count() const
{
    return milliseconds_.size();
}

std::size_t FrameTimes::overBudget() const
{
    return static_cast<std::size_t>(
        std::count_if( milliseconds_.begin(), milliseconds_.end(),
                       [ this ]( double milliseconds ) { return milliseconds > budgetMs_; } ) );
}

Distribution FrameTimes::distribution() const
{
    return Distribution::of( milliseconds_ );
}

QJsonObject FrameTimes::toJson() const
{
    auto json = distribution().toJson();
    json.insert( "budget_ms", budgetMs_ );
    json.insert( "over_budget_count", static_cast<qint64>( overBudget() ) );
    return json;
}

} // namespace logsquirl::benchmark
