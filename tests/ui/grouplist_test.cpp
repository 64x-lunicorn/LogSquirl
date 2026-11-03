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

// The Group List the Predefined Filters, Highlighters and Value Names dialogs
// share (#893).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

#include "grouplist.h"
#include "highlightersdialog.h"
#include "modal_answers.h"
#include "predefinedfiltersdialog.h"
#include "shown_widget.h"
#include "valuenamesdialog.h"

using logsquirl::groupexchange::GroupKind;
using Section = GroupList::Section;

namespace {

// The kind's name, singular and plural, as the list spells it.
struct KindName {
    GroupKind kind;
    QString one;
    QString many;
};

// Every text the list shows: the titles of the sections and of the buttons,
// and their tooltips.
QStringList shownTexts( const GroupList& list )
{
    QStringList texts;
    const auto* tree = list.findChild<QTreeWidget*>( QStringLiteral( "groupList" ) );
    for ( int index = 0; index < tree->topLevelItemCount(); ++index ) {
        texts.append( tree->topLevelItem( index )->text( 0 ) );
    }
    const QList<const QAbstractButton*> buttons{ list.newButton(),  list.deleteButton(),
                                                 list.upButton(),   list.downButton(),
                                                 list.shareButton(), list.copyButton(),
                                                 list.exportButton(), list.importButton() };
    for ( const auto* button : buttons ) {
        texts.append( button->text() );
        texts.append( button->toolTip() );
    }
    REQUIRE( list.newButton()->menu() != nullptr );
    for ( const auto* action : list.newButton()->menu()->actions() ) {
        texts.append( action->text() );
    }
    return texts;
}

// The texts with the kind's name taken out.
QStringList withoutKind( QStringList texts, const KindName& name )
{
    for ( auto& text : texts ) {
        text.replace( name.many, QStringLiteral( "<kinds>" ) );
        text.replace( name.one, QStringLiteral( "<kind>" ) );
    }
    return texts;
}

// A list with all three sections, the Team groups changeable.
void fill( GroupList& list )
{
    list.setOwnGroups( { "Default", "Mine", "Other" }, { 0 } );
    list.setTeamGroups( { "Shared" }, true );
    list.setPluginGroups( QList<PluginGroup<PredefinedFilterSet>>{
        { PredefinedFilterSet::createNewSet( "Crashes" ), "Shipping" } } );
}

} // namespace

TEST_CASE( "The Group List shows the user's own, the Team and the Plugin groups as sections of "
           "one list",
           "[ui][grouplist]" )
{
    GroupList list( GroupKind::Filter );
    auto* tree = list.findChild<QTreeWidget*>( QStringLiteral( "groupList" ) );
    REQUIRE( tree != nullptr );

    // Only the user's own groups at first.
    REQUIRE( tree->topLevelItemCount() == 1 );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == "My Filter Groups" );
    CHECK_FALSE( list.hasSection( Section::Team ) );
    CHECK_FALSE( list.hasSection( Section::Plugin ) );

    // No Plugin section without a plugin's group.
    list.setPluginGroups( QList<PluginGroup<PredefinedFilterSet>>{} );
    CHECK_FALSE( list.hasSection( Section::Plugin ) );

    // The sections keep their order, whichever comes first.
    list.setPluginGroups( QList<PluginGroup<PredefinedFilterSet>>{
        { PredefinedFilterSet::createNewSet( "Crashes" ), "Shipping" } } );
    list.setTeamGroups( { "Shared", "Zeta" }, false );
    list.setOwnGroups( { "Default", "Mine" } );
    REQUIRE( tree->topLevelItemCount() == 3 );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == "My Filter Groups" );
    CHECK( tree->topLevelItem( 1 )->text( 0 ) == "Team Filter Groups" );
    CHECK( tree->topLevelItem( 2 )->text( 0 ) == "Plugin Filter Groups" );
    CHECK( list.count( Section::Own ) == 2 );
    CHECK( list.count( Section::Team ) == 2 );
    CHECK( list.count( Section::Plugin ) == 1 );
    CHECK( list.name( Section::Team, 1 ) == "Zeta" );
    // A plugin's group is listed with its Source.
    CHECK( list.name( Section::Plugin, 0 ) == pluginGroupShownName( "Crashes", "Shipping" ) );

    // A section title is never selected.
    CHECK_FALSE( tree->topLevelItem( 1 )->flags().testFlag( Qt::ItemIsSelectable ) );
    CHECK( tree->topLevelItem( 1 )->child( 0 )->flags().testFlag( Qt::ItemIsSelectable ) );
    CHECK( tree->topLevelItem( 1 )->child( 0 )->flags().testFlag( Qt::ItemIsEnabled ) );

    // The plugin's groups gone, the section goes.
    list.setPluginGroups( QList<PluginGroup<PredefinedFilterSet>>{} );
    CHECK_FALSE( list.hasSection( Section::Plugin ) );
}

TEST_CASE( "The Group List says the same for every kind of group, apart from the kind's name",
           "[ui][grouplist]" )
{
    const auto name = GENERATE( KindName{ GroupKind::Highlighter, "Highlighter Set",
                                          "Highlighter Sets" },
                                KindName{ GroupKind::ValueNames, "Naming Group", "Naming Groups" } );

    GroupList filters( GroupKind::Filter );
    fill( filters );
    GroupList other( name.kind );
    fill( other );

    const auto filterTexts = withoutKind( shownTexts( filters ), { GroupKind::Filter,
                                                                   "Filter Group",
                                                                   "Filter Groups" } );
    const auto otherTexts = withoutKind( shownTexts( other ), name );
    CHECK( filterTexts == otherTexts );

    // The buttons name the kind, and those that open a file dialog say so.
    CHECK( other.newButton()->text() == "New " + name.one );
    CHECK( other.deleteButton()->toolTip() == "Delete " + name.one );
    CHECK( other.upButton()->toolTip() == "Move " + name.one + " Up" );
    CHECK( other.downButton()->toolTip() == "Move " + name.one + " Down" );
    CHECK( other.exportButton()->text() == "Export..." );
    CHECK( other.importButton()->text() == "Import..." );
    CHECK( other.newTeamAction()->text() == "New Team " + name.one );
}

SCENARIO( "The Group List enables what can be done with the group selected", "[ui][grouplist]" )
{
    GroupList list( GroupKind::Filter );
    fill( list );
    QSignalSpy current( &list, &GroupList::currentChanged );

    WHEN( "nothing is selected" )
    {
        THEN( "only a new group and Import... are offered" )
        {
            CHECK( list.currentRow() == -1 );
            CHECK( list.newButton()->isEnabled() );
            CHECK( list.importButton()->isEnabled() );
            CHECK_FALSE( list.deleteButton()->isEnabled() );
            CHECK_FALSE( list.upButton()->isEnabled() );
            CHECK_FALSE( list.downButton()->isEnabled() );
            CHECK_FALSE( list.shareButton()->isEnabled() );
            CHECK_FALSE( list.copyButton()->isEnabled() );
            CHECK_FALSE( list.exportButton()->isEnabled() );
        }
    }

    WHEN( "a group of the user's own is selected in the list" )
    {
        auto* tree = list.findChild<QTreeWidget*>( QStringLiteral( "groupList" ) );
        showUntilExposed( list );
        const auto* item = tree->topLevelItem( 0 )->child( 1 );
        QTest::mouseClick( tree->viewport(), Qt::LeftButton, {},
                           tree->visualItemRect( item ).center() );

        THEN( "the list says so, and it can be moved, deleted, shared and exported" )
        {
            REQUIRE( current.count() == 1 );
            CHECK( current.last().at( 0 ).value<Section>() == Section::Own );
            CHECK( current.last().at( 1 ).toInt() == 1 );
            CHECK( list.currentSection() == Section::Own );
            CHECK( list.currentRow() == 1 );
            CHECK( list.deleteButton()->isEnabled() );
            CHECK( list.upButton()->isEnabled() );
            CHECK( list.downButton()->isEnabled() );
            CHECK( list.shareButton()->isEnabled() );
            CHECK_FALSE( list.copyButton()->isEnabled() );
            CHECK( list.exportButton()->isEnabled() );
        }
    }

    WHEN( "the first and the last group of the user's own are selected" )
    {
        list.setCurrent( Section::Own, 0 );
        const bool firstUp = list.upButton()->isEnabled();
        const bool firstDeletable = list.deleteButton()->isEnabled();
        list.setCurrent( Section::Own, 2 );

        THEN( "the first does not move up, the last not down, an undeletable one is kept" )
        {
            CHECK_FALSE( firstUp );
            CHECK_FALSE( firstDeletable );
            CHECK( list.upButton()->isEnabled() );
            CHECK_FALSE( list.downButton()->isEnabled() );
            CHECK( list.deleteButton()->isEnabled() );
        }
    }

    WHEN( "a Team group is selected" )
    {
        list.setCurrent( Section::Team, 0 );

        THEN( "it is copied, deleted and exported, not moved or shared" )
        {
            CHECK( list.copyButton()->isEnabled() );
            CHECK( list.deleteButton()->isEnabled() );
            CHECK( list.exportButton()->isEnabled() );
            CHECK_FALSE( list.upButton()->isEnabled() );
            CHECK_FALSE( list.downButton()->isEnabled() );
            CHECK_FALSE( list.shareButton()->isEnabled() );
        }

        AND_WHEN( "the Team groups are read-only" )
        {
            list.setTeamGroups( { "Shared" }, false );
            list.setCurrent( Section::Team, 0 );

            THEN( "it is only copied and exported; no Team group is added or shared into" )
            {
                CHECK( list.copyButton()->isEnabled() );
                CHECK( list.exportButton()->isEnabled() );
                CHECK_FALSE( list.deleteButton()->isEnabled() );
                CHECK( list.shareButton()->isHidden() );
                CHECK_FALSE( list.newTeamAction()->isVisible() );
            }
        }
    }

    WHEN( "a plugin's group is selected" )
    {
        list.setCurrent( Section::Plugin, 0 );

        THEN( "it is only copied and exported" )
        {
            CHECK( list.copyButton()->isEnabled() );
            CHECK( list.exportButton()->isEnabled() );
            CHECK_FALSE( list.deleteButton()->isEnabled() );
            CHECK_FALSE( list.upButton()->isEnabled() );
            CHECK_FALSE( list.shareButton()->isEnabled() );
        }
    }

    WHEN( "the section of the group selected is listed anew" )
    {
        list.setCurrent( Section::Team, 0 );
        current.clear();
        list.setOwnGroups( { "Default", "Mine" }, { 0 } );
        const auto afterOther = current.count();
        list.setTeamGroups( { "Shared", "New" }, true );

        THEN( "nothing is selected any more, and the list says so; another section leaves it" )
        {
            CHECK( afterOther == 0 );
            REQUIRE( current.count() == 1 );
            CHECK( current.last().at( 1 ).toInt() == -1 );
            CHECK( list.currentRow() == -1 );
        }
    }
}

SCENARIO( "The Group List says what the user asked for", "[ui][grouplist]" )
{
    GroupList list( GroupKind::Highlighter );
    fill( list );

    WHEN( "New is clicked, and New Team is chosen beside it" )
    {
        QSignalSpy added( &list, &GroupList::newRequested );
        list.newButton()->click();
        REQUIRE( list.newTeamAction()->isVisible() );
        list.newTeamAction()->trigger();

        THEN( "a group of the user's own, then a Team group is asked for" )
        {
            REQUIRE( added.count() == 2 );
            CHECK( added.at( 0 ).at( 0 ).value<Section>() == Section::Own );
            CHECK( added.at( 1 ).at( 0 ).value<Section>() == Section::Team );
        }
    }

    WHEN( "a group of the user's own is moved, shared, exported and deleted" )
    {
        QSignalSpy moved( &list, &GroupList::moveRequested );
        QSignalSpy shared( &list, &GroupList::shareRequested );
        QSignalSpy exported( &list, &GroupList::exportRequested );
        QSignalSpy deleted( &list, &GroupList::deleteRequested );
        list.setCurrent( Section::Own, 1 );
        list.upButton()->click();
        list.downButton()->click();
        list.shareButton()->click();
        list.exportButton()->click();
        list.deleteButton()->click();

        THEN( "each is asked for that group; the list changes no group itself" )
        {
            REQUIRE( moved.count() == 2 );
            CHECK( moved.at( 0 ) == QVariantList{ 1, -1 } );
            CHECK( moved.at( 1 ) == QVariantList{ 1, 1 } );
            REQUIRE( shared.count() == 1 );
            CHECK( shared.at( 0 ).at( 0 ).toInt() == 1 );
            REQUIRE( exported.count() == 1 );
            CHECK( exported.at( 0 ).at( 0 ).value<Section>() == Section::Own );
            REQUIRE( deleted.count() == 1 );
            CHECK( deleted.at( 0 ).at( 1 ).toInt() == 1 );
            CHECK( list.count( Section::Own ) == 3 );
        }
    }

    WHEN( "a Team group and a plugin's group are copied" )
    {
        QSignalSpy copied( &list, &GroupList::copyRequested );
        list.setCurrent( Section::Team, 0 );
        list.copyButton()->click();
        list.setCurrent( Section::Plugin, 0 );
        list.copyButton()->click();

        THEN( "a copy of each among the user's own is asked for" )
        {
            REQUIRE( copied.count() == 2 );
            CHECK( copied.at( 0 ).at( 0 ).value<Section>() == Section::Team );
            CHECK( copied.at( 1 ).at( 0 ).value<Section>() == Section::Plugin );
        }
    }

    WHEN( "Import... is clicked" )
    {
        QSignalSpy imported( &list, &GroupList::importRequested );
        list.importButton()->click();

        THEN( "an import is asked for" )
        {
            CHECK( imported.count() == 1 );
        }
    }

    WHEN( "a Team group is deleted" )
    {
        QSignalSpy deleted( &list, &GroupList::deleteRequested );
        list.setCurrent( Section::Team, 0 );
        ModalAnswers modals;
        const auto answer = GENERATE( QMessageBox::No, QMessageBox::Yes );
        modals.click( answer );
        list.deleteButton()->click();

        THEN( "the user is asked first, and only Yes deletes it for the whole team" )
        {
            CHECK( modals.unanswered() == 0 );
            REQUIRE( modals.messages().size() == 1 );
            CHECK( modals.messages().front() == "This deletes the group for the whole team." );
            CHECK( deleted.count() == ( answer == QMessageBox::Yes ? 1 : 0 ) );
        }
    }
}

TEST_CASE( "The three dialogs list their groups in the Group List", "[ui][grouplist]" )
{
    PredefinedFiltersDialog filters;
    HighlightersDialog highlighters;
    ValueNamesDialog valueNames;

    CHECK( filters.findChildren<GroupList*>() == QList<GroupList*>{ filters.groupList() } );
    CHECK( highlighters.findChildren<GroupList*>()
           == QList<GroupList*>{ highlighters.groupList() } );
    CHECK( valueNames.findChildren<GroupList*>() == QList<GroupList*>{ valueNames.groupList() } );
    CHECK( filters.groupList()->newButton()->text() == "New Filter Group" );
    CHECK( highlighters.groupList()->newButton()->text() == "New Highlighter Set" );
    CHECK( valueNames.groupList()->newButton()->text() == "New Naming Group" );

    // The Team groups are a section of the same list.
    filters.showTeamGroups( { PredefinedFilterSet::createNewSet( "Theirs" ) }, true );
    CHECK( filters.groupList()->count( Section::Team ) == 1 );
    QCoreApplication::processEvents();
}
