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

TEST_CASE( "Applying a choice keeps it and stops the question", "[fileassociations][firststart]" )
{
    FileAssociationChoice choice;
    choice.apply( { "log", "trace" } );
    CHECK_FALSE( choice.ask );
    REQUIRE( choice.chosen );
    CHECK( *choice.chosen == QStringList{ "log", "trace" } );
}
