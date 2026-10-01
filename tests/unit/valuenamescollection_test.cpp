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

// The Value Names Collection stores the Naming Groups and, apart from them,
// their checks, and keeps the checks of groups it does not hold now (#647).

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include "valuenames_fixture.h"
#include "valuenamescollection.h"

using valuenamesfixture::exampleGroup;
using valuenamesfixture::NamingGroup;

namespace {

// The example group with its rule "Id" unchecked.
NamingGroup withIdUnchecked( NamingGroup group )
{
    auto rules = group.rules();
    rules[ 1 ].enabled = false;
    group.setRules( rules );
    return group;
}

} // namespace

SCENARIO( "The Value Names Collection stores its groups and their checks apart",
          "[valuenames][valuenamescollection]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( QStringLiteral( "settings.conf" ) );

    GIVEN( "a collection with a group whose rule Id is unchecked, and an unchecked group" )
    {
        auto unchecked = NamingGroup::createNewGroup( QStringLiteral( "Off" ) );
        unchecked.setEnabled( false );
        const auto group = withIdUnchecked( exampleGroup() );

        ValueNamesCollection collection;
        const auto generation = collection.generation();
        REQUIRE( collection.setGroups( { group, unchecked } ) );
        REQUIRE( collection.generation() > generation );

        WHEN( "it is saved and read into another collection" )
        {
            {
                QSettings settings( file, QSettings::IniFormat );
                collection.saveToStorage( settings );
            }
            QSettings settings( file, QSettings::IniFormat );
            ValueNamesCollection read;
            read.retrieveFromStorage( settings );

            THEN( "it holds the same groups with the same checks" )
            {
                REQUIRE( read.groups() == collection.groups() );
                REQUIRE_FALSE( read.groups()[ 0 ].rules()[ 1 ].enabled );
                REQUIRE( read.groups()[ 0 ].rules()[ 0 ].enabled );
                REQUIRE_FALSE( read.groups()[ 1 ].isEnabled() );
            }

            THEN( "the checks are stored by key, apart from the groups" )
            {
                REQUIRE( settings.value( "ValueNamesChecks/unchecked/size" ).toInt() == 2 );
                REQUIRE(
                    read.uncheckedKeys()
                    == QSet<QString>{
                        ValueNamesCollection::ruleCheckKey( group.id(), QStringLiteral( "Id" ) ),
                        ValueNamesCollection::groupCheckKey( unchecked.id() ) } );
            }
        }

        WHEN( "the same groups are set again" )
        {
            const auto before = collection.generation();

            THEN( "nothing changes" )
            {
                REQUIRE_FALSE( collection.setGroups( { group, unchecked } ) );
                REQUIRE( collection.generation() == before );
            }
        }

        WHEN( "the rule Id is checked by its key" )
        {
            const auto before = collection.generation();
            REQUIRE( collection.setUncheckedKeys(
                { ValueNamesCollection::groupCheckKey( unchecked.id() ) } ) );

            THEN( "the namer names with it, under a new generation" )
            {
                REQUIRE( collection.groups()[ 0 ].rules()[ 1 ].enabled );
                REQUIRE( collection.generation() > before );
                REQUIRE( collection.namer().namedValues( QStringLiteral( "id=7" ) ).size() == 1 );
            }
        }
    }
}

SCENARIO( "The checks of a Team group outlive the time it is not held",
          "[valuenames][valuenamescollection]" )
{
    GIVEN( "a collection holding a Team group with an unchecked rule" )
    {
        ValueNamesCollection collection;
        const auto team = exampleGroup();
        collection.setTeamGroups( { team } );
        REQUIRE( collection.teamGroupCount() == 1 );
        const auto key = ValueNamesCollection::ruleCheckKey( team.id(), QStringLiteral( "Id" ) );
        REQUIRE( collection.setUncheckedKeys( { key } ) );

        WHEN( "the Team group goes, its checks are set for the others, and it comes back" )
        {
            collection.setTeamGroups( {} );
            collection.setUncheckedKeys( {} );
            collection.setGroups( { NamingGroup::createNewGroup( QStringLiteral( "Own" ) ) } );
            collection.setTeamGroups( { team } );

            THEN( "its rule is unchecked again, and listed after the user's own group" )
            {
                REQUIRE( collection.groups().size() == 2 );
                REQUIRE( collection.groups()[ 1 ].id() == team.id() );
                REQUIRE_FALSE( collection.groups()[ 1 ].rules()[ 1 ].enabled );
                REQUIRE( collection.uncheckedKeys().contains( key ) );
            }
        }
    }
}
