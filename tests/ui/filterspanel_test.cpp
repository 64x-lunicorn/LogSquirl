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

#include <catch2/catch.hpp>

#include <QApplication>
#include <QMap>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QVariant>

#include "filterspanel.h"
#include "persistentinfo.h"
#include "predefinedfilters.h"

// Helper: locate the internal QTreeWidget inside a FiltersPanel.
static QTreeWidget* findTree( FiltersPanel* panel )
{
    return panel->findChild<QTreeWidget*>();
}

// Helper: find a top-level group item by display name.
static QTreeWidgetItem* findGroup( QTreeWidget* tree, const QString& name )
{
    for ( int g = 0; g < tree->topLevelItemCount(); ++g ) {
        if ( tree->topLevelItem( g )->text( 0 ) == name ) {
            return tree->topLevelItem( g );
        }
    }
    return nullptr;
}

// Helper: count how many leaf (filter) items are checked across all groups.
static int countChecked( QTreeWidget* tree )
{
    int count = 0;
    for ( int g = 0; g < tree->topLevelItemCount(); ++g ) {
        const auto* group = tree->topLevelItem( g );
        for ( int c = 0; c < group->childCount(); ++c ) {
            if ( group->child( c )->checkState( 0 ) == Qt::Checked ) {
                ++count;
            }
        }
    }
    return count;
}

// Helper: invoke the private onItemDoubleClicked slot via Qt's meta-object system.
// This is necessary because the offscreen platform plugin does not support
// QTest::mouseDClick on QTreeWidget items.
static void invokeDoubleClick( FiltersPanel* panel, QTreeWidgetItem* item )
{
    const bool invoked = QMetaObject::invokeMethod( panel, "onItemDoubleClicked",
                                                    Q_ARG( QTreeWidgetItem*, item ),
                                                    Q_ARG( int, 0 ) );
    REQUIRE( invoked );
}

// Helper: set check states on leaf items without triggering onItemChanged.
// Prevents stale debounce timers that could fire on processEvents() and
// cause non-deterministic signal emissions.
static void setCheckedSilently( QTreeWidgetItem* group, Qt::CheckState state )
{
    auto* tree = group->treeWidget();
    tree->blockSignals( true );
    for ( int c = 0; c < group->childCount(); ++c ) {
        group->child( c )->setCheckState( 0, state );
    }
    tree->blockSignals( false );
}

// Clear pinned filter state so tests start with a clean slate.
static void clearPinnedFilters()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( "PinnedFilters" );
    settings.remove( "" );
    settings.endGroup();
    settings.sync();
}

class FiltersPanelTestStateGuard {
  public:
    FiltersPanelTestStateGuard()
        : originalSets_( PredefinedFiltersCollection::getSynced().filterSets() )
    {
        auto& settings = PersistentInfo::getSettings( app_settings{} );
        settings.beginGroup( "PinnedFilters" );
        const auto keys = settings.allKeys();
        for ( const auto& key : keys ) {
            pinnedState_.insert( key, settings.value( key ) );
        }
        settings.endGroup();
    }

    ~FiltersPanelTestStateGuard()
    {
        auto& collection = PredefinedFiltersCollection::getSynced();
        collection.setFilterSets( originalSets_ );
        collection.save();

        auto& settings = PersistentInfo::getSettings( app_settings{} );
        settings.beginGroup( "PinnedFilters" );
        settings.remove( "" );
        for ( auto it = pinnedState_.cbegin(); it != pinnedState_.cend(); ++it ) {
            settings.setValue( it.key(), it.value() );
        }
        settings.endGroup();
        settings.sync();
    }

  private:
    QList<PredefinedFilterSet> originalSets_;
    QMap<QString, QVariant> pinnedState_;
};

// Set up two filter groups with known test data.
static void setupTestFilters()
{
    clearPinnedFilters();

    auto groupA = PredefinedFilterSet::createNewSet( "GroupA" );
    groupA.setFilters( { { "Error", "ERROR", true },
                         { "Warning", "WARN", true },
                         { "Info", "INFO", true } } );

    auto groupB = PredefinedFilterSet::createNewSet( "GroupB" );
    groupB.setFilters( { { "Debug", "DEBUG", true }, { "Trace", "TRACE", true } } );

    auto& collection = PredefinedFiltersCollection::getSynced();
    collection.setFilterSets( { groupA, groupB } );
    collection.save();
}

SCENARIO( "Double-click on a filter activates only that filter",
          "[filterspanel][doubleclick]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );

    auto* groupA = findGroup( tree, "GroupA" );
    auto* groupB = findGroup( tree, "GroupB" );
    REQUIRE( groupA != nullptr );
    REQUIRE( groupB != nullptr );
    REQUIRE( groupA->childCount() == 3 );
    REQUIRE( groupB->childCount() == 2 );

    GIVEN( "no filters are checked initially" )
    {
        REQUIRE( countChecked( tree ) == 0 );

        WHEN( "double-clicking a child filter item" )
        {
            auto* warningItem = groupA->child( 1 );
            invokeDoubleClick( &panel, warningItem );

            THEN( "only that filter is checked" )
            {
                REQUIRE( countChecked( tree ) == 1 );
                REQUIRE( warningItem->checkState( 0 ) == Qt::Checked );
            }
        }
    }

    GIVEN( "multiple filters are already checked" )
    {
        setCheckedSilently( groupA, Qt::Checked );
        setCheckedSilently( groupB, Qt::Checked );
        REQUIRE( countChecked( tree ) == 5 );

        WHEN( "double-clicking a single filter" )
        {
            auto* traceItem = groupB->child( 1 );
            invokeDoubleClick( &panel, traceItem );

            THEN( "only the double-clicked filter remains checked" )
            {
                REQUIRE( countChecked( tree ) == 1 );
                REQUIRE( traceItem->checkState( 0 ) == Qt::Checked );
            }
        }
    }
}

SCENARIO( "Double-click on a group activates all its filters exclusively",
          "[filterspanel][doubleclick]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );

    auto* groupA = findGroup( tree, "GroupA" );
    auto* groupB = findGroup( tree, "GroupB" );
    REQUIRE( groupA != nullptr );
    REQUIRE( groupB != nullptr );

    GIVEN( "some filters are checked in GroupB" )
    {
        setCheckedSilently( groupB, Qt::Checked );
        REQUIRE( countChecked( tree ) == 2 );

        WHEN( "double-clicking GroupA's group item" )
        {
            invokeDoubleClick( &panel, groupA );

            THEN( "all GroupA filters are checked and GroupB filters are unchecked" )
            {
                REQUIRE( countChecked( tree ) == 3 );

                for ( int c = 0; c < groupA->childCount(); ++c ) {
                    REQUIRE( groupA->child( c )->checkState( 0 ) == Qt::Checked );
                }
                for ( int c = 0; c < groupB->childCount(); ++c ) {
                    REQUIRE( groupB->child( c )->checkState( 0 ) == Qt::Unchecked );
                }
            }
        }
    }
}

SCENARIO( "Double-click emits filtersChanged with correct selection",
          "[filterspanel][doubleclick]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );

    auto* groupA = findGroup( tree, "GroupA" );
    REQUIRE( groupA != nullptr );
    REQUIRE( groupA->childCount() == 3 );

    QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );

    GIVEN( "the panel is ready" )
    {
        WHEN( "double-clicking the Error filter" )
        {
            auto* errorItem = groupA->child( 0 );
            invokeDoubleClick( &panel, errorItem );

            THEN( "filtersChanged is emitted with exactly one filter" )
            {
                REQUIRE( spy.count() == 1 );
                const auto& emission = spy.at( 0 );
                const auto filters = emission.at( 0 ).value<QList<PredefinedFilter>>();
                REQUIRE( filters.size() == 1 );
                REQUIRE( filters[ 0 ].name == "Error" );
                REQUIRE( filters[ 0 ].pattern == "ERROR" );
            }
        }
    }
}

SCENARIO( "Pinned state survives panel recreation after flush",
          "[filterspanel][persistence]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    GIVEN( "a panel where one filter is solo-activated and flushed" )
    {
        {
            FiltersPanel panel;
            auto* tree = findTree( &panel );
            REQUIRE( tree != nullptr );

            auto* groupA = findGroup( tree, "GroupA" );
            REQUIRE( groupA != nullptr );

            invokeDoubleClick( &panel, groupA->child( 2 ) ); // "Info"
            panel.flushPendingSaves();
        } // panel destroyed — timer is dead, but save already flushed.

        WHEN( "a new panel is created" )
        {
            FiltersPanel panel2;
            auto* tree2 = findTree( &panel2 );
            REQUIRE( tree2 != nullptr );

            THEN( "the previously pinned filter is restored" )
            {
                REQUIRE( countChecked( tree2 ) == 1 );

                auto* groupA2 = findGroup( tree2, "GroupA" );
                REQUIRE( groupA2 != nullptr );
                REQUIRE( groupA2->child( 2 )->checkState( 0 ) == Qt::Checked );
            }
        }
    }
}

SCENARIO( "flushPendingSaves is a no-op when nothing is pending",
          "[filterspanel][persistence]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    GIVEN( "a freshly constructed panel with no user interaction" )
    {
        FiltersPanel panel;

        WHEN( "flushPendingSaves is called" )
        {
            // Must not crash or write unexpected state.
            panel.flushPendingSaves();

            THEN( "no filters are pinned" )
            {
                auto* tree = findTree( &panel );
                REQUIRE( tree != nullptr );
                REQUIRE( countChecked( tree ) == 0 );
            }
        }
    }
}
