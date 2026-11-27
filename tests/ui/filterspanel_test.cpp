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

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVariant>

#include "filterspanel.h"
#include "persistentinfo.h"
#include "predefinedfilters.h"
#include "soloselection.h"

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
    const bool invoked = QMetaObject::invokeMethod(
        panel, "onItemDoubleClicked", Q_ARG( QTreeWidgetItem*, item ), Q_ARG( int, 0 ) );
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

// The index of a group in the Predefined Filters Collection, by name: the
// Collection puts its Default group before the others.
static qsizetype groupIndex( const QList<PredefinedFilterSet>& sets, const QString& name )
{
    for ( qsizetype i = 0; i < sets.size(); ++i ) {
        if ( sets[ i ].name() == name ) {
            return i;
        }
    }
    FAIL( "no group " << name.toStdString() );
    return -1;
}

class FiltersPanelTestStateGuard {
public:
    FiltersPanelTestStateGuard()
        : originalSets_( PredefinedFiltersCollection::getSynced().filterSets() )
    {
    }

    ~FiltersPanelTestStateGuard()
    {
        auto& collection = PredefinedFiltersCollection::getSynced();
        collection.setFilterSets( originalSets_ );
        collection.save();
    }

private:
    QList<PredefinedFilterSet> originalSets_;
};

// Set up two filter groups with known test data.
static void setupTestFilters()
{
    auto groupA = PredefinedFilterSet::createNewSet( "GroupA" );
    groupA.setFilters(
        { { "Error", "ERROR", true }, { "Warning", "WARN", true }, { "Info", "INFO", true } } );

    auto groupB = PredefinedFilterSet::createNewSet( "GroupB" );
    groupB.setFilters( { { "Debug", "DEBUG", true }, { "Trace", "TRACE", true } } );

    auto& collection = PredefinedFiltersCollection::getSynced();
    collection.setFilterSets( { groupA, groupB } );
    collection.save();
}

SCENARIO( "Double-click on a filter activates only that filter", "[filterspanel][doubleclick]" )
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

SCENARIO( "The Filters tab shows the checks of the tab in front without telling its Search",
          "[filterspanel][pertab]" )
{
    // #984: the checks belong to the tab they were made in; the window hands
    // the panel those of the tab that comes to the front.
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );
    auto* errorItem = findGroup( tree, "GroupA" )->child( 0 );
    errorItem->setCheckState( 0, Qt::Checked );
    QCoreApplication::processEvents();
    const auto errorChecked = panel.checkedKeys();
    REQUIRE( errorChecked.size() == 1 );

    QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );

    WHEN( "the checks of a tab without any are shown" )
    {
        panel.showChecks( {} );
        QCoreApplication::processEvents();

        THEN( "no filter is checked, and the Search is not told" )
        {
            REQUIRE( countChecked( tree ) == 0 );
            REQUIRE( spy.isEmpty() );
        }

        AND_WHEN( "the checks of the first tab are shown again" )
        {
            panel.showChecks( errorChecked );
            QCoreApplication::processEvents();

            THEN( "Error is checked again, and the Search is not told" )
            {
                tree = findTree( &panel );
                REQUIRE( countChecked( tree ) == 1 );
                REQUIRE( findGroup( tree, "GroupA" )->child( 0 )->checkState( 0 ) == Qt::Checked );
                REQUIRE( spy.isEmpty() );
            }
        }

        AND_WHEN( "Debug is checked" )
        {
            findGroup( findTree( &panel ), "GroupB" )->child( 0 )->setCheckState( 0, Qt::Checked );
            QCoreApplication::processEvents();

            THEN( "the Search is told of Debug only" )
            {
                REQUIRE( spy.count() == 1 );
                const auto filters = spy.last().at( 0 ).value<QList<PredefinedFilter>>();
                REQUIRE( filters.size() == 1 );
                REQUIRE( filters[ 0 ].name == "Debug" );
            }
        }

        AND_WHEN( "the filter list is refreshed" )
        {
            panel.refreshFilters();

            THEN( "still no filter is checked, and the Search is not told" )
            {
                REQUIRE( countChecked( findTree( &panel ) ) == 0 );
                REQUIRE( spy.isEmpty() );
            }
        }
    }

    WHEN( "checks of a filter that no longer exists are shown" )
    {
        panel.showChecks( { QStringLiteral( "gone/Gone" ) } );

        THEN( "no filter is checked" )
        {
            REQUIRE( countChecked( findTree( &panel ) ) == 0 );
            REQUIRE( spy.isEmpty() );
        }
    }
}

SCENARIO( "Restore previous selection brings back no checks of another tab",
          "[filterspanel][pertab][soloselection]" )
{
    // #984: the checks from before "Show only this" are those of the tab it
    // was used in.
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );
    findGroup( tree, "GroupA" )->setCheckState( 0, Qt::Checked );
    QCoreApplication::processEvents();
    invokeDoubleClick( &panel, findGroup( tree, "GroupB" )->child( 0 ) );
    auto* solo = panel.findChild<SoloSelection*>();
    REQUIRE( solo != nullptr );
    REQUIRE( solo->canRestore() );

    WHEN( "the checks of another tab are shown" )
    {
        panel.showChecks( {} );

        THEN( "there is nothing to restore" )
        {
            REQUIRE_FALSE( solo->canRestore() );
        }
    }
}

SCENARIO( "A new Filters tab checks nothing, whatever an older version kept in the settings",
          "[filterspanel][pertab]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();
    const auto errorKey = QStringLiteral( "%1/Error" )
                              .arg( PredefinedFiltersCollection::get().filterSets().front().id() );

    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( "PinnedFilters" );
    settings.setValue( "count", 1 );
    settings.setValue( "filter0", errorKey );
    settings.endGroup();

    FiltersPanel panel;

    THEN( "no filter is checked" )
    {
        REQUIRE( countChecked( findTree( &panel ) ) == 0 );
    }

    settings.remove( "PinnedFilters" );
}

SCENARIO( "The settings migration removes the window-wide filter checks", "[filterspanel][pertab]" )
{
    GIVEN( "settings that hold the checks of an older version" )
    {
        QTemporaryDir directory;
        REQUIRE( directory.isValid() );
        QSettings appSettings( directory.filePath( QStringLiteral( "logsquirl.conf" ) ),
                               QSettings::IniFormat );
        QSettings sessionSettings( directory.filePath( QStringLiteral( "logsquirl_session.conf" ) ),
                                   QSettings::IniFormat );
        appSettings.setValue( QStringLiteral( "version" ), 1 );
        appSettings.setValue( QStringLiteral( "PinnedFilters/count" ), 1 );
        appSettings.setValue( QStringLiteral( "PinnedFilters/filter0" ), QStringLiteral( "a/b" ) );
        appSettings.setValue( QStringLiteral( "geometry" ), QStringLiteral( "kept" ) );

        WHEN( "the settings are migrated" )
        {
            PersistentInfo::updateSettings( appSettings, sessionSettings );

            THEN( "the checks are gone, and the rest is kept" )
            {
                REQUIRE( appSettings.childGroups().count( QStringLiteral( "PinnedFilters" ) )
                         == 0 );
                REQUIRE( appSettings.value( QStringLiteral( "geometry" ) )
                         == QStringLiteral( "kept" ) );
            }
        }
    }
}

SCENARIO( "The search of the Filters tab keeps the checks of the filters it hides",
          "[filterspanel][search]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    GIVEN( "the filters Error and Debug checked" )
    {
        FiltersPanel panel;
        auto* tree = findTree( &panel );
        REQUIRE( tree != nullptr );
        invokeDoubleClick( &panel, findGroup( tree, "GroupA" )->child( 0 ) );
        findGroup( tree, "GroupB" )->child( 0 )->setCheckState( 0, Qt::Checked );
        QCoreApplication::processEvents();
        REQUIRE( countChecked( tree ) == 2 );

        QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );
        auto* searchBox = panel.findChild<QLineEdit*>();
        REQUIRE( searchBox != nullptr );

        WHEN( "the search shows Debug only" )
        {
            searchBox->setText( QStringLiteral( "debu" ) );
            searchBox->setText( QStringLiteral( "debug" ) );
            QCoreApplication::processEvents();

            THEN( "the Search is not handed the same filters again, key by key" )
            {
                // #928: every key re-set the Search pattern, and with the
                // Search run on a change of it restarted the Search.
                REQUIRE( spy.isEmpty() );
                REQUIRE( countChecked( tree ) == 2 );
            }

            AND_WHEN( "Debug, shown, is unchecked" )
            {
                findGroup( tree, "GroupB" )->child( 0 )->setCheckState( 0, Qt::Unchecked );
                QCoreApplication::processEvents();

                THEN( "the Search is told at once of Error, which the search hides" )
                {
                    REQUIRE( spy.count() == 1 );
                    const auto filters = spy.last().at( 0 ).value<QList<PredefinedFilter>>();
                    REQUIRE( filters.size() == 1 );
                    REQUIRE( filters[ 0 ].name == "Error" );
                }

                AND_WHEN( "it is checked again" )
                {
                    findGroup( tree, "GroupB" )->child( 0 )->setCheckState( 0, Qt::Checked );
                    QCoreApplication::processEvents();

                    THEN( "the Search is told at once of both" )
                    {
                        REQUIRE( spy.count() == 2 );
                        REQUIRE( spy.last().at( 0 ).value<QList<PredefinedFilter>>().size() == 2 );
                    }
                }
            }

            AND_WHEN( "Deselect All is clicked and the search is cleared" )
            {
                for ( auto* button : panel.findChildren<QPushButton*>() ) {
                    if ( button->text() == QStringLiteral( "Deselect All" ) ) {
                        button->click();
                    }
                }
                searchBox->clear();

                THEN( "only the filter shown was unchecked: Error is checked again" )
                {
                    REQUIRE( countChecked( tree ) == 1 );
                    REQUIRE( findGroup( tree, "GroupA" )->child( 0 )->checkState( 0 )
                             == Qt::Checked );
                }
            }
        }
    }
}

// The action of a context menu with this text, nullptr when there is none.
static QAction* menuAction( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

SCENARIO( "The Filters tab shows only one filter from its context menu and restores the checks",
          "[filterspanel][soloselection]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    GIVEN( "the filters of GroupA checked" )
    {
        FiltersPanel panel;
        auto* tree = findTree( &panel );
        REQUIRE( tree != nullptr );
        auto* groupA = findGroup( tree, "GroupA" );
        auto* groupB = findGroup( tree, "GroupB" );
        groupA->setCheckState( 0, Qt::Checked );
        QCoreApplication::processEvents();
        REQUIRE( countChecked( tree ) == 3 );

        auto* solo = panel.findChild<SoloSelection*>();
        REQUIRE( solo != nullptr );
        QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );
        const auto showOnlyThis = QStringLiteral( "Show only this" );
        const auto restorePrevious = QStringLiteral( "Restore previous selection" );

        THEN( "the tool tips of the filters and the groups name the double-click" )
        {
            const auto hint = SoloSelection::doubleClickHint();
            REQUIRE( groupB->child( 1 )->toolTip( 0 ).contains( hint ) );
            REQUIRE( groupB->child( 1 )->toolTip( 0 ).contains( QStringLiteral( "TRACE" ) ) );
            REQUIRE( groupA->toolTip( 0 ).contains( hint ) );
        }

        THEN( "the context menu of a filter offers Show only this, and Restore disabled" )
        {
            const auto menu = solo->contextMenu( groupB->child( 1 ) );
            REQUIRE( menuAction( *menu, showOnlyThis ) != nullptr );
            REQUIRE( menuAction( *menu, restorePrevious ) != nullptr );
            REQUIRE_FALSE( menuAction( *menu, restorePrevious )->isEnabled() );
        }

        WHEN( "Show only this of Trace's context menu is triggered" )
        {
            menuAction( *solo->contextMenu( groupB->child( 1 ) ), showOnlyThis )->trigger();

            THEN( "only Trace is checked and active, as by the double-click" )
            {
                REQUIRE( countChecked( tree ) == 1 );
                REQUIRE( groupB->child( 1 )->checkState( 0 ) == Qt::Checked );
                REQUIRE( spy.count() == 1 );
                const auto filters = spy.last().at( 0 ).value<QList<PredefinedFilter>>();
                REQUIRE( filters.size() == 1 );
                REQUIRE( filters[ 0 ].name == "Trace" );
            }

            AND_WHEN( "Restore previous selection is triggered" )
            {
                const auto menu = solo->contextMenu( groupB->child( 0 ) );
                REQUIRE( menuAction( *menu, restorePrevious )->isEnabled() );
                menuAction( *menu, restorePrevious )->trigger();

                THEN( "the filters of GroupA are checked and active again" )
                {
                    REQUIRE( countChecked( tree ) == 3 );
                    REQUIRE( groupA->checkState( 0 ) == Qt::Checked );
                    REQUIRE( spy.last().at( 0 ).value<QList<PredefinedFilter>>().size() == 3 );
                    REQUIRE_FALSE( solo->canRestore() );
                }
            }
        }

        WHEN( "a filter is double-clicked and the search changes before the restore" )
        {
            invokeDoubleClick( &panel, groupB->child( 0 ) );
            panel.findChild<QLineEdit*>()->setText( QStringLiteral( "trace" ) );
            REQUIRE( solo->canRestore() );
            solo->restore();
            panel.findChild<QLineEdit*>()->clear();

            THEN( "the checks from before the double-click are back" )
            {
                tree = findTree( &panel );
                REQUIRE( countChecked( tree ) == 3 );
                REQUIRE( findGroup( tree, "GroupA" )->checkState( 0 ) == Qt::Checked );
            }
        }
    }
}

SCENARIO( "Refreshing the Filters tab tells the Search of the checked filters only when there are "
          "some",
          "[filterspanel][refresh]" )
{
    // #914: a refresh with no filter checked replaced the Search pattern with
    // an empty one.
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    FiltersPanel panel;
    auto* tree = findTree( &panel );
    REQUIRE( tree != nullptr );

    GIVEN( "no filter checked" )
    {
        REQUIRE( countChecked( tree ) == 0 );
        QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );

        WHEN( "the panel is refreshed" )
        {
            panel.refreshFilters();

            THEN( "filtersChanged is not emitted" )
            {
                REQUIRE( spy.isEmpty() );
            }
        }
    }

    GIVEN( "a filter checked" )
    {
        auto* groupA = findGroup( tree, "GroupA" );
        REQUIRE( groupA != nullptr );
        invokeDoubleClick( &panel, groupA->child( 0 ) ); // "Error"
        QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );

        WHEN( "the panel is refreshed with the filter unchanged" )
        {
            panel.refreshFilters();

            THEN( "the Search is not told: it already is what the check built (#985)" )
            {
                REQUIRE( spy.isEmpty() );
                REQUIRE( countChecked( findTree( &panel ) ) == 1 );
            }
        }

        WHEN( "its pattern was changed" )
        {
            auto& collection = PredefinedFiltersCollection::getSynced();
            auto sets = collection.filterSets();
            sets[ groupIndex( sets, "GroupA" ) ].setFilters(
                { { "Error", "FATAL|ERROR", true }, { "Warning", "WARN", true } } );
            collection.setFilterSets( sets );
            collection.save();
            panel.refreshFilters();

            THEN( "filtersChanged is emitted with the new pattern" )
            {
                REQUIRE( spy.count() == 1 );
                const auto filters = spy.at( 0 ).at( 0 ).value<QList<PredefinedFilter>>();
                REQUIRE( filters.size() == 1 );
                REQUIRE( filters[ 0 ].pattern == "FATAL|ERROR" );
            }
        }

        WHEN( "it was deleted" )
        {
            auto& collection = PredefinedFiltersCollection::getSynced();
            auto sets = collection.filterSets();
            sets[ groupIndex( sets, "GroupA" ) ].setFilters( { { "Warning", "WARN", true } } );
            collection.setFilterSets( sets );
            collection.save();
            panel.refreshFilters();

            THEN( "filtersChanged is emitted without it" )
            {
                REQUIRE( spy.count() == 1 );
                REQUIRE( spy.at( 0 ).at( 0 ).value<QList<PredefinedFilter>>().isEmpty() );
                REQUIRE( panel.checkedKeys().isEmpty() );
            }
        }
    }
}

// A restored tab's checks may be of a Plugin's or a Team group, which the
// panel gets only after the restore: they check nothing until it comes, and
// then are checked without a Search, which the restore already ran (#985).
SCENARIO( "Checks of a group that comes after them are checked when it comes, without a Search",
          "[filterspanel][refresh][session]" )
{
    FiltersPanelTestStateGuard stateGuard;
    setupTestFilters();

    auto lateGroup = PredefinedFilterSet::createNewSet( "Late" );
    lateGroup.setFilters( { { "Late", "LATE", false } } );
    const auto lateKey = lateGroup.id() + QStringLiteral( "/Late" );

    FiltersPanel panel;
    const auto sets = PredefinedFiltersCollection::getSynced().filterSets();
    const auto errorKey = sets[ groupIndex( sets, "GroupA" ) ].id() + QStringLiteral( "/Error" );
    panel.showChecks( { errorKey, lateKey } );
    QCoreApplication::processEvents();
    REQUIRE( panel.checkedKeys() == QStringList{ errorKey } );

    QSignalSpy spy( &panel, &FiltersPanel::filtersChanged );

    WHEN( "the group comes" )
    {
        panel.setTeamGroups( { lateGroup } );
        QCoreApplication::processEvents();

        THEN( "both are checked, and the Search is not told" )
        {
            REQUIRE( spy.isEmpty() );
            auto checked = panel.checkedKeys();
            checked.sort();
            auto expected = QStringList{ errorKey, lateKey };
            expected.sort();
            REQUIRE( checked == expected );
        }
    }
}
