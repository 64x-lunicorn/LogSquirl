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

// The Group Chooser the quick-add dialogs choose their group with (#955): the
// user's own groups and the Team groups under headings, New ... and New Team
// ... with a name field, the Team hint, and the group chosen last.

#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStandardItemModel>

#include "groupchooser.h"
#include "shown_widget.h"

using Section = GroupChooser::Section;

namespace {

GroupChooser::Texts texts()
{
    GroupChooser::Texts texts;
    texts.ownSection = QStringLiteral( "My Things" );
    texts.teamSection = QStringLiteral( "Team Things" );
    texts.newGroup = QStringLiteral( "New Thing Group..." );
    texts.newTeamGroup = QStringLiteral( "New Team Thing Group..." );
    texts.newGroupName = QStringLiteral( "New Thing Group" );
    texts.teamHint = QStringLiteral( "OK shares it with the team." );
    texts.nameEmpty = QStringLiteral( "No name." );
    texts.nameTaken = QStringLiteral( "Taken: %1" );
    return texts;
}

const GroupChooser::Group Default{ QStringLiteral( "default" ),
                                   QStringLiteral( "Default" ),
                                   { QStringLiteral( "Errors" ), QStringLiteral( "Warnings" ) } };
const GroupChooser::Group Network{ QStringLiteral( "network" ), QStringLiteral( "Network" ), {} };
// One of the user's own groups has the name a new one is proposed with.
const GroupChooser::Group Taken{ QStringLiteral( "taken" ),
                                 QStringLiteral( "New Thing Group" ),
                                 {} };
const GroupChooser::Group Crashes{ QStringLiteral( "crashes" ),
                                   QStringLiteral( "Crashes" ),
                                   { QStringLiteral( "Fatal" ) } };

QStringList entriesOf( const QComboBox& combo )
{
    QStringList entries;
    for ( int row = 0; row < combo.count(); ++row ) {
        entries.append( combo.itemText( row ) );
    }
    return entries;
}

bool canBeChosen( const QComboBox& combo, const QString& text )
{
    const auto row = combo.findText( text );
    REQUIRE( row >= 0 );
    const auto* model = qobject_cast<const QStandardItemModel*>( combo.model() );
    REQUIRE( model != nullptr );
    const auto flags = model->item( row )->flags();
    return flags.testFlag( Qt::ItemIsEnabled ) && flags.testFlag( Qt::ItemIsSelectable );
}

void pick( GroupChooser& chooser, const QString& text )
{
    auto* combo = chooser.comboBox();
    const auto row = combo->findText( text );
    REQUIRE( row >= 0 );
    combo->setCurrentIndex( row );
}

bool shown( const QWidget& part, const QWidget& chooser )
{
    return part.isVisibleTo( &chooser );
}

} // namespace

SCENARIO( "The Group Chooser offers the user's own groups, and New ...", "[ui][groupchooser]" )
{
    GroupChooser chooser( texts() );
    chooser.setGroups( { Default, Network, Taken } );
    showUntilExposed( chooser );
    auto* combo = chooser.comboBox();

    THEN( "the own groups are listed under a heading that cannot be chosen, New ... last, and "
          "there is no Team section" )
    {
        const auto entries = entriesOf( *combo );
        REQUIRE( entries.first() == QStringLiteral( "My Things" ) );
        REQUIRE_FALSE( canBeChosen( *combo, QStringLiteral( "My Things" ) ) );
        REQUIRE( entries.mid( 1, 3 )
                 == QStringList{ QStringLiteral( "Default" ), QStringLiteral( "Network" ),
                                 QStringLiteral( "New Thing Group" ) } );
        REQUIRE( entries.last() == QStringLiteral( "New Thing Group..." ) );
        REQUIRE_FALSE( entries.contains( QStringLiteral( "Team Things" ) ) );
        REQUIRE_FALSE( entries.contains( QStringLiteral( "New Team Thing Group..." ) ) );
        REQUIRE_FALSE( chooser.hasTeamSection() );
    }

    THEN( "the first own group is chosen; no name field and no Team hint show" )
    {
        REQUIRE( chooser.choice().id == Default.id );
        REQUIRE( chooser.choice().section == Section::Own );
        REQUIRE_FALSE( chooser.choice().isNew() );
        REQUIRE_FALSE( shown( *chooser.nameEdit(), chooser ) );
        REQUIRE_FALSE( shown( *chooser.teamHint(), chooser ) );
        REQUIRE( chooser.warning().isEmpty() );
    }

    THEN( "each group's tooltip lists what it holds, and its names are those checked against" )
    {
        REQUIRE( combo->itemData( combo->findText( QStringLiteral( "Default" ) ), Qt::ToolTipRole )
                     .toString()
                 == QStringLiteral( "Errors\nWarnings" ) );
        REQUIRE( chooser.itemNames() == Default.itemNames );
        REQUIRE( combo->toolTip() == QStringLiteral( "Errors\nWarnings" ) );
    }

    WHEN( "New ... is chosen" )
    {
        QSignalSpy changed( &chooser, &GroupChooser::choiceChanged );
        pick( chooser, QStringLiteral( "New Thing Group..." ) );

        THEN( "the name field shows below the choice, with the first free name" )
        {
            REQUIRE( shown( *chooser.nameEdit(), chooser ) );
            REQUIRE( chooser.nameEdit()->text() == QStringLiteral( "New Thing Group (2)" ) );
            REQUIRE( chooser.choice().isNew() );
            REQUIRE( chooser.choice().section == Section::Own );
            REQUIRE( chooser.choice().name == QStringLiteral( "New Thing Group (2)" ) );
            REQUIRE( chooser.itemNames().isEmpty() );
            REQUIRE( chooser.warning().isEmpty() );
            REQUIRE_FALSE( shown( *chooser.teamHint(), chooser ) );
            REQUIRE( changed.count() >= 1 );
        }

        AND_WHEN( "the name is that of a group of the user's" )
        {
            chooser.nameEdit()->setText( QStringLiteral( "Network" ) );

            THEN( "it says so" )
            {
                REQUIRE( chooser.warning() == QStringLiteral( "Taken: Network" ) );
            }
        }

        AND_WHEN( "the name is emptied" )
        {
            chooser.nameEdit()->setText( QStringLiteral( "  " ) );

            THEN( "it says so" )
            {
                REQUIRE( chooser.warning() == QStringLiteral( "No name." ) );
            }
        }

        AND_WHEN( "a group is chosen again" )
        {
            pick( chooser, QStringLiteral( "Network" ) );

            THEN( "the name field is gone" )
            {
                REQUIRE_FALSE( shown( *chooser.nameEdit(), chooser ) );
                REQUIRE( chooser.choice().id == Network.id );
            }
        }
    }
}

SCENARIO( "The Group Chooser offers the Team groups, with the Team Folder on",
          "[ui][groupchooser]" )
{
    GroupChooser chooser( texts() );
    GIVEN( "Team groups that can be changed" )
    {
        chooser.setGroups( { Default, Network },
                           GroupChooser::TeamGroups{ { Crashes, Taken }, true } );
        showUntilExposed( chooser );
        auto* combo = chooser.comboBox();

        THEN( "they are listed under a heading of their own, below the user's own, and New Team "
              "... after New ..." )
        {
            const auto entries = entriesOf( *combo );
            const auto team = entries.indexOf( QStringLiteral( "Team Things" ) );
            REQUIRE( team > entries.indexOf( QStringLiteral( "Network" ) ) );
            REQUIRE_FALSE( canBeChosen( *combo, QStringLiteral( "Team Things" ) ) );
            REQUIRE( entries.indexOf( QStringLiteral( "Crashes" ) ) == team + 1 );
            REQUIRE( canBeChosen( *combo, QStringLiteral( "Crashes" ) ) );
            REQUIRE( entries.indexOf( QStringLiteral( "New Team Thing Group..." ) )
                     == entries.indexOf( QStringLiteral( "New Thing Group..." ) ) + 1 );
            REQUIRE( chooser.hasTeamSection() );
        }

        WHEN( "a Team group is chosen" )
        {
            pick( chooser, QStringLiteral( "Crashes" ) );

            THEN( "the Team hint shows, and no name field" )
            {
                REQUIRE( chooser.choice().section == Section::Team );
                REQUIRE( chooser.choice().id == Crashes.id );
                REQUIRE( shown( *chooser.teamHint(), chooser ) );
                REQUIRE( chooser.teamHint()->text()
                         == QStringLiteral( "OK shares it with the team." ) );
                REQUIRE_FALSE( shown( *chooser.nameEdit(), chooser ) );
                REQUIRE( chooser.itemNames() == Crashes.itemNames );
            }
        }

        WHEN( "New Team ... is chosen" )
        {
            pick( chooser, QStringLiteral( "New Team Thing Group..." ) );

            THEN( "the name field shows with a name no Team group has, and the Team hint" )
            {
                REQUIRE( chooser.choice().isNew() );
                REQUIRE( chooser.choice().section == Section::Team );
                REQUIRE( chooser.nameEdit()->text() == QStringLiteral( "New Thing Group (2)" ) );
                REQUIRE( shown( *chooser.nameEdit(), chooser ) );
                REQUIRE( shown( *chooser.teamHint(), chooser ) );
            }

            AND_WHEN( "the name is that of a Team group" )
            {
                chooser.nameEdit()->setText( QStringLiteral( "Crashes" ) );

                THEN( "it says so" )
                {
                    REQUIRE( chooser.warning() == QStringLiteral( "Taken: Crashes" ) );
                }
            }

            AND_WHEN( "the name is that of a group of the user's own only" )
            {
                chooser.nameEdit()->setText( QStringLiteral( "Network" ) );

                THEN( "it will do: the Team groups are another section" )
                {
                    REQUIRE( chooser.warning().isEmpty() );
                }
            }
        }
    }

    GIVEN( "Team groups that cannot be changed" )
    {
        chooser.setGroups( { Default }, GroupChooser::TeamGroups{ { Crashes }, false } );
        auto* combo = chooser.comboBox();

        THEN( "they are listed greyed, and there is no New Team ..." )
        {
            REQUIRE_FALSE( canBeChosen( *combo, QStringLiteral( "Crashes" ) ) );
            REQUIRE( combo->findText( QStringLiteral( "New Team Thing Group..." ) ) < 0 );
        }

        THEN( "one of them chosen last is not chosen again" )
        {
            chooser.choose( Crashes.id, Default.id );
            REQUIRE( chooser.choice().id == Default.id );
        }
    }
}

SCENARIO( "The Group Chooser chooses the group chosen last, the fallback when it is gone",
          "[ui][groupchooser]" )
{
    GroupChooser chooser( texts() );
    chooser.setGroups( { Default, Network }, GroupChooser::TeamGroups{ { Crashes }, true } );

    WHEN( "a group of the user's own is chosen" )
    {
        chooser.choose( Network.id, Default.id );
        THEN( "it is chosen" )
        {
            REQUIRE( chooser.choice().id == Network.id );
        }
    }

    WHEN( "a Team group is chosen" )
    {
        chooser.choose( Crashes.id, Default.id );
        THEN( "it is chosen" )
        {
            REQUIRE( chooser.choice().id == Crashes.id );
            REQUIRE( chooser.choice().section == Section::Team );
        }
    }

    WHEN( "a group that is gone is chosen" )
    {
        chooser.choose( QStringLiteral( "gone" ), Network.id );
        THEN( "the fallback is chosen" )
        {
            REQUIRE( chooser.choice().id == Network.id );
        }
    }

    WHEN( "neither the group nor the fallback is there" )
    {
        chooser.choose( QStringLiteral( "gone" ), QStringLiteral( "also gone" ) );
        THEN( "the first of the user's own is chosen" )
        {
            REQUIRE( chooser.choice().id == Default.id );
        }
    }

    WHEN( "the Team Folder is off now, and a Team group was chosen last" )
    {
        chooser.setGroups( { Default, Network } );
        chooser.choose( Crashes.id, Default.id );
        THEN( "the fallback is chosen" )
        {
            REQUIRE( chooser.choice().id == Default.id );
            REQUIRE_FALSE( chooser.hasTeamSection() );
        }
    }
}
