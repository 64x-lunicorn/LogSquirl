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

#include "updateschedule.h"

namespace logsquirl::versioncheck {

namespace {

constexpr std::time_t CheckIntervalS = 3600 * 24 * 7; /* 7 days */

} // namespace

bool isCheckDue( std::time_t now, std::time_t nextDeadline, bool betaCheckingEnabled )
{
    return nextDeadline < now || betaCheckingEnabled;
}

std::time_t nextDeadlineAfterCheck( std::time_t now, bool /*downloadSucceeded*/ )
{
    return now + CheckIntervalS;
}

} // namespace logsquirl::versioncheck
