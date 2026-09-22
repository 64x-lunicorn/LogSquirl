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

#include <ctime>

namespace logsquirl::versioncheck {

// When the update check downloads the feed (#389). Both are decided from the
// current time, the stored deadline and the user's "check for beta versions"
// option alone, without a network or a settings store.

// Whether a check started at now downloads the feed: once the deadline lies in
// the past, or on every start while beta checking is enabled.
bool isCheckDue( std::time_t now, std::time_t nextDeadline, bool betaCheckingEnabled );

// The deadline a check finished at now leaves for the next one: seven days on,
// whether the download succeeded or failed, so a failed check is not retried
// before then.
std::time_t nextDeadlineAfterCheck( std::time_t now, bool downloadSucceeded );

} // namespace logsquirl::versioncheck
