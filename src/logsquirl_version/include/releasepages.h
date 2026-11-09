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

#include <array>
#include <cstddef>

namespace logsquirl::versioncheck {

namespace detail {

// The texts one after the other, ending in one NUL: a constant made of others.
template <std::size_t... Sizes>
constexpr auto joined( const char ( &... texts )[ Sizes ] )
{
    std::array<char, ( Sizes + ... ) - sizeof...( Sizes ) + 1> result{};
    std::size_t at = 0;
    const auto append = [ &result, &at ]( const char* text, std::size_t size ) {
        for ( std::size_t i = 0; i + 1 < size; ++i ) {
            result[ at++ ] = text[ i ];
        }
    };
    ( append( texts, Sizes ), ... );
    return result;
}

// The project's repository on GitHub, which every page of it below lies in.
inline constexpr char RepositoryRoot[] = "https://github.com/64x-lunicorn/LogSquirl";

inline constexpr auto ReleasesOverview = joined( RepositoryRoot, "/releases" );
inline constexpr auto ReleasePages = joined( RepositoryRoot, "/releases/" );

} // namespace detail

// The project's repository on GitHub, which Help > LogSquirl on GitHub opens
// (#932).
inline constexpr const char* ProjectPageUrl = detail::RepositoryRoot;

// Every release page of LogSquirl lies below this (#222, ADR 0005).
inline constexpr const char* ReleasePagesPrefix = detail::ReleasePages.data();

// The page with every release, newest first.
inline constexpr const char* ReleasesOverviewUrl = detail::ReleasesOverview.data();

// The project's website, the `site` of website/astro.config.mjs.
inline constexpr auto WebsiteUrl = "https://logsquirl.lunicorn-lab.de";

// The month a version belongs to, YY.MM: "26.12" for the build 26.12.0.1638
// and for the release 26.12.0-beta2. A version without one, or with a month
// that is not 1 to 12, is returned as it is. Help > What's New is named with
// it (#932).
QString versionMonth( const QString& version );

// The month of the running version: versionMonth( logsquirlVersion() ).
QString runningVersionMonth();

// The page Help > What's New opens for a build running version (#932): the
// website's release page of its month, news/release-YY-MM/, which is
// deployed with that month's first release. A version without a month, and a
// local build (build number 0), whose month may have no release yet, open the
// overview of the releases on GitHub instead.
QString releaseNotesUrl( const QString& version );

// The page Help > What's New opens: releaseNotesUrl( logsquirlVersion() ).
QString runningReleaseNotesUrl();

} // namespace logsquirl::versioncheck
