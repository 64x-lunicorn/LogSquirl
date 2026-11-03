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

// "Show only this" of the sidebar's trees and the way back (#889): the
// SoloSelection both tabs use, and the Value Names tab with it. The Filters
// tab's are in filterspanel_test.cpp.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QMenu>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "soloselection.h"
#include "valuenames_fixture.h"
#include "valuenamescollection.h"
#include "valuenamespanel.h"

using valuenamesfixture::NamingGroup;
using valuenamesfixture::NamingRule;

namespace {

// The key of an item of the trees below: its text, empty for a group with
// items under it.
QString textKey( const QTreeWidgetItem* item )
{
    return item->childCount() > 0 ? QString{} : item->text( 0 );
}

// Group A with the items a1 and a2, group B with b1, and the group C without
// items; all unchecked.
void fill( QTreeWidget& tree )
{
    const auto addGroup = [ &tree ]( const QString& name, const QStringList& items ) {
        auto* group = new QTreeWidgetItem( &tree, { name } );
        group->setFlags( group->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable );
        group->setCheckState( 0, Qt::Unchecked );
        for ( const auto& text : items ) {
            auto* item = new QTreeWidgetItem( group, { text } );
            item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
            item->setCheckState( 0, Qt::Unchecked );
        }
    };
    addGroup( QStringLiteral( "A" ), { QStringLiteral( "a1" ), QStringLiteral( "a2" ) } );
    addGroup( QStringLiteral( "B" ), { QStringLiteral( "b1" ) } );
    addGroup( QStringLiteral( "C" ), {} );
}

// The texts of the checked units, in tree order.
QStringList checked( const QTreeWidget& tree )
{
    QStringList texts;
    for ( int g = 0; g < tree.topLevelItemCount(); ++g ) {
        const auto* group = tree.topLevelItem( g );
        if ( group->childCount() == 0 && group->checkState( 0 ) == Qt::Checked ) {
            texts.append( group->text( 0 ) );
        }
        for ( int c = 0; c < group->childCount(); ++c ) {
            if ( group->child( c )->checkState( 0 ) == Qt::Checked ) {
                texts.append( group->child( c )->text( 0 ) );
            }
        }
    }
    return texts;
}

QTreeWidgetItem* itemNamed( QTreeWidget& tree, const QString& text )
{
    const auto found = tree.findItems( text, Qt::MatchExactly | Qt::MatchRecursive );
    REQUIRE( found.size() == 1 );
    return found.first();
}

QAction* actionOf( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

const QString ShowOnlyThis = QStringLiteral( "Show only this" );
const QString RestorePrevious = QStringLiteral( "Restore previous selection" );

} // namespace

SCENARIO( "Show only this checks one item or group and Restore brings the checks back",
          "[ui][soloselection]" )
{
    GIVEN( "a tree with a1, b1 and the group C checked" )
    {
        QTreeWidget tree;
        fill( tree );
        SoloSelection solo( &tree, textKey );
        QSignalSpy changed( &solo, &SoloSelection::checksChanged );
        QSignalSpy itemChanged( &tree, &QTreeWidget::itemChanged );
        for ( const auto* text : { "a1", "b1", "C" } ) {
            itemNamed( tree, QString::fromLatin1( text ) )->setCheckState( 0, Qt::Checked );
        }
        itemChanged.clear();

        THEN( "there is nothing to restore" )
        {
            REQUIRE_FALSE( solo.canRestore() );
        }

        WHEN( "a2 is shown only" )
        {
            solo.showOnly( itemNamed( tree, QStringLiteral( "a2" ) ) );

            THEN( "only a2 is checked, told once and not item by item" )
            {
                REQUIRE( checked( tree ) == QStringList{ QStringLiteral( "a2" ) } );
                REQUIRE( changed.count() == 1 );
                REQUIRE( itemChanged.isEmpty() );
                REQUIRE( solo.canRestore() );
            }

            AND_WHEN( "the previous selection is restored" )
            {
                solo.restore();

                THEN( "the checks from before are back, and there is nothing more to restore" )
                {
                    REQUIRE( checked( tree )
                             == QStringList{ QStringLiteral( "a1" ), QStringLiteral( "b1" ),
                                             QStringLiteral( "C" ) } );
                    REQUIRE( changed.count() == 2 );
                    REQUIRE_FALSE( solo.canRestore() );
                }
            }
        }

        WHEN( "the group A is shown only" )
        {
            solo.showOnly( itemNamed( tree, QStringLiteral( "A" ) ) );

            THEN( "its items are checked, nothing else" )
            {
                REQUIRE( checked( tree )
                         == QStringList{ QStringLiteral( "a1" ), QStringLiteral( "a2" ) } );
            }
        }

        WHEN( "the group C, without items, is shown only" )
        {
            solo.showOnly( itemNamed( tree, QStringLiteral( "C" ) ) );

            THEN( "it alone is checked" )
            {
                REQUIRE( checked( tree ) == QStringList{ QStringLiteral( "C" ) } );
            }
        }

        WHEN( "a2 and then b1 are shown only" )
        {
            solo.showOnly( itemNamed( tree, QStringLiteral( "a2" ) ) );
            solo.showOnly( itemNamed( tree, QStringLiteral( "b1" ) ) );
            solo.restore();

            THEN( "Restore brings back the checks from before the last one" )
            {
                REQUIRE( checked( tree ) == QStringList{ QStringLiteral( "a2" ) } );
            }
        }

        WHEN( "the tree is built again, with an item more, between Show only this and Restore" )
        {
            solo.showOnly( itemNamed( tree, QStringLiteral( "a2" ) ) );
            tree.clear();
            fill( tree );
            itemNamed( tree, QStringLiteral( "a2" ) )->setCheckState( 0, Qt::Checked );
            auto* added = new QTreeWidgetItem( itemNamed( tree, QStringLiteral( "B" ) ),
                                               { QStringLiteral( "b2" ) } );
            added->setFlags( added->flags() | Qt::ItemIsUserCheckable );
            added->setCheckState( 0, Qt::Checked );
            solo.restore();

            THEN( "the checks are restored by their keys, and the new item keeps its own" )
            {
                REQUIRE( checked( tree )
                         == QStringList{ QStringLiteral( "a1" ), QStringLiteral( "b1" ),
                                         QStringLiteral( "b2" ), QStringLiteral( "C" ) } );
            }
        }
    }
}

SCENARIO( "The context menu of a sidebar tree offers Show only this and Restore",
          "[ui][soloselection]" )
{
    GIVEN( "a tree with a1 checked" )
    {
        QTreeWidget tree;
        fill( tree );
        SoloSelection solo( &tree, textKey );
        itemNamed( tree, QStringLiteral( "a1" ) )->setCheckState( 0, Qt::Checked );

        THEN( "the menu of an item offers Show only this, and a disabled Restore" )
        {
            const auto menu = solo.contextMenu( itemNamed( tree, QStringLiteral( "b1" ) ) );
            REQUIRE( menu->actions().size() == 2 );
            REQUIRE( actionOf( *menu, ShowOnlyThis ) != nullptr );
            REQUIRE( actionOf( *menu, RestorePrevious ) != nullptr );
            REQUIRE_FALSE( actionOf( *menu, RestorePrevious )->isEnabled() );
        }

        THEN( "the menu off the items offers Restore alone" )
        {
            const auto menu = solo.contextMenu( nullptr );
            REQUIRE( menu->actions().size() == 1 );
            REQUIRE( actionOf( *menu, RestorePrevious ) != nullptr );
        }

        WHEN( "Show only this of b1's menu is triggered" )
        {
            const auto menu = solo.contextMenu( itemNamed( tree, QStringLiteral( "b1" ) ) );
            actionOf( *menu, ShowOnlyThis )->trigger();

            THEN( "b1 alone is checked" )
            {
                REQUIRE( checked( tree ) == QStringList{ QStringLiteral( "b1" ) } );
            }

            AND_WHEN( "Restore of the next menu is triggered" )
            {
                const auto next = solo.contextMenu( itemNamed( tree, QStringLiteral( "a2" ) ) );
                REQUIRE( actionOf( *next, RestorePrevious )->isEnabled() );
                actionOf( *next, RestorePrevious )->trigger();

                THEN( "a1 is checked again" )
                {
                    REQUIRE( checked( tree ) == QStringList{ QStringLiteral( "a1" ) } );
                }
            }
        }

        WHEN( "the tree is built again while the menu of b1 is open" )
        {
            const auto menu = solo.contextMenu( itemNamed( tree, QStringLiteral( "b1" ) ) );
            tree.clear();
            fill( tree );
            actionOf( *menu, ShowOnlyThis )->trigger();

            THEN( "Show only this does nothing" )
            {
                REQUIRE( checked( tree ).isEmpty() );
                REQUIRE_FALSE( solo.canRestore() );
            }
        }
    }
}

namespace {

// Restores the collection, and what is stored of it, when it goes.
class StoredValueNamesGuard {
public:
    StoredValueNamesGuard()
        : before_( ValueNamesCollection::get().ownGroups() )
        , teamBefore_( ValueNamesCollection::get().groups().mid(
              ValueNamesCollection::get().ownGroups().size() ) )
    {
    }

    ~StoredValueNamesGuard()
    {
        auto& collection = ValueNamesCollection::get();
        collection.setTeamGroups( teamBefore_ );
        collection.setGroups( before_ );
        collection.save();
    }

    StoredValueNamesGuard( const StoredValueNamesGuard& ) = delete;
    StoredValueNamesGuard& operator=( const StoredValueNamesGuard& ) = delete;

private:
    QList<NamingGroup> before_;
    QList<NamingGroup> teamBefore_;
};

NamingRule namingRule( const QString& name, const QString& pattern )
{
    NamingRule rule;
    rule.name = name;
    rule.pattern = pattern;
    return rule;
}

// Whether each rule of the collection's groups is checked, group by group.
QList<QList<bool>> ruleChecks()
{
    QList<QList<bool>> checks;
    for ( const auto& group : ValueNamesCollection::get().groups() ) {
        QList<bool> rules;
        for ( const auto& rule : group.rules() ) {
            rules.append( group.isEnabled() && rule.enabled );
        }
        checks.append( rules );
    }
    return checks;
}

} // namespace

SCENARIO( "The Value Names tab shows only one Naming Rule and restores the checks",
          "[ui][valuenames][valuenamespanel][soloselection]" )
{
    const StoredValueNamesGuard guard;
    auto first = NamingGroup::createNewGroup( QStringLiteral( "A" ) );
    first.setRules( { namingRule( QStringLiteral( "One" ), QStringLiteral( "one=(\\d)" ) ),
                      namingRule( QStringLiteral( "Two" ), QStringLiteral( "two=(\\d)" ) ) } );
    auto second = NamingGroup::createNewGroup( QStringLiteral( "B" ) );
    second.setRules( { namingRule( QStringLiteral( "Three" ), QStringLiteral( "three=(\\d)" ) ) } );
    auto& collection = ValueNamesCollection::get();
    collection.setTeamGroups( {} );
    collection.setGroups( { first, second } );
    collection.save();

    GIVEN( "the tab with the rule Two unchecked" )
    {
        ValueNamesPanel panel;
        QSignalSpy changed( &panel, &ValueNamesPanel::valueNamesChanged );
        auto* tree = panel.findChild<QTreeWidget*>();
        REQUIRE( tree != nullptr );
        auto* groupA = tree->topLevelItem( 0 );
        auto* three = tree->topLevelItem( 1 )->child( 0 );
        groupA->child( 1 )->setCheckState( 0, Qt::Unchecked );
        QCoreApplication::processEvents();
        REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, false }, { true } } );
        changed.clear();

        THEN( "the tool tips of the rules and the groups name the double-click" )
        {
            const auto hint = SoloSelection::doubleClickHint();
            REQUIRE( three->toolTip( 0 ).contains( hint ) );
            REQUIRE( three->toolTip( 0 ).contains( QStringLiteral( "three=" ) ) );
            REQUIRE( groupA->toolTip( 0 ).contains( hint ) );
        }

        THEN( "the context menu of a rule offers Show only this, and Restore disabled" )
        {
            const auto menu = panel.findChild<SoloSelection*>()->contextMenu( three );
            REQUIRE( actionOf( *menu, ShowOnlyThis ) != nullptr );
            REQUIRE_FALSE( actionOf( *menu, RestorePrevious )->isEnabled() );
        }

        WHEN( "Show only this of the rule Three's context menu is triggered" )
        {
            auto* solo = panel.findChild<SoloSelection*>();
            REQUIRE( solo != nullptr );
            actionOf( *solo->contextMenu( three ), ShowOnlyThis )->trigger();

            THEN( "only Three is checked, as by the double-click" )
            {
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { false, false }, { true } } );
                REQUIRE( changed.count() == 1 );
            }

            AND_WHEN( "Restore previous selection is triggered" )
            {
                const auto menu = solo->contextMenu( three );
                REQUIRE( actionOf( *menu, RestorePrevious )->isEnabled() );
                actionOf( *menu, RestorePrevious )->trigger();

                THEN( "the checks from before are back" )
                {
                    REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, false }, { true } } );
                    REQUIRE( changed.count() == 2 );
                    REQUIRE_FALSE( solo->canRestore() );
                }
            }
        }

        WHEN( "the group A is double-clicked and the previous selection restored" )
        {
            REQUIRE( QMetaObject::invokeMethod( &panel, "onItemDoubleClicked",
                                                Q_ARG( QTreeWidgetItem*, groupA ),
                                                Q_ARG( int, 0 ) ) );
            const auto soloed = ruleChecks();
            panel.findChild<SoloSelection*>()->restore();

            THEN( "first A's rules alone, then the checks from before" )
            {
                REQUIRE( soloed == QList<QList<bool>>{ { true, true }, { false } } );
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, false }, { true } } );
            }
        }
    }
}
