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

#include "versionchecker.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

namespace {

// Settings held in memory, so the check reads and writes no settings store.
struct HeldSettings {
    bool betaCheckingEnabled = false;
    std::time_t nextDeadline = 0;
    int deadlineWrites = 0;

    UpdateCheckSettings settings()
    {
        return UpdateCheckSettings{ [] { return true; }, [ this ] { return betaCheckingEnabled; },
                                    [ this ] { return nextDeadline; },
                                    [ this ]( std::time_t deadline ) {
                                        nextDeadline = deadline;
                                        ++deadlineWrites;
                                    } };
    }
};

// A feed offering a stable release from a build no running version reaches.
QByteArray feedOfferingARelease()
{
    return QByteArrayLiteral(
        R"({"stable":"99.12.0","stable_url":"https://github.com/64x-lunicorn/LogSquirl/releases/tag/v99.12.0",)"
        R"("stable_build":"99.12.0.99999","releases":["99.12.0"],)"
        R"("changelog":[{"version":"99.12.0","description":"Notes"}]})" );
}

// Runs the event loop until condition holds or a few seconds have passed,
// then a little longer so that a reply handled twice would show.
template <typename Condition>
void processEventsUntil( Condition condition )
{
    QDeadlineTimer deadline( 5000 );
    while ( !condition() && !deadline.hasExpired() ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }
    QDeadlineTimer settle( 200 );
    while ( !settle.hasExpired() ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }
}

} // namespace

SCENARIO( "Starting the update check twice handles each downloaded reply once",
          "[versioncheck][versionchecker]" )
{
    GIVEN( "A feed served from a file and a beta check, which is due on every start" )
    {
        QTemporaryDir directory;
        REQUIRE( directory.isValid() );
        QFile feedFile( directory.filePath( "latest.json" ) );
        REQUIRE( feedFile.open( QIODevice::WriteOnly ) );
        feedFile.write( feedOfferingARelease() );
        feedFile.close();

        HeldSettings held;
        held.betaCheckingEnabled = true;
        VersionChecker checker( held.settings(), QUrl::fromLocalFile( feedFile.fileName() ) );

        int announcements = 0;
        QObject::connect( &checker, &VersionChecker::newVersionFound,
                          [ &announcements ] { ++announcements; } );

        WHEN( "The check is started twice" )
        {
            checker.startCheck();
            checker.startCheck();
            processEventsUntil( [ &held ] { return held.deadlineWrites >= 2; } );

            THEN( "Each of the two replies moves the deadline once and announces the offer once" )
            {
                REQUIRE( held.deadlineWrites == 2 );
                REQUIRE( announcements == 2 );
            }
        }
    }
}

SCENARIO( "The update check downloads nothing before its deadline",
          "[versioncheck][versionchecker]" )
{
    GIVEN( "A stable check whose deadline lies a day ahead" )
    {
        HeldSettings held;
        held.nextDeadline = std::time( nullptr ) + 24 * 3600;
        const auto deadline = held.nextDeadline;
        VersionChecker checker( held.settings(),
                                QUrl::fromLocalFile( "/nonexistent/latest.json" ) );

        WHEN( "The check is started" )
        {
            checker.startCheck();
            processEventsUntil( [] { return false; } );

            THEN( "No reply is handled and the deadline stays" )
            {
                REQUIRE( held.deadlineWrites == 0 );
                REQUIRE( held.nextDeadline == deadline );
            }
        }
    }
}

SCENARIO( "A failed download moves the deadline as a successful one does",
          "[versioncheck][versionchecker]" )
{
    GIVEN( "A due stable check whose feed cannot be read" )
    {
        HeldSettings held;
        VersionChecker checker( held.settings(),
                                QUrl::fromLocalFile( "/nonexistent/latest.json" ) );

        int announcements = 0;
        QObject::connect( &checker, &VersionChecker::newVersionFound,
                          [ &announcements ] { ++announcements; } );

        WHEN( "The check is started" )
        {
            const auto started = std::time( nullptr );
            checker.startCheck();
            processEventsUntil( [ &held ] { return held.deadlineWrites >= 1; } );

            THEN( "The deadline moves seven days on and nothing is announced" )
            {
                REQUIRE( held.deadlineWrites == 1 );
                REQUIRE( held.nextDeadline >= started + 7 * 24 * 3600 );
                REQUIRE( announcements == 0 );
            }
        }
    }
}
