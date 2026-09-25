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

#include "installoptout.h"

#include <QFile>
#include <QTemporaryDir>

using namespace logsquirl::versioncheck;

SCENARIO( "The installer can turn the update check off for an installation", "[versioncheck]" )
{
    GIVEN( "An installation directory" )
    {
        QTemporaryDir installation;
        REQUIRE( installation.isValid() );

        THEN( "The update check is on while the installer left no file there" )
        {
            CHECK_FALSE( updateCheckTurnedOffAtInstall( installation.path() ) );
        }

        WHEN( "The installer leaves the file beside the executable" )
        {
            QFile marker( installation.filePath( UpdateCheckOffFileName ) );
            REQUIRE( marker.open( QIODevice::WriteOnly ) );
            marker.close();

            THEN( "The update check is off, although the file is empty" )
            {
                CHECK( updateCheckTurnedOffAtInstall( installation.path() ) );
            }

            AND_WHEN( "A later install, with the component ticked, deletes it" )
            {
                REQUIRE( QFile::remove( marker.fileName() ) );

                THEN( "The update check is on again" )
                {
                    CHECK_FALSE( updateCheckTurnedOffAtInstall( installation.path() ) );
                }
            }
        }
    }
}
