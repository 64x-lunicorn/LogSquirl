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

#include <catch2/catch.hpp>

#include "updateschedule.h"

using logsquirl::versioncheck::isCheckDue;
using logsquirl::versioncheck::nextDeadlineAfterCheck;

namespace {

constexpr std::time_t Now = 1'790'000'000;
constexpr std::time_t Day = 24 * 3600;

} // namespace

SCENARIO( "The update check is due once its deadline has passed", "[versioncheck][schedule]" )
{
    GIVEN( "A deadline one second ago" )
    {
        THEN( "A stable check is due" )
        {
            REQUIRE( isCheckDue( Now, Now - 1, false ) );
        }
    }

    GIVEN( "A deadline that has never been set" )
    {
        THEN( "A stable check is due" )
        {
            REQUIRE( isCheckDue( Now, 0, false ) );
        }
    }

    GIVEN( "A deadline a day from now" )
    {
        THEN( "A stable check is not due" )
        {
            REQUIRE_FALSE( isCheckDue( Now, Now + Day, false ) );
        }
    }

    GIVEN( "A deadline of exactly now" )
    {
        THEN( "A stable check is not due yet: the deadline must lie in the past" )
        {
            REQUIRE_FALSE( isCheckDue( Now, Now, false ) );
        }
    }
}

SCENARIO( "A beta check runs regardless of the deadline", "[versioncheck][schedule]" )
{
    GIVEN( "A deadline a day from now" )
    {
        THEN( "A beta check is due" )
        {
            REQUIRE( isCheckDue( Now, Now + Day, true ) );
        }
    }

    GIVEN( "A deadline that has passed" )
    {
        THEN( "A beta check is due" )
        {
            REQUIRE( isCheckDue( Now, Now - 1, true ) );
        }
    }
}

SCENARIO( "The next check is due seven days after a check", "[versioncheck][schedule]" )
{
    WHEN( "The feed was downloaded" )
    {
        THEN( "The next deadline is seven days from now" )
        {
            REQUIRE( nextDeadlineAfterCheck( Now, true ) == Now + 7 * Day );
        }
    }

    WHEN( "The download failed" )
    {
        THEN( "The next deadline is seven days from now as well: a failed check is not retried "
              "sooner" )
        {
            REQUIRE( nextDeadlineAfterCheck( Now, false ) == Now + 7 * Day );
        }
    }
}
