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

#include "configuration.h"
#include "persistentinfo.h"

#include <QObject>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

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

SCENARIO( "A start counted by another LogSquirl keeps a dismissal it has not read",
          "[starrequest]" )
{
    GIVEN( "a request due, read by a second LogSquirl before the first one dismisses it" )
    {
        const ForgetsStarRequest forgets;
        auto& request = StarRequest::getSynced();
        request.setUse( usedFor( 30, 30 ) );
        request.save();

        StarRequest secondInstance;
        secondInstance.retrieveFromStorage( PersistentInfo::getSettings( app_settings{} ) );
        REQUIRE_FALSE( secondInstance.use().dismissed );

        WHEN( "the first dismisses it and then the second counts its start" )
        {
            StarRequest::dismissForGood();
            secondInstance.countStart( Today );

            THEN( "the dismissal stays, and the start is counted" )
            {
                const auto stored = StarRequest::getSynced().use();
                CHECK( stored.dismissed );
                CHECK( stored.starts == 31 );
                CHECK_FALSE( StarRequest::dueOn( Today ) );
            }
        }
    }
}

SCENARIO( "A first start after today, kept while the clock was wrong, becomes today",
          "[starrequest]" )
{
    GIVEN( "a first start stored years ahead of today" )
    {
        const ForgetsStarRequest forgets;
        auto& request = StarRequest::getSynced();
        request.setUse(
            StarRequestUse{ .firstStart = Today.addYears( 3 ), .starts = 40, .dismissed = false } );
        request.save();

        WHEN( "the application starts today" )
        {
            StarRequest::countApplicationStart( Today );

            THEN( "the first start is today, and the request is due two weeks later" )
            {
                const auto stored = StarRequest::getSynced().use();
                CHECK( stored.firstStart == Today );
                CHECK( stored.starts == 41 );
                CHECK_FALSE( StarRequest::dueOn( Today.addDays( 13 ) ) );
                CHECK( StarRequest::dueOn( Today.addDays( 14 ) ) );
            }
        }

        WHEN( "the application starts on a day before an earlier first start" )
        {
            request.setUse( usedFor( 5, 3 ) );
            request.save();
            StarRequest::countApplicationStart( Today );

            THEN( "a first start before today stays" )
            {
                CHECK( StarRequest::getSynced().use().firstStart == Today.addDays( -5 ) );
            }
        }
    }
}

SCENARIO( "A dismissal survives an update's settings migration and the saving of other settings",
          "[starrequest]" )
{
    GIVEN( "settings of an older version in which the request is dismissed" )
    {
        QTemporaryDir directory;
        REQUIRE( directory.isValid() );
        QSettings appSettings( directory.filePath( QStringLiteral( "logsquirl.conf" ) ),
                               QSettings::IniFormat );
        QSettings sessionSettings( directory.filePath( QStringLiteral( "logsquirl_session.conf" ) ),
                                   QSettings::IniFormat );
        // Version 0: every step of the migration runs.
        appSettings.setValue( QStringLiteral( "RecentFiles/files" ), QStringLiteral( "a.log" ) );
        appSettings.setValue( QStringLiteral( "geometry" ), QStringLiteral( "kept" ) );
        sessionSettings.setValue( QStringLiteral( "geometry" ), QStringLiteral( "kept" ) );

        StarRequest dismissed;
        dismissed.setUse(
            StarRequestUse{ .firstStart = Today.addDays( -40 ), .starts = 60, .dismissed = true } );
        dismissed.saveToStorage( appSettings );

        WHEN( "the settings are migrated" )
        {
            PersistentInfo::updateSettings( appSettings, sessionSettings );
            appSettings.sync();
            REQUIRE_FALSE( appSettings.contains( QStringLiteral( "geometry" ) ) );

            THEN( "the request stays dismissed, with its first start and its starts" )
            {
                QSettings reread( directory.filePath( QStringLiteral( "logsquirl.conf" ) ),
                                  QSettings::IniFormat );
                StarRequest migrated;
                migrated.retrieveFromStorage( reread );
                CHECK( migrated.use().dismissed );
                CHECK( migrated.use().firstStart == Today.addDays( -40 ) );
                CHECK( migrated.use().starts == 60 );
            }
        }
    }

    GIVEN( "a request dismissed in the settings store" )
    {
        const ForgetsStarRequest forgets;
        for ( int start = 0; start < 20; ++start ) {
            StarRequest::countApplicationStart( Today.addDays( start ) );
        }
        StarRequest::dismissForGood();

        WHEN( "other settings of the same store are saved" )
        {
            auto& configuration = Configuration::getSynced();
            configuration.save();

            THEN( "the request stays dismissed" )
            {
                const auto stored = StarRequest::getSynced().use();
                CHECK( stored.dismissed );
                CHECK( stored.starts == 20 );
                CHECK_FALSE( StarRequest::dueOn( Today.addDays( 100 ) ) );
            }
        }
    }
}

SCENARIO( "Dismissing the request tells everyone who listens", "[starrequest]" )
{
    GIVEN( "two listeners, one of them gone before the dismissal" )
    {
        const ForgetsStarRequest forgets;
        QObject listener;
        auto gone = std::make_unique<QObject>();
        int hidden = 0;
        int hiddenGone = 0;
        StarRequest::whenDismissed( &listener, [ &hidden ] { ++hidden; } );
        StarRequest::whenDismissed( gone.get(), [ &hiddenGone ] { ++hiddenGone; } );
        gone.reset();

        WHEN( "the request is dismissed for good" )
        {
            StarRequest::dismissForGood();

            THEN( "the listener that still exists hides, once" )
            {
                CHECK( hidden == 1 );
                CHECK( hiddenGone == 0 );
                CHECK( StarRequest::getSynced().use().dismissed );
            }
        }
    }
}
