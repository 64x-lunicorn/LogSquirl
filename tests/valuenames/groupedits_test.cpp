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

// How a Naming Rule and its Name Table are added to a Naming Group: the one
// operation the Value Names dialog and "Name this value..." share (#883).

#include "groupedits.h"
#include "valuenamer.h"

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::valuenames;

namespace {

NamingRule rule( const QString& name, const QString& pattern )
{
    NamingRule namingRule;
    namingRule.name = name;
    namingRule.pattern = pattern;
    return namingRule;
}

NameTable table( const QString& name, const QList<NameRow>& rows )
{
    NameTable nameTable;
    nameTable.name = name;
    nameTable.rows = rows;
    return nameTable;
}

RuleWithTable esimState( bool existingTable = false )
{
    RuleWithTable added;
    added.rule = rule( QStringLiteral( "EsimState" ), QStringLiteral( "EsimState=(\\d+)" ) );
    added.rule.displayTemplate = QStringLiteral( "{name} ({value})" );
    added.table = table( QStringLiteral( "EsimState" ),
                         { NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
    added.existingTable = existingTable;
    return added;
}

QString shown( const NamingGroup& group, const QString& line )
{
    const ValueNamer namer( { group } );
    return shownLine( line, namer.namedValues( line ) );
}

} // namespace

SCENARIO( "A free name is the name itself or the first numbered one not taken",
          "[valuenames][groupedits]" )
{
    REQUIRE( freeName( QStringLiteral( "Rule" ), {} ) == QStringLiteral( "Rule" ) );
    REQUIRE( freeName( QStringLiteral( "Rule" ), { QStringLiteral( "Rule" ) } )
             == QStringLiteral( "Rule (2)" ) );
    REQUIRE( freeName( QStringLiteral( "Rule" ),
                       { QStringLiteral( "Rule" ), QStringLiteral( "Rule (2)" ) } )
             == QStringLiteral( "Rule (3)" ) );
    REQUIRE(
        freeName( QStringLiteral( "Net" ), { QStringLiteral( "Net" ), QStringLiteral( "Net (2)" ),
                                             QStringLiteral( "Net (4)" ) } )
        == QStringLiteral( "Net (3)" ) );
}

SCENARIO( "A new Name Table without a better name is the first Name Table N not taken",
          "[valuenames][groupedits]" )
{
    REQUIRE( newTableName( {} ) == QStringLiteral( "Name Table 1" ) );
    REQUIRE( newTableName( { QStringLiteral( "Name Table 1" ), QStringLiteral( "Other" ) } )
             == QStringLiteral( "Name Table 2" ) );
    REQUIRE( newTableName( { QStringLiteral( "Name Table 2" ) } )
             == QStringLiteral( "Name Table 1" ) );
}

SCENARIO( "The names an edited rule or table must not take leave out its own",
          "[valuenames][groupedits]" )
{
    const QList<NameTable> tables{ table( QStringLiteral( "A" ), {} ),
                                   table( QStringLiteral( "B" ), {} ) };
    REQUIRE( tableNames( tables ) == QStringList{ QStringLiteral( "A" ), QStringLiteral( "B" ) } );
    REQUIRE( tableNames( tables, 0 ) == QStringList{ QStringLiteral( "B" ) } );
    const QList<NamingRule> rules{ rule( QStringLiteral( "R" ), {} ),
                                   rule( QStringLiteral( "S" ), {} ) };
    REQUIRE( ruleNames( rules, 1 ) == QStringList{ QStringLiteral( "R" ) } );
}

SCENARIO( "A rule and a table are added under names unique in their group",
          "[valuenames][groupedits]" )
{
    auto group = NamingGroup::createNewGroup( QStringLiteral( "G" ) );

    WHEN( "rules without a name are added" )
    {
        const auto first = addRule( group, NamingRule{}, QStringLiteral( "Rule" ) );
        const auto second = addRule( group, NamingRule{}, QStringLiteral( "Rule" ) );

        THEN( "they get the fallback name, numbered from the second on" )
        {
            REQUIRE( first == QStringLiteral( "Rule" ) );
            REQUIRE( second == QStringLiteral( "Rule (2)" ) );
            REQUIRE( ruleNames( group.rules() ) == QStringList{ first, second } );
        }
    }

    WHEN( "a table of a name the group has is added" )
    {
        addTable( group, table( QStringLiteral( "T" ), {} ) );
        const auto name = addTable( group, table( QStringLiteral( "T" ), {} ) );

        THEN( "it is numbered" )
        {
            REQUIRE( name == QStringLiteral( "T (2)" ) );
            REQUIRE( tableNames( group.tables() )
                     == QStringList{ QStringLiteral( "T" ), QStringLiteral( "T (2)" ) } );
        }
    }

    WHEN( "tables without a name are added" )
    {
        const auto first = addTable( group, NameTable{} );
        const auto second = addTable( group, NameTable{} );

        THEN( "they are Name Table 1 and Name Table 2" )
        {
            REQUIRE( first == QStringLiteral( "Name Table 1" ) );
            REQUIRE( second == QStringLiteral( "Name Table 2" ) );
        }
    }
}

SCENARIO( "A rule and its table are created in a group in one operation",
          "[valuenames][groupedits]" )
{
    GIVEN( "an empty group" )
    {
        auto group = NamingGroup::createNewGroup( QStringLiteral( "G" ) );

        WHEN( "the rule EsimState=(\\d+) and the table EsimState are created" )
        {
            const auto added = addRuleWithTable( group, esimState(), QStringLiteral( "Rule" ) );

            THEN( "the group has both, the rule's capture group using the table" )
            {
                REQUIRE( added.rule == QStringLiteral( "EsimState" ) );
                REQUIRE( added.table == QStringLiteral( "EsimState" ) );
                REQUIRE( group.rules().size() == 1 );
                REQUIRE( group.tables().size() == 1 );
                REQUIRE( group.rules().front().tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "EsimState" ) );
                REQUIRE( group.tables().front().rows.size() == 1 );
                REQUIRE( validate( group ).isEmpty() );
            }

            THEN( "the group names the value" )
            {
                REQUIRE( shown( group, QStringLiteral( "modem EsimState=9" ) )
                         == QStringLiteral( "modem EsimState=Ready (9)" ) );
            }
        }
    }

    GIVEN( "a group that has a rule and a table of those names" )
    {
        auto group = NamingGroup::createNewGroup( QStringLiteral( "G" ) );
        addRuleWithTable( group, esimState(), QStringLiteral( "Rule" ) );

        WHEN( "they are created again as new" )
        {
            const auto added = addRuleWithTable( group, esimState(), QStringLiteral( "Rule" ) );

            THEN( "both are numbered, and the new rule uses the new table" )
            {
                REQUIRE( added.rule == QStringLiteral( "EsimState (2)" ) );
                REQUIRE( added.table == QStringLiteral( "EsimState (2)" ) );
                REQUIRE( group.rules().size() == 2 );
                REQUIRE( group.tables().size() == 2 );
                REQUIRE( group.rules().back().tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "EsimState (2)" ) );
                REQUIRE( validate( group ).isEmpty() );
            }
        }

        WHEN( "a rule is created with the existing table, its rows edited" )
        {
            auto withExisting = esimState( true );
            withExisting.rule.pattern = QStringLiteral( "state (\\d+)" );
            withExisting.table.rows.append(
                NameRow{ QStringLiteral( "3" ), QStringLiteral( "Off" ) } );
            withExisting.table.caseSensitive = true;
            const auto added = addRuleWithTable( group, withExisting, QStringLiteral( "Rule" ) );

            THEN( "the table is not added again, its rows are the edited ones, and both rules "
                  "use it" )
            {
                REQUIRE( added.table == QStringLiteral( "EsimState" ) );
                REQUIRE( group.tables().size() == 1 );
                REQUIRE( group.tables().front().rows.size() == 2 );
                REQUIRE( group.tables().front().caseSensitive );
                REQUIRE( group.rules().size() == 2 );
                REQUIRE( group.rules().back().tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "EsimState" ) );
                REQUIRE( shown( group, QStringLiteral( "state 3" ) )
                         == QStringLiteral( "state Off (3)" ) );
            }
        }
    }

    GIVEN( "a table said to be existing that the group does not have" )
    {
        auto group = NamingGroup::createNewGroup( QStringLiteral( "G" ) );
        const auto added = addRuleWithTable( group, esimState( true ), QStringLiteral( "Rule" ) );

        THEN( "it is added as a new one" )
        {
            REQUIRE( added.table == QStringLiteral( "EsimState" ) );
            REQUIRE( group.tables().size() == 1 );
        }
    }

    GIVEN( "a rule without a name, a table without a name, and a rule with a second capture "
           "group using a table" )
    {
        auto group = NamingGroup::createNewGroup( QStringLiteral( "G" ) );
        RuleWithTable added;
        added.rule = rule( {}, QStringLiteral( "(\\w+)=(\\d+)" ) );
        added.rule.groupTables = { GroupTable{ QStringLiteral( "1" ), QStringLiteral( "Keys" ) },
                                   GroupTable{ QStringLiteral( "2" ), QStringLiteral( "Old" ) } };
        added.captureGroup = QStringLiteral( "2" );

        const auto names = addRuleWithTable( group, added, QStringLiteral( "Rule" ) );

        THEN( "the rule gets the fallback name and the table Name Table 1, and only the capture "
              "group given uses the table" )
        {
            REQUIRE( names.rule == QStringLiteral( "Rule" ) );
            REQUIRE( names.table == QStringLiteral( "Name Table 1" ) );
            const auto& created = group.rules().front();
            REQUIRE( created.tableFor( QStringLiteral( "1" ) ) == QStringLiteral( "Keys" ) );
            REQUIRE( created.tableFor( QStringLiteral( "2" ) )
                     == QStringLiteral( "Name Table 1" ) );
            REQUIRE( created.groupTables.size() == 2 );
        }
    }
}
