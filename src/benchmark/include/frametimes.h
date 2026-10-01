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

#include <cstddef>
#include <vector>

#include <QJsonObject>

#include "benchmarkreport.h"
#include "processclock.h"

namespace logsquirl::benchmark {

// The frames of a view while it scrolls (#669): the duration of every paint
// of its Viewport, from the moment the paint event reached the Viewport to
// the moment its handler returned, as a PaintProbe times it. A frame longer
// than the budget is one a display refreshing at 60 Hz shows late.
//
// The paints are those a PaintProbe times; nothing here reads a clock.
class FrameTimes {
public:
    // One frame at 60 Hz, 16.7 ms.
    static constexpr double DefaultBudgetMs = 1000.0 / 60.0;

    explicit FrameTimes( double budgetMs = DefaultBudgetMs );

    // A paint of the view, from started to ended.
    void painted( Clock::time_point started, Clock::time_point ended );

    double budgetMs() const;
    std::size_t count() const;
    // The frames longer than the budget; one exactly as long fits in it.
    std::size_t overBudget() const;
    Distribution distribution() const;

    // Distribution::toJson() of the frames, with "budget_ms" and
    // "over_budget_count".
    QJsonObject toJson() const;

private:
    double budgetMs_;
    std::vector<double> milliseconds_;
};

} // namespace logsquirl::benchmark
