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

#include <QString>

namespace logsquirl::versioncheck {

// The project's repository on GitHub, which Help > LogSquirl on GitHub opens
// (#932).
inline constexpr auto ProjectPageUrl = "https://github.com/64x-lunicorn/LogSquirl";

// Every release page of LogSquirl lies below this (#222, ADR 0005).
inline constexpr auto ReleasePagesPrefix = "https://github.com/64x-lunicorn/LogSquirl/releases/";

// The page with every release, newest first.
inline constexpr auto ReleasesOverviewUrl = "https://github.com/64x-lunicorn/LogSquirl/releases";

// The page Help > What's New opens for a build running version (#932): the
// release page of its tag, v<version>, when version is the name of a
// release -- "26.10.0", "26.12.0-beta2", or the older "26.03.1-beta.2".
// Any other version, a build YY.MM.PATCH.BUILD among them, names no tag, and
// opens the overview of the releases.
QString releaseNotesUrl( const QString& version );

} // namespace logsquirl::versioncheck
