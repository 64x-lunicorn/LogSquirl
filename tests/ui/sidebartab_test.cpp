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

// The SidebarTab the sidebar's Filters tab and Value Names tab are built on
// (#923): what it does with the groups and checks a tab hands it.

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "plugingroup.h"
#include "shown_widget.h"
#include "sidebartab.h"
#include "soloselection.h"
#include "theme.h"

namespace {

using Entry = SidebarTab::Entry;
using Group = SidebarTab::Group;

// Own with the entries "Error — ERROR" (checked) and "WARN"; the plugin's
// Crashes with "FATAL"; the Team's Shared with "Debug — DEBUG" (checked);
// Empty without entries and no check; Alone without entries, a check of its
// own.
QList<Group> someGroups()
{
    Group own{ .name = QStringLiteral( "Own" ) };
    own.entries = { { .key = QStringLiteral( "own/Error" ),
                      .name = QStringLiteral( "Error" ),
                      .pattern = QStringLiteral( "ERROR" ),
                      .checked = true },
                    { .key = QStringLiteral( "own/WARN" ),
                      .name = QStringLiteral( "WARN" ),
                      .pattern = QStringLiteral( "WARN" ) } };

    Group plugin{ .name = QStringLiteral( "Crashes" ),
                  .pluginName = QStringLiteral( "Android" ),
                  .kind = Group::Kind::Plugin };
    plugin.entries
        = { { .key = QStringLiteral( "crashes/FATAL" ), .pattern = QStringLiteral( "FATAL" ) } };

    Group team{ .name = QStringLiteral( "Shared" ), .kind = Group::Kind::Team };
    team.entries = { { .key = QStringLiteral( "shared/Debug" ),
                       .name = QStringLiteral( "Debug" ),
                       .pattern = QStringLiteral( "DEBUG" ),
                       .checked = true } };

    const Group empty{ .name = QStringLiteral( "Empty" ) };
    const Group alone{ .name = QStringLiteral( "Alone" ), .key = QStringLiteral( "alone" ) };

    return { own, plugin, team, empty, alone };
}

QTreeWidget* treeOf( SidebarTab& tab )
{
    auto* tree = tab.findChild<QTreeWidget*>();
    REQUIRE( tree != nullptr );
    return tree;
}

QTreeWidgetItem* topLevel( SidebarTab& tab, int index )
{
    auto* item = treeOf( tab )->topLevelItem( index );
    REQUIRE( item != nullptr );
    return item;
}

QPushButton* button( SidebarTab& tab, const QString& text )
{
    for ( auto* candidate : tab.findChildren<QPushButton*>() ) {
        if ( candidate->text() == text ) {
            return candidate;
        }
    }
    FAIL( "no button " << text.toStdString() );
    return nullptr;
}

// The texts of the groups and entries shown, in tree order.
QStringList shownTexts( SidebarTab& tab )
{
    QStringList texts;
    auto* tree = treeOf( tab );
    for ( int g = 0; g < tree->topLevelItemCount(); ++g ) {
        const auto* group = tree->topLevelItem( g );
        if ( group->isHidden() ) {
            continue;
        }
        texts.append( group->text( 0 ) );
        for ( int e = 0; e < group->childCount(); ++e ) {
            if ( !group->child( e )->isHidden() ) {
                texts.append( group->child( e )->text( 0 ) );
            }
        }
    }
    return texts;
}

void doubleClick( SidebarTab& tab, QTreeWidgetItem* item )
{
    REQUIRE( QMetaObject::invokeMethod( &tab, "onItemDoubleClicked",
                                        Q_ARG( QTreeWidgetItem*, item ), Q_ARG( int, 0 ) ) );
}

const QString Dash = QStringLiteral( "  —  " );

} // namespace

SCENARIO( "A sidebar tab shows the groups and entries a tab hands it", "[ui][sidebartab]" )
{
    SidebarTab tab( QStringLiteral( "Search things..." ) );
    tab.setGroups( someGroups() );

    THEN( "the search field shows the tab's placeholder, and the buttons are there" )
    {
        REQUIRE( tab.findChild<QLineEdit*>()->placeholderText() == "Search things..." );
        button( tab, QStringLiteral( "Select All" ) );
        button( tab, QStringLiteral( "Deselect All" ) );
        button( tab, QStringLiteral( "Edit..." ) );
    }

    THEN( "an entry shows its name and pattern, or its pattern alone, and the groups their "
          "Source or (Team)" )
    {
        REQUIRE( shownTexts( tab )
                 == QStringList{ "Own", "Error" + Dash + "ERROR", "WARN",
                                 pluginGroupShownName( "Crashes", "Android" ), "FATAL",
                                 "Shared (Team)", "Debug" + Dash + "DEBUG", "Empty", "Alone" } );
    }

    THEN( "the tool tips name the pattern, the Source or the Team, and the double-click" )
    {
        const auto hint = SoloSelection::doubleClickHint();
        REQUIRE( topLevel( tab, 0 )->toolTip( 0 ) == hint );
        REQUIRE( topLevel( tab, 0 )->child( 0 )->toolTip( 0 )
                 == SoloSelection::toolTipWithHint( "ERROR" ) );
        REQUIRE( topLevel( tab, 1 )->toolTip( 0 ).contains( "Android" ) );
        REQUIRE( topLevel( tab, 1 )->toolTip( 0 ).contains( hint ) );
        REQUIRE( topLevel( tab, 2 )->toolTip( 0 ).contains( "Team Folder" ) );
        REQUIRE( topLevel( tab, 2 )->toolTip( 0 ).contains( hint ) );
    }

    THEN( "the checks are those handed over; a group without entries has one only with a key" )
    {
        REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "shared/Debug" } );
        REQUIRE( tab.uncheckedKeys() == QStringList{ "own/WARN", "crashes/FATAL", "alone" } );
        REQUIRE( topLevel( tab, 3 )->data( 0, Qt::CheckStateRole ).isNull() );
        REQUIRE( topLevel( tab, 4 )->checkState( 0 ) == Qt::Unchecked );
    }

    THEN( "the groups with entries are expanded" )
    {
        REQUIRE( topLevel( tab, 0 )->isExpanded() );
    }
}

SCENARIO( "A sidebar tab tells the tab of the checks the user changes", "[ui][sidebartab]" )
{
    SidebarTab tab( QStringLiteral( "Search..." ) );
    QSignalSpy changed( &tab, &SidebarTab::checksChanged );
    tab.setGroups( someGroups() );
    QCoreApplication::processEvents();

    THEN( "setting the groups is no change of the user's" )
    {
        REQUIRE( changed.isEmpty() );
    }

    WHEN( "a group's check is clicked" )
    {
        topLevel( tab, 0 )->setCheckState( 0, Qt::Checked );
        QCoreApplication::processEvents();

        THEN( "all its entries are checked, and the tab is told once" )
        {
            REQUIRE( changed.count() == 1 );
            REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "own/WARN", "shared/Debug" } );
        }
    }

    WHEN( "the groups are set again while a click is not yet told" )
    {
        topLevel( tab, 0 )->child( 1 )->setCheckState( 0, Qt::Checked );
        tab.setGroups( someGroups() );
        QCoreApplication::processEvents();

        THEN( "the groups set overtake it" )
        {
            REQUIRE( changed.isEmpty() );
            REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "shared/Debug" } );
        }
    }

    WHEN( "Edit... is clicked" )
    {
        QSignalSpy edit( &tab, &SidebarTab::editRequested );
        button( tab, QStringLiteral( "Edit..." ) )->click();

        THEN( "the tab is asked to edit" )
        {
            REQUIRE( edit.count() == 1 );
            REQUIRE( changed.isEmpty() );
        }
    }
}

SCENARIO( "The search of a sidebar tab hides entries and keeps their checks",
          "[ui][sidebartab][search]" )
{
    SidebarTab tab( QStringLiteral( "Search..." ) );
    tab.setGroups( someGroups() );
    QSignalSpy changed( &tab, &SidebarTab::checksChanged );
    QSignalSpy searched( &tab, &SidebarTab::searchChanged );
    auto* search = tab.findChild<QLineEdit*>();

    WHEN( "the search matches a pattern" )
    {
        search->setText( QStringLiteral( "debug" ) );

        THEN( "only that entry and its group show, and every check stays" )
        {
            REQUIRE( shownTexts( tab )
                     == QStringList{ "Shared (Team)", "Debug" + Dash + "DEBUG" } );
            REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "shared/Debug" } );
            REQUIRE( searched.count() == 1 );
            REQUIRE( changed.isEmpty() );
        }

        AND_WHEN( "Deselect All is clicked and the search is cleared" )
        {
            button( tab, QStringLiteral( "Deselect All" ) )->click();
            search->clear();

            THEN( "only the entry shown was unchecked" )
            {
                REQUIRE( changed.count() == 1 );
                REQUIRE( tab.checkedKeys() == QStringList{ "own/Error" } );
                REQUIRE( shownTexts( tab ).size() == 9 );
            }
        }
    }

    WHEN( "the search matches a group's name" )
    {
        search->setText( QStringLiteral( " own " ) );

        THEN( "the group shows with all its entries" )
        {
            REQUIRE( shownTexts( tab ) == QStringList{ "Own", "Error" + Dash + "ERROR", "WARN" } );
        }

        AND_WHEN( "Select All is clicked" )
        {
            button( tab, QStringLiteral( "Select All" ) )->click();

            THEN( "the entries shown are checked, the others keep their checks" )
            {
                REQUIRE( changed.count() == 1 );
                REQUIRE( tab.checkedKeys()
                         == QStringList{ "own/Error", "own/WARN", "shared/Debug" } );
            }
        }
    }

    WHEN( "the search matches the names of groups without entries" )
    {
        search->setText( QStringLiteral( "e" ) );

        THEN( "one with a check of its own shows, one without does not" )
        {
            const auto shown = shownTexts( tab );
            REQUIRE( shown.contains( "Alone" ) );
            REQUIRE_FALSE( shown.contains( "Empty" ) );
        }
    }

    WHEN( "the search matches a group without entries that is a check of its own" )
    {
        search->setText( QStringLiteral( "alone" ) );
        button( tab, QStringLiteral( "Select All" ) )->click();

        THEN( "Select All checks it" )
        {
            REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "shared/Debug", "alone" } );
        }
    }

    WHEN( "the groups are set again while searching" )
    {
        search->setText( QStringLiteral( "fatal" ) );
        tab.setGroups( someGroups() );

        THEN( "the search applies to them" )
        {
            REQUIRE( shownTexts( tab )
                     == QStringList{ pluginGroupShownName( "Crashes", "Android" ), "FATAL" } );
        }
    }
}

SCENARIO( "A sidebar tab shows only one entry or group and restores the checks",
          "[ui][sidebartab][soloselection]" )
{
    SidebarTab tab( QStringLiteral( "Search..." ) );
    tab.setGroups( someGroups() );
    QSignalSpy changed( &tab, &SidebarTab::checksChanged );
    auto* solo = tab.findChild<SoloSelection*>();
    REQUIRE( solo != nullptr );

    WHEN( "an entry is double-clicked" )
    {
        doubleClick( tab, topLevel( tab, 1 )->child( 0 ) );

        THEN( "only it is checked, and the tab is told at once" )
        {
            REQUIRE( tab.checkedKeys() == QStringList{ "crashes/FATAL" } );
            REQUIRE( changed.count() == 1 );
        }

        AND_WHEN( "Restore previous selection is chosen" )
        {
            REQUIRE( solo->canRestore() );
            solo->restore();

            THEN( "the checks from before are back" )
            {
                REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "shared/Debug" } );
                REQUIRE( changed.count() == 2 );
            }
        }
    }

    WHEN( "a group is double-clicked while the search hides some of the checked entries" )
    {
        tab.findChild<QLineEdit*>()->setText( QStringLiteral( "warn" ) );
        doubleClick( tab, topLevel( tab, 0 ) );

        THEN( "all its entries, and nothing else, are checked" )
        {
            REQUIRE( tab.checkedKeys() == QStringList{ "own/Error", "own/WARN" } );
        }
    }

    WHEN( "a group without entries that is a check of its own is double-clicked" )
    {
        doubleClick( tab, topLevel( tab, 4 ) );

        THEN( "only it is checked" )
        {
            REQUIRE( tab.checkedKeys() == QStringList{ "alone" } );
        }
    }
}

SCENARIO( "A sidebar tab shows its empty hint while it has no groups", "[ui][sidebartab]" )
{
    SidebarTab tab( QStringLiteral( "Search..." ) );
    auto* hint = tab.findChild<QLabel*>();
    REQUIRE( hint != nullptr );

    GIVEN( "no hint" )
    {
        tab.setGroups( {} );

        THEN( "none shows" )
        {
            REQUIRE( hint->isHidden() );
        }
    }

    GIVEN( "a hint" )
    {
        tab.setEmptyHint( QStringLiteral( "Nothing yet" ) );

        THEN( "it shows while there are no groups, and not with some" )
        {
            REQUIRE_FALSE( hint->isHidden() );
            REQUIRE( hint->text() == "Nothing yet" );
            tab.setGroups( someGroups() );
            REQUIRE( hint->isHidden() );
            tab.setGroups( {} );
            REQUIRE_FALSE( hint->isHidden() );
        }
    }
}

SCENARIO( "A sidebar tab's rows are as tall whenever its groups were set",
          "[ui][sidebartab][look]" )
{
    // #924: the Value Names tab, filled before the window placed it in the
    // sidebar, showed its entries without the rows' padding of the Theme.
    Theme::apply( Theme::defaultTheme() );
    const auto groups = someGroups();

    // The heights of the rows of the tab placed behind another in a tab
    // widget, then brought to the front; its groups set before it was placed
    // or after.
    const auto rowHeights = [ &groups ]( bool setBeforePlaced ) {
        auto* tab = new SidebarTab( QStringLiteral( "Search..." ) );
        if ( setBeforePlaced ) {
            tab->setGroups( groups );
        }
        QTabWidget tabs;
        tabs.addTab( new SidebarTab( QStringLiteral( "Search..." ) ), QStringLiteral( "front" ) );
        tabs.addTab( tab, QStringLiteral( "back" ) );
        if ( !setBeforePlaced ) {
            tab->setGroups( groups );
        }
        tabs.resize( 300, 400 );
        showUntilExposed( tabs );
        tabs.setCurrentWidget( tab );
        QCoreApplication::processEvents();

        QList<int> heights;
        auto* tree = treeOf( *tab );
        for ( int g = 0; g < tree->topLevelItemCount(); ++g ) {
            const auto* group = tree->topLevelItem( g );
            heights.append( tree->visualItemRect( group ).height() );
            for ( int e = 0; e < group->childCount(); ++e ) {
                heights.append( tree->visualItemRect( group->child( e ) ).height() );
            }
        }
        return heights;
    };

    REQUIRE( rowHeights( true ) == rowHeights( false ) );
}
