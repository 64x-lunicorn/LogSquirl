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
using logsquirl::versioncheck::versionMonth;

namespace {

const auto Overview = QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases" );

QString monthPage( const QString& slug )
{
    return QStringLiteral( "https://logsquirl.lunicorn-lab.de/news/release-%1/" ).arg( slug );
}

} // namespace

// Help > What's New opens the website's release page of the running version's
// month (#932).
TEST_CASE( "What's New opens the release page of the version's month", "[releasepages]" )
{
    SECTION( "a CI build opens the page of its month" )
    {
        CHECK( releaseNotesUrl( "26.12.0.1638" ) == monthPage( "26-12" ) );
        CHECK( releaseNotesUrl( "26.10.0.1364" ) == monthPage( "26-10" ) );
        CHECK( releaseNotesUrl( "26.10.1.1400" ) == monthPage( "26-10" ) );
    }

    SECTION( "a release's name opens the page of its month" )
    {
        CHECK( releaseNotesUrl( "26.10.0" ) == monthPage( "26-10" ) );
        CHECK( releaseNotesUrl( "26.12.0-beta2" ) == monthPage( "26-12" ) );
        CHECK( releaseNotesUrl( "26.03.1-beta.2" ) == monthPage( "26-03" ) );
        CHECK( releaseNotesUrl( "27.01.0-rc1" ) == monthPage( "27-01" ) );
    }

    SECTION( "a one-digit month is written with two" )
    {
        CHECK( releaseNotesUrl( "26.7.0.741" ) == monthPage( "26-07" ) );
    }

    SECTION( "a local build, build number 0, opens the overview on GitHub" )
    {
        CHECK( releaseNotesUrl( "26.12.0.0" ) == Overview );
    }

    SECTION( "a version without a month opens the overview on GitHub" )
    {
        CHECK( releaseNotesUrl( "" ) == Overview );
        CHECK( releaseNotesUrl( "dev" ) == Overview );
        CHECK( releaseNotesUrl( "26.12" ) == Overview );
        CHECK( releaseNotesUrl( "26.12.0-dirty" ) == Overview );
        CHECK( releaseNotesUrl( "26.12.0-beta2/../../x" ) == Overview );
    }
}

TEST_CASE( "What's New is named with the version's month", "[releasepages]" )
{
    CHECK( versionMonth( "26.12.0.1638" ) == "26.12" );
    CHECK( versionMonth( "26.12.0.0" ) == "26.12" );
    CHECK( versionMonth( "26.12.0-beta2" ) == "26.12" );
    CHECK( versionMonth( "26.7.0.741" ) == "26.07" );
    CHECK( versionMonth( "dev" ) == "dev" );
}

TEST_CASE( "A month that is not 1 to 12 is no month", "[releasepages]" )
{
    SECTION( "the first and the last month are months" )
    {
        CHECK( versionMonth( "26.1.0.5" ) == "26.01" );
        CHECK( versionMonth( "26.01.0.5" ) == "26.01" );
        CHECK( versionMonth( "26.12.0.5" ) == "26.12" );
        CHECK( releaseNotesUrl( "26.1.0.5" ) == monthPage( "26-01" ) );
        CHECK( releaseNotesUrl( "26.12.0-beta1" ) == monthPage( "26-12" ) );
    }

    SECTION( "month 0 and months from 13 on name no month and open the overview" )
    {
        for ( const auto* version :
              { "26.0.0.5", "26.00.0.5", "26.13.0.5", "26.20.0-beta1", "26.99.0" } ) {
            INFO( version );
            CHECK( versionMonth( version ) == QString( version ) );
            CHECK( releaseNotesUrl( version ) == Overview );
        }
    }
}

TEST_CASE( "The pages of the repository lie below its root", "[releasepages]" )
{
    using namespace logsquirl::versioncheck;
    CHECK( QString( ProjectPageUrl ) == "https://github.com/64x-lunicorn/LogSquirl" );
    CHECK( QString( ReleasesOverviewUrl ) == Overview );
    CHECK( QString( ReleasePagesPrefix ) == Overview + u'/' );
}
