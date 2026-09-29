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

// The Open Command Output dialog offers the recent commands; choosing one
// fills in its command line, working folder and standard error choice (#575).

#include <catch2/catch_test_macros.hpp>

#include "commandoutputdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>

SCENARIO( "The Open Command Output dialog fills in a recent command", "[commandsource][dialog]" )
{
    GIVEN( "No command run yet" )
    {
        CommandOutputDialog dialog( {} );

        THEN( "It opens empty, with standard error included, and cannot open anything" )
        {
            REQUIRE( dialog.commandBox()->currentText().isEmpty() );
            REQUIRE( dialog.workingFolderEdit()->text().isEmpty() );
            REQUIRE( dialog.standardErrorBox()->isChecked() );
            REQUIRE_FALSE( dialog.openButton()->isEnabled() );
        }

        WHEN( "A command line is typed" )
        {
            dialog.commandBox()->setEditText( "  journalctl -f  " );

            THEN( "It can be opened, trimmed, in the home folder" )
            {
                REQUIRE( dialog.openButton()->isEnabled() );
                REQUIRE( dialog.command() == RecentCommand{ "journalctl -f", QString{}, true } );
            }
        }
    }

    GIVEN( "Two recent commands" )
    {
        const RecentCommand latest{ "docker logs -f web", QDir::tempPath(), true };
        const RecentCommand older{ "tail -f app.log", QDir::homePath(), false };
        CommandOutputDialog dialog( { latest, older } );

        THEN( "They are offered, the most recent first, which the dialog opens with" )
        {
            REQUIRE( dialog.commandBox()->count() == 2 );
            REQUIRE( dialog.commandBox()->itemText( 0 ) == latest.commandLine );
            REQUIRE( dialog.commandBox()->itemText( 1 ) == older.commandLine );
            REQUIRE( dialog.command() == latest );
            REQUIRE( dialog.openButton()->isEnabled() );
        }

        WHEN( "The older one is chosen" )
        {
            dialog.commandBox()->setCurrentIndex( 1 );
            Q_EMIT dialog.commandBox()->activated( 1 );

            THEN( "All three fields are filled in from it" )
            {
                REQUIRE( dialog.command() == older );
                REQUIRE( dialog.workingFolderEdit()->text()
                         == QDir::toNativeSeparators( older.workingFolder ) );
                REQUIRE_FALSE( dialog.standardErrorBox()->isChecked() );
            }
        }
    }
}
