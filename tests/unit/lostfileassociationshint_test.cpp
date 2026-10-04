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

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include "fake_file_associations.h"
#include "fileassociationchoice.h"
#include "lostfileassociationshint.h"

TEST_CASE( "The hint names the chosen file types LogSquirl no longer opens",
           "[fileassociations][lostassociation]" )
{
    FakeFileAssociations associations;

    SECTION( "taken over by another application" )
    {
        LostFileAssociationsHint hint( associations, { "log" }, {} );
        CHECK( hint.textLabel()->text() == "LogSquirl no longer opens .log files." );
        CHECK( hint.restoreButton()->text() == "Restore" );
        CHECK( hint.dismissButton()->text() == "Dismiss" );
    }

    SECTION( "several" )
    {
        LostFileAssociationsHint hint( associations, { "log", "logcat" }, {} );
        CHECK( hint.textLabel()->text()
               == "LogSquirl no longer opens .log, .adb, .adb0-.adb9 files." );
    }

    SECTION( "a moved portable LogSquirl says the associations point at the old location" )
    {
        LostFileAssociationsHint hint( associations, { "log" }, "D:/Old/logsquirl_portable.exe" );
        CHECK( hint.textLabel()->text().contains( "point at the old location of LogSquirl" ) );
        CHECK( hint.textLabel()->text().contains( "logsquirl_portable.exe" ) );
    }
}

TEST_CASE( "Restore makes LogSquirl the default again, Dismiss stops the hint for that loss",
           "[fileassociations][lostassociation]" )
{
    FakeFileAssociations associations;
    associations.current = { { "log", FileAssociationState::Registered },
                             { "logcat", FileAssociationState::Default } };
    LostFileAssociationsHint hint( associations, { "log" }, {} );
    QSignalSpy dismissed( &hint, &LostFileAssociationsHint::dismissed );
    QSignalSpy finished( &hint, &LostFileAssociationsHint::finished );

    SECTION( "Restore applies the choice again for the lost types only" )
    {
        hint.restoreButton()->click();
        REQUIRE( associations.applied.size() == 1 );
        CHECK( associations.applied[ 0 ].first == QStringList{ "log" } );
        CHECK( associations.applied[ 0 ].second.isEmpty() );
        CHECK( associations.state( *FileTypes::find( "log" ) ) == FileAssociationState::Default );
        CHECK( finished.count() == 1 );
        CHECK( dismissed.isEmpty() );
    }

    SECTION( "Dismiss applies nothing, and the next start does not name the loss again" )
    {
        hint.dismissButton()->click();
        CHECK( associations.applied.empty() );
        REQUIRE( dismissed.count() == 1 );
        CHECK( dismissed.at( 0 ).at( 0 ).toStringList() == QStringList{ "log" } );
        CHECK( finished.count() == 1 );

        FileAssociationChoice choice;
        choice.apply( { "log", "logcat" } );
        choice.dismissed = dismissed.at( 0 ).at( 0 ).toStringList();
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost.isEmpty() );

        // Until a choice is applied again.
        choice.apply( { "log", "logcat" } );
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost
               == QStringList{ "log" } );
    }
}
