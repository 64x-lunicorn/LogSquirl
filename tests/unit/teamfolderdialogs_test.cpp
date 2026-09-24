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

// The Team groups in the Highlighters and Predefined Filters dialogs, and the
// Team Highlighter Sets across a restart (#471, #472, #473).

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersetedit.h"
#include "predefinedfiltersdialog.h"
#include "predefinedfiltersetedit.h"
#include "teamfolder.h"

using namespace logsquirl::teamfolder;

namespace {

HighlighterSet makeSet( const QString& name, const QString& pattern = "ERROR" )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( pattern, false, true, Qt::red, Qt::white ) );
    return set;
}

PredefinedFilterSet makeGroup( const QString& name, const QString& pattern = "ERROR" )
{
    auto group = PredefinedFilterSet::createNewSet( name );
    group.addFilter( { "Errors", pattern, true } );
    return group;
}

QPushButton* buttonNamed( QWidget& parent, const QString& text )
{
    for ( auto* button : parent.findChildren<QPushButton*>() ) {
        if ( button->text() == text ) {
            return button;
        }
    }
    return nullptr;
}

// The list of the Team groups: the dialog's list that is not the given one.
QListWidget* otherList( QWidget& dialog, const QListWidget* own )
{
    QListWidget* found = nullptr;
    for ( auto* list : dialog.findChildren<QListWidget*>() ) {
        if ( list != own ) {
            found = list;
        }
    }
    return found;
}

// The Highlighter Sets of the collection, put back when this goes.
class KeepCollection {
public:
    KeepCollection()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , active_( HighlighterSetCollection::get().activeSetIds() )
    {
    }
    ~KeepCollection()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets_ );
        collection.deactivateAll();
        for ( const auto& id : active_ ) {
            collection.activateSet( id );
        }
    }

private:
    QList<HighlighterSet> sets_;
    QStringList active_;
};

} // namespace

TEST_CASE( "The Highlighters Dialog with Team sets takes a selection of its own sets",
           "[teamfolderdialogs]" )
{
    HighlightersDialog dialog;
    dialog.showTeamGroups( { makeSet( "Theirs" ) }, true );

    // A set of the user's own, selected. This used to dereference buttons
    // that were never created.
    dialog.addHighlighterButton->click();
    QCoreApplication::processEvents();
    REQUIRE( dialog.highlighterListWidget->currentRow() >= 0 );

    auto* share = buttonNamed( dialog, "Share with team" );
    auto* copy = buttonNamed( dialog, "Copy to my groups" );
    auto* remove = buttonNamed( dialog, "Delete for the team" );
    REQUIRE( share );
    REQUIRE( copy );
    REQUIRE( remove );
    CHECK( share->isEnabled() );
    CHECK_FALSE( copy->isEnabled() );
    CHECK_FALSE( remove->isEnabled() );

    auto* teamList = otherList( dialog, dialog.highlighterListWidget );
    REQUIRE( teamList );
    REQUIRE( teamList->count() == 1 );

    share->click();
    CHECK( teamList->count() == 2 );

    teamList->setCurrentRow( 0 );
    CHECK( copy->isEnabled() );
    const auto own = dialog.highlighterListWidget->count();
    copy->click();
    CHECK( dialog.highlighterListWidget->count() == own + 1 );
    QCoreApplication::processEvents();
}

TEST_CASE( "Team sets stay active until a sync has delivered groups", "[teamfolderdialogs]" )
{
    const auto theirs = makeSet( "Theirs" );
    QTemporaryDir dir;
    const auto path = dir.filePath( "settings.ini" );

    {
        HighlighterSetCollection before;
        before.setTeamHighlighterSets( { theirs } );
        before.activateSet( theirs.id() );
        QSettings settings( path, QSettings::IniFormat );
        before.saveToStorage( settings );
    }

    // The next start: read from the settings, the first sync not done.
    HighlighterSetCollection after;
    {
        QSettings settings( path, QSettings::IniFormat );
        after.retrieveFromStorage( settings );
    }
    after.setTeamHighlighterSets( {}, false );
    CHECK( after.activeSetIds().contains( theirs.id() ) );

    // The sync brings the set: still active.
    after.setTeamHighlighterSets( { theirs } );
    CHECK( after.activeSetIds().contains( theirs.id() ) );

    // A sync without the set: it is gone, and no longer active.
    after.setTeamHighlighterSets( {} );
    CHECK_FALSE( after.activeSetIds().contains( theirs.id() ) );
}

TEST_CASE( "A group published by Apply is edited on its new revision", "[teamfolderdialogs]" )
{
    SECTION( "Highlighter sets" )
    {
        KeepCollection keep;
        const auto original = makeSet( "Theirs" );
        HighlightersDialog dialog;
        dialog.showTeamGroups( { original }, true, { { original.id(), "R1" } } );
        QList<PublishRequest> requests;
        QObject::connect( &dialog, &HighlightersDialog::publishRequested, &dialog,
                          [ &requests ]( const auto& r ) { requests = r; } );

        auto* teamList = otherList( dialog, dialog.highlighterListWidget );
        REQUIRE( teamList );
        teamList->setCurrentRow( 0 );
        auto* edit = dialog.findChild<HighlighterSetEdit*>();
        REQUIRE( edit );
        auto* apply = dialog.buttonBox->button( QDialogButtonBox::Apply );
        REQUIRE( apply );

        auto edited = original;
        edited.addHighlighter( Highlighter( "WARN", false, true, Qt::blue, Qt::white ) );
        edit->setHighlighters( edited );
        apply->click();
        REQUIRE( requests.size() == 1 );
        CHECK( requests.front().baseRevision == QString( "R1" ) );

        // The publish ended: the file has a new revision.
        dialog.updateTeamRevisions( { original.id() }, { { original.id(), "R2" } } );
        requests.clear();
        edited.addHighlighter( Highlighter( "INFO", false, true, Qt::green, Qt::white ) );
        edit->setHighlighters( edited );
        apply->click();
        REQUIRE( requests.size() == 1 );
        CHECK( requests.front().baseRevision == QString( "R2" ) );
        // What the dialog left for the event loop must run before it is gone.
        QCoreApplication::processEvents();
    }

    SECTION( "Filter groups" )
    {
        const auto original = makeGroup( "Theirs" );
        PredefinedFiltersDialog dialog( QString{} );
        dialog.showTeamGroups( { original }, true, { { original.id(), "R1" } } );
        QList<PublishRequest> requests;
        QObject::connect( &dialog, &PredefinedFiltersDialog::publishRequested, &dialog,
                          [ &requests ]( const auto& r ) { requests = r; } );

        auto* teamList = otherList( dialog, dialog.setListWidget );
        REQUIRE( teamList );
        teamList->setCurrentRow( 0 );
        auto* edit = dialog.findChild<PredefinedFilterSetEdit*>();
        REQUIRE( edit );
        auto* apply = dialog.buttonBox->button( QDialogButtonBox::Apply );
        REQUIRE( apply );

        auto edited = original;
        edited.addFilter( { "Warnings", "WARN", true } );
        edit->setFilterSet( edited );
        apply->click();
        REQUIRE( requests.size() == 1 );
        CHECK( requests.front().baseRevision == QString( "R1" ) );

        dialog.updateTeamRevisions( { original.id() }, { { original.id(), "R2" } } );
        requests.clear();
        edited.addFilter( { "Info", "INFO", true } );
        edit->setFilterSet( edited );
        apply->click();
        REQUIRE( requests.size() == 1 );
        CHECK( requests.front().baseRevision == QString( "R2" ) );
        // What the dialog left for the event loop must run before it is gone.
        QCoreApplication::processEvents();
    }
}
