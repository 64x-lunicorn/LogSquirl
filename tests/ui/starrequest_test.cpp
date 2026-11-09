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

#include "starrequest.h"

#include <catch2/catch_test_macros.hpp>

// The Welcome Dashboard asks for a star on GitHub once LogSquirl has been used
// for a while, until the user is done with it (#934).

namespace {

const QDate Today{ 2026, 11, 9 };

StarRequestUse usedFor( qint64 days, int starts )
{
    return StarRequestUse{ .firstStart = Today.addDays( -days ),
                           .starts = starts,
                           .dismissed = false };
}

// Stores nothing of the request when it ends, as a fresh installation has.
class ForgetsStarRequest {
public:
    ForgetsStarRequest()
    {
        forget();
    }
    ~ForgetsStarRequest()
    {
        forget();
    }
    ForgetsStarRequest( const ForgetsStarRequest& ) = delete;
    ForgetsStarRequest& operator=( const ForgetsStarRequest& ) = delete;
    ForgetsStarRequest( ForgetsStarRequest&& ) = delete;
    ForgetsStarRequest& operator=( ForgetsStarRequest&& ) = delete;

private:
    static void forget()
    {
        auto& request = StarRequest::getSynced();
        request.setUse( StarRequestUse{} );
        request.save();
    }
};

} // namespace

SCENARIO( "The star request is due after two weeks and twenty starts", "[starrequest]" )
{
    GIVEN( "a use of LogSquirl at the thresholds" )
    {
        THEN( "it is due when both are reached" )
        {
            CHECK( starRequestDue( usedFor( 14, 20 ), Today ) );
            CHECK( starRequestDue( usedFor( 15, 21 ), Today ) );
            CHECK( starRequestDue( usedFor( 400, 1000 ), Today ) );
        }

        THEN( "it is not due a day or a start before" )
        {
            CHECK_FALSE( starRequestDue( usedFor( 13, 20 ), Today ) );
            CHECK_FALSE( starRequestDue( usedFor( 14, 19 ), Today ) );
            CHECK_FALSE( starRequestDue( usedFor( 13, 1000 ), Today ) );
            CHECK_FALSE( starRequestDue( usedFor( 400, 19 ), Today ) );
            CHECK_FALSE( starRequestDue( usedFor( 0, 0 ), Today ) );
        }

        THEN( "it is not due without a first start, or with one after today" )
        {
            CHECK_FALSE( starRequestDue(
                StarRequestUse{ .firstStart = {}, .starts = 1000, .dismissed = false }, Today ) );
            CHECK_FALSE( starRequestDue( usedFor( -20, 1000 ), Today ) );
        }

        THEN( "it is never due once dismissed" )
        {
            for ( const auto& use : { usedFor( 14, 20 ), usedFor( 400, 1000 ) } ) {
                auto dismissed = use;
                dismissed.dismissed = true;
                CHECK_FALSE( starRequestDue( dismissed, Today ) );
            }
        }
    }
}

SCENARIO( "Each start of the application is counted in the settings", "[starrequest]" )
{
    GIVEN( "a fresh installation" )
    {
        const ForgetsStarRequest forgets;
        REQUIRE( StarRequest::getSynced().use().starts == 0 );
        REQUIRE_FALSE( StarRequest::getSynced().use().firstStart.isValid() );

        WHEN( "the application starts" )
        {
            StarRequest::countApplicationStart( Today );

            THEN( "the start is counted once and its day is the first start" )
            {
                const auto stored = StarRequest::getSynced().use();
                CHECK( stored.starts == 1 );
                CHECK( stored.firstStart == Today );
                CHECK_FALSE( stored.dismissed );
            }

            AND_WHEN( "it starts again on later days" )
            {
                StarRequest::countApplicationStart( Today.addDays( 3 ) );
                StarRequest::countApplicationStart( Today.addDays( 9 ) );

                THEN( "each start is counted and the first start stays" )
                {
                    const auto stored = StarRequest::getSynced().use();
                    CHECK( stored.starts == 3 );
                    CHECK( stored.firstStart == Today );
                }
            }
        }

        WHEN( "it has started twenty times over two weeks" )
        {
            for ( int start = 0; start < 20; ++start ) {
                StarRequest::countApplicationStart( Today.addDays( start ) );
            }

            THEN( "the request is due from the fourteenth day on" )
            {
                CHECK_FALSE( StarRequest::dueOn( Today.addDays( 13 ) ) );
                CHECK( StarRequest::dueOn( Today.addDays( 14 ) ) );
            }

            AND_WHEN( "it is dismissed" )
            {
                StarRequest::dismissForGood();

                THEN( "it is kept as dismissed and is not due again, however many starts follow" )
                {
                    CHECK( StarRequest::getSynced().use().dismissed );
                    for ( int start = 0; start < 30; ++start ) {
                        StarRequest::countApplicationStart( Today.addDays( 20 + start ) );
                    }
                    const auto stored = StarRequest::getSynced().use();
                    CHECK( stored.dismissed );
                    CHECK( stored.starts == 50 );
                    CHECK_FALSE( StarRequest::dueOn( Today.addDays( 400 ) ) );
                }
            }
        }
    }
}
