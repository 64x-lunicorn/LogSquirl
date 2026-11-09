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

// The project's website, the `site` of website/astro.config.mjs.
inline constexpr auto WebsiteUrl = "https://logsquirl.lunicorn-lab.de";

// The month a version belongs to, YY.MM: "26.12" for the build 26.12.0.1638
// and for the release 26.12.0-beta2. A version without one is returned as it
// is. Help > What's New is named with it (#932).
QString versionMonth( const QString& version );

// The page Help > What's New opens for a build running version (#932): the
// website's release page of its month, news/release-YY-MM/, which is
// deployed with that month's first release. A version without a month, and a
// local build (build number 0), whose month may have no release yet, open the
// overview of the releases on GitHub instead.
QString releaseNotesUrl( const QString& version );

} // namespace logsquirl::versioncheck
