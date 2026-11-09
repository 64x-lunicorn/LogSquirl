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

#include <catch2/catch_test_macros.hpp>

#include "releasepages.h"

using logsquirl::versioncheck::releaseNotesUrl;

namespace {

const auto Overview = QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases" );

QString tagPage( const QString& tag )
{
    return QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases/tag/" ) + tag;
}

} // namespace

// Help > What's New opens the release notes of the running version (#932).
TEST_CASE( "What's New opens the release page of a release and the overview otherwise",
           "[releasepages]" )
{
    SECTION( "a stable release opens the page of its tag" )
    {
        CHECK( releaseNotesUrl( "26.10.0" ) == tagPage( "v26.10.0" ) );
    }

    SECTION( "a beta opens the page of its tag" )
    {
        CHECK( releaseNotesUrl( "26.12.0-beta2" ) == tagPage( "v26.12.0-beta2" ) );
        CHECK( releaseNotesUrl( "26.03.1-beta.2" ) == tagPage( "v26.03.1-beta.2" ) );
        CHECK( releaseNotesUrl( "27.01.0-rc1" ) == tagPage( "v27.01.0-rc1" ) );
    }

    SECTION( "a build without a tag of its own opens the overview" )
    {
        CHECK( releaseNotesUrl( "26.12.0.0" ) == Overview );
        CHECK( releaseNotesUrl( "26.12.0.1638" ) == Overview );
        CHECK( releaseNotesUrl( "" ) == Overview );
        CHECK( releaseNotesUrl( "26.12.0-dirty" ) == Overview );
        CHECK( releaseNotesUrl( "26.12.0-beta2/../../x" ) == Overview );
    }
}
