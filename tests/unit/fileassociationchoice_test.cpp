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

#include "fake_file_associations.h"
#include "fileassociationchoice.h"

namespace {

constexpr auto Default = FileAssociationState::Default;
constexpr auto Registered = FileAssociationState::Registered;
constexpr auto Unconfirmed = FileAssociationState::Unconfirmed;

} // namespace

TEST_CASE( "The first start asks which file types LogSquirl opens, with the suggested ones checked",
           "[fileassociations][firststart]" )
{
    FakeFileAssociations associations;
    FileAssociationChoice choice;

    SECTION( "nothing is LogSquirl's yet" )
    {
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.ask );
        CHECK( atStart.checks == QStringList{ "log", "logcat" } );
        CHECK( atStart.lost.isEmpty() );
    }

    SECTION( "a type LogSquirl already opens is checked too" )
    {
        associations.current = { { "text", Default } };
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.ask );
        CHECK( atStart.checks == QStringList{ "log", "logcat", "text" } );
    }

    SECTION( "every suggested type is LogSquirl's already, as the installer chose" )
    {
        associations.current = { { "log", Default }, { "logcat", Default } };
        CHECK_FALSE( FileAssociationsAtStart::of( associations, choice, true ).ask );
    }

    SECTION( "a start with a file to open does not ask" )
    {
        CHECK_FALSE( FileAssociationsAtStart::of( associations, choice, false ).ask );
    }

    SECTION( "the user answered Don't ask again, or applied a choice" )
    {
        choice.ask = false;
        CHECK_FALSE( FileAssociationsAtStart::of( associations, choice, true ).ask );
    }

    SECTION( "a run that cannot associate, such as an AppImage, never asks" )
    {
        associations.available = false;
        CHECK_FALSE( FileAssociationsAtStart::of( associations, choice, true ).ask );
    }
}

TEST_CASE( "Applying a choice keeps it, stops the question and forgets what was dismissed",
           "[fileassociations][firststart]" )
{
    FileAssociationChoice choice;
    choice.dismissed = { "log" };
    choice.confirmed = { "log", "logcat" };
    choice.apply( { "log", "trace" } );
    CHECK_FALSE( choice.ask );
    REQUIRE( choice.chosen );
    CHECK( *choice.chosen == QStringList{ "log", "trace" } );
    CHECK( choice.dismissed.isEmpty() );
    // A type that stays chosen stays confirmed.
    CHECK( choice.confirmed == QStringList{ "log" } );
}

TEST_CASE( "A chosen type counts as confirmed once LogSquirl opens it",
           "[fileassociations][lostassociation]" )
{
    FileAssociationChoice choice;
    CHECK_FALSE( choice.confirm( { { "log", Default } } ) );

    choice.apply( { "log", "trace" } );
    CHECK_FALSE( choice.confirm( { { "log", Unconfirmed }, { "text", Default } } ) );
    CHECK( choice.confirmed.isEmpty() );

    CHECK( choice.confirm( { { "log", Default }, { "text", Default } } ) );
    CHECK( choice.confirmed == QStringList{ "log" } );
    CHECK_FALSE( choice.confirm( { { "log", Default } } ) );
}

TEST_CASE( "A chosen file type that is no longer LogSquirl's is lost",
           "[fileassociations][lostassociation]" )
{
    FakeFileAssociations associations;
    FileAssociationChoice choice;
    choice.ask = false;
    choice.apply( { "log", "logcat" } );
    // LogSquirl opened both since.
    choice.confirm( { { "log", Default }, { "logcat", Default } } );

    SECTION( "no hint while every chosen type is LogSquirl's" )
    {
        associations.current = { { "log", Default }, { "logcat", Default } };
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.lost.isEmpty() );
        CHECK_FALSE( atStart.ask );
        CHECK_FALSE( atStart.choiceChanged );
    }

    SECTION( "another application took .log over" )
    {
        associations.current = { { "log", Registered }, { "logcat", Default } };
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.lost == QStringList{ "log" } );
        CHECK( atStart.movedFrom.isEmpty() );
    }

    SECTION( "a type the user unchecked never counts" )
    {
        choice.apply( { "logcat" } );
        associations.current = { { "log", Registered }, { "logcat", Default } };
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost.isEmpty() );
    }

    SECTION( "a dismissed loss is not named again" )
    {
        associations.current = { { "log", Registered }, { "logcat", Registered } };
        choice.dismissed = { "log" };
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost
               == QStringList{ "logcat" } );
    }

    SECTION( "a run that cannot associate says nothing" )
    {
        associations.available = false;
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost.isEmpty() );
    }

    SECTION( "a moved portable LogSquirl names every chosen type and where it was" )
    {
        associations.current = { { "log", Default }, { "logcat", Default } };
        associations.moved = "D:\\Old\\logsquirl_portable.exe";
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.lost == QStringList{ "log", "logcat" } );
        CHECK( atStart.movedFrom == associations.moved );
    }
}

TEST_CASE( "A chosen type LogSquirl never opened is not lost, as one Windows waits to confirm",
           "[fileassociations][lostassociation]" )
{
    FakeFileAssociations associations;
    FileAssociationChoice choice;
    choice.apply( { "log", "trace" } );

    // Applied, but the user never chose LogSquirl on the Default apps page.
    associations.current = { { "log", Unconfirmed }, { "trace", Registered } };
    auto atStart = FileAssociationsAtStart::of( associations, choice, true );
    CHECK( atStart.lost.isEmpty() );
    CHECK_FALSE( atStart.choiceChanged );

    // Confirmed later: a start counts it, and keeps that.
    associations.current[ "log" ] = Default;
    atStart = FileAssociationsAtStart::of( associations, choice, true );
    CHECK( atStart.lost.isEmpty() );
    CHECK( atStart.choiceChanged );
    CHECK( choice.confirmed == QStringList{ "log" } );

    // Taken over by a Windows update: lost. .trace never was LogSquirl's.
    associations.current[ "log" ] = Unconfirmed;
    atStart = FileAssociationsAtStart::of( associations, choice, true );
    CHECK( atStart.lost == QStringList{ "log" } );
    CHECK_FALSE( atStart.choiceChanged );
}

TEST_CASE( "Before the user chose, the types LogSquirl opens count as chosen, as the installer "
           "chose them",
           "[fileassociations][lostassociation]" )
{
    FakeFileAssociations associations;
    FileAssociationChoice choice;
    choice.ask = false;

    SECTION( "the installer made LogSquirl the default" )
    {
        associations.current = { { "log", Default }, { "text", Default } };
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK( atStart.choiceChanged );
        REQUIRE( choice.chosen );
        CHECK( *choice.chosen == QStringList{ "log", "text" } );
        CHECK( choice.confirmed == QStringList{ "log", "text" } );
        CHECK( atStart.lost.isEmpty() );

        // Taken over afterwards, it is lost.
        associations.current[ "text" ] = Registered;
        CHECK( FileAssociationsAtStart::of( associations, choice, true ).lost
               == QStringList{ "text" } );
    }

    SECTION( "nothing is LogSquirl's: nothing is chosen yet" )
    {
        const auto atStart = FileAssociationsAtStart::of( associations, choice, true );
        CHECK_FALSE( atStart.choiceChanged );
        CHECK_FALSE( choice.chosen );
    }
}
