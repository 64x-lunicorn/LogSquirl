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

// A plugin ships Predefined Filters and Highlighter Sets named in its manifest
// (#865): read for every enabled plugin once the plugins were discovered and
// whenever one is enabled or disabled in Plugin Management, shown with the
// plugin as their Source, read-only, and never stored with the user's own.

#include <catch2/catch_test_macros.hpp>

#include "applicationplugins.h"
#include "configuration.h"
#include "filterspanel.h"
#include "groupexchange.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersmenu.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "plugindialog.h"
#include "plugingroups.h"
#include "predefinedfilters.h"
#include "predefinedfiltersdialog.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <memory>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginMetadata;

namespace {

const auto GroupPluginId = QStringLiteral( "io.github.logsquirl.test.group-plugin" );
const auto GroupPluginName = QStringLiteral( "Group Plugin" );
const auto CrashesShown = QStringLiteral( "Crashes (Group Plugin)" );
const auto LevelsShown = QStringLiteral( "Levels (Group Plugin)" );

void writeFile( const QString& path, const QByteArray& content )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( content );
}

PredefinedFilterSet crashesGroup()
{
    auto group = PredefinedFilterSet::createNewSet( "Crashes" );
    group.addFilter( { "Fatal", "FATAL EXCEPTION", false } );
    return group;
}

HighlighterSet levelsSet()
{
    auto set = HighlighterSet::createNewSet( "Levels" );
    set.addHighlighter( Highlighter( "ERROR", false, true, Qt::red, Qt::white ) );
    return set;
}

/// Installs the group plugin into root: the Log Lines plugin's library, whose
/// manifest names the given files, written by the dialogs' export, and the
/// given extra manifest members.
QString installGroupPlugin( const QString& root, const QString& members )
{
    const auto pluginDir = QDir( root ).filePath( "group-plugin" );
    writeFile( QDir( pluginDir ).filePath( "plugin.json" ),
               QStringLiteral( R"({
        "id": "%1",
        "name": "%2",
        "version": "1.0.0",
        "type": "ui",
        "library": "%3",
        "api_version": 1%4
    })" )
                   .arg( GroupPluginId, GroupPluginName,
                         QStringLiteral( LOGSQUIRL_TEST_LOG_LINES_PLUGIN_PATH ), members )
                   .toUtf8() );
    return pluginDir;
}

/// The group plugin shipping one Filter Group file and one Highlighter Set
/// file, each as the dialogs export them.
QString installShippingPlugin( const QString& root )
{
    const auto pluginDir
        = installGroupPlugin( root, R"(, "predefined_filters": [ "filters/crashes_filter.conf" ],
                  "highlighter_sets": [ "levels_highlighter.conf" ])" );
    REQUIRE( QDir().mkpath( QDir( pluginDir ).filePath( "filters" ) ) );
    REQUIRE( logsquirl::groupexchange::writeGroup(
        QDir( pluginDir ).filePath( "filters/crashes_filter.conf" ), crashesGroup() ) );
    REQUIRE( logsquirl::groupexchange::writeGroup(
        QDir( pluginDir ).filePath( "levels_highlighter.conf" ), levelsSet() ) );
    return pluginDir;
}

/// The enabled plugins stand in the configuration, where Plugin Management
/// writes them and the windows read them.
struct EnabledPluginsInConfiguration {
    explicit EnabledPluginsInConfiguration( const QStringList& enabled )
        : before( Configuration::get().enabledPlugins() )
    {
        Configuration::get().setEnabledPlugins( enabled );
    }
    ~EnabledPluginsInConfiguration()
    {
        Configuration::get().setEnabledPlugins( before );
    }
    EnabledPluginsInConfiguration( const EnabledPluginsInConfiguration& ) = delete;
    EnabledPluginsInConfiguration& operator=( const EnabledPluginsInConfiguration& ) = delete;

    QStringList before;
};

/// The plugin config directory the host creates goes to a test location, not the user's.
struct StandardPathsInTestMode {
    StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( true );
    }
    ~StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( false );
    }
    StandardPathsInTestMode( const StandardPathsInTestMode& ) = delete;
    StandardPathsInTestMode& operator=( const StandardPathsInTestMode& ) = delete;
};

/// The collections as a test case found them: the plugins' groups go, the
/// user's own come back. The sections of a case share the process.
struct KeepCollections {
    KeepCollections()
        : filterSets( PredefinedFiltersCollection::getSynced().filterSets() )
        , highlighterSets( HighlighterSetCollection::getSynced().highlighterSets() )
    {
    }
    ~KeepCollections()
    {
        auto& filters = PredefinedFiltersCollection::get();
        filters.setPluginFilterGroups( {} );
        filters.setFilterSets( filterSets );
        filters.save();
        auto& highlighters = HighlighterSetCollection::get();
        highlighters.deactivateAll();
        highlighters.setPluginHighlighterSets( {} );
        highlighters.setHighlighterSets( highlighterSets );
        highlighters.save();
    }
    KeepCollections( const KeepCollections& ) = delete;
    KeepCollections& operator=( const KeepCollections& ) = delete;

    QList<PredefinedFilterSet> filterSets;
    QList<HighlighterSet> highlighterSets;
};

std::shared_ptr<ApplicationPlugins> pluginsIn( const QString& root )
{
    return std::make_shared<ApplicationPlugins>(
        [ root ]( PluginCatalog& catalog, PluginHost& host ) {
            catalog.discoverPlugins( { root } );
            const auto errors
                = host.autoLoadPlugins(
                          { .autoLoad = true, .enabled = Configuration::get().enabledPlugins() } )
                      .errors;
            REQUIRE( errors.isEmpty() );
        } );
}

std::shared_ptr<Session> newSession()
{
    return std::make_shared<Session>( testSettingsPolicies(),
                                      std::make_shared<LogFormatCatalog>() );
}

/// The names of the groups the window's Filters panel lists.
QStringList filterPanelGroups( const MainWindow& window )
{
    auto* panel = window.findChild<FiltersPanel*>();
    REQUIRE( panel != nullptr );
    auto* tree = panel->findChild<QTreeWidget*>();
    REQUIRE( tree != nullptr );
    QStringList names;
    for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
        names.append( tree->topLevelItem( i )->text( 0 ) );
    }
    return names;
}

/// The action of the window's Highlighters menu with the given text, or nullptr.
QAction* highlightersAction( const MainWindow& window, const QString& text )
{
    auto* menu = window.findChild<HighlightersMenu*>();
    REQUIRE( menu != nullptr );
    for ( auto* action : menu->actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

/// The Enable or Disable button of the only plugin card in Plugin Management.
QPushButton* toggleButton( const PluginDialog& dialog )
{
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    const auto buttons = dialog.findChildren<QPushButton*>( "toggleButton" );
    REQUIRE( buttons.size() == 1 );
    return buttons.front();
}

bool colorsError()
{
    HighlightedMatchRanges ranges;
    return HighlighterSetCollection::get().currentActiveSet().matchLine( "an ERROR here", ranges )
           != HighlighterMatchType::NoMatch;
}

QListWidget* pluginList( QWidget& dialog )
{
    return dialog.findChild<QListWidget*>( "pluginGroupList" );
}

QPushButton* pluginCopyButton( QWidget& dialog )
{
    return dialog.findChild<QPushButton*>( "pluginCopy" );
}

/// The groups of the user's own, as the settings store holds them.
QList<PredefinedFilterSet> storedFilterGroups()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.sync();
    PredefinedFiltersCollection stored;
    stored.retrieveFromStorage( settings );
    return stored.filterSets();
}

QList<HighlighterSet> storedHighlighterSets()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.sync();
    HighlighterSetCollection stored;
    stored.retrieveFromStorage( settings );
    return stored.highlighterSets();
}

template <typename Group>
bool anyNamed( const QList<Group>& groups, const QString& name )
{
    return std::any_of( groups.cbegin(), groups.cend(),
                        [ &name ]( const auto& group ) { return group.name() == name; } );
}

template <typename Group>
bool anyFromPlugin( const QList<Group>& groups )
{
    return std::any_of( groups.cbegin(), groups.cend(), []( const auto& group ) {
        return group.id().startsWith( QStringLiteral( "plugin:" ) );
    } );
}

} // namespace

SCENARIO( "A plugin's Predefined Filters and Highlighter Sets are offered while it is enabled",
          "[ui][plugins][plugingroups]" )
{
    const StandardPathsInTestMode testPaths;
    const KeepCollections keep;
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    installShippingPlugin( root.path() );

    GIVEN( "a window, the plugin enabled, and a group and a set of the user's own of the same "
           "names" )
    {
        auto own = PredefinedFilterSet::createNewSet( "Crashes" );
        own.addFilter( { "Mine", "MINE", false } );
        auto& filters = PredefinedFiltersCollection::getSynced();
        filters.setFilterSets( { own } );
        filters.save();
        auto& highlighters = HighlighterSetCollection::getSynced();
        auto ownLevels = HighlighterSet::createNewSet( "Levels" );
        ownLevels.addHighlighter( Highlighter( "MINE", false, true, Qt::blue, Qt::white ) );
        highlighters.setHighlighterSets( { ownLevels } );
        highlighters.save();

        const EnabledPluginsInConfiguration enabled( { GroupPluginId } );
        auto plugins = pluginsIn( root.path() );
        const auto session = newSession();
        auto window = std::make_unique<MainWindow>( WindowSession{ session, "First", 0 }, plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
        QTest::qWait( 20 );

        THEN( "the Filters panel lists the plugin's group with its Source, beside the user's own" )
        {
            const auto groups = filterPanelGroups( *window );
            CHECK( groups.contains( CrashesShown ) );
            CHECK( groups.contains( "Crashes" ) );
            const auto plugin = PredefinedFiltersCollection::get().pluginFilterGroups();
            REQUIRE( plugin.size() == 1 );
            CHECK( plugin.front().pluginName == GroupPluginName );
            CHECK( plugin.front().group.filters().front().pattern == "FATAL EXCEPTION" );
            // The user's own group is untouched.
            REQUIRE( PredefinedFiltersCollection::get().filterSets().size() >= 1 );
            CHECK( anyNamed( PredefinedFiltersCollection::get().filterSets(), "Crashes" ) );
            CHECK_FALSE( anyFromPlugin( PredefinedFiltersCollection::get().filterSets() ) );
        }

        THEN( "the Highlighters menu lists the plugin's set with its Source, beside the user's "
              "own" )
        {
            CHECK( highlightersAction( *window, LevelsShown ) != nullptr );
            CHECK( highlightersAction( *window, "Levels" ) != nullptr );
            CHECK_FALSE( anyFromPlugin( HighlighterSetCollection::get().highlighterSets() ) );
        }

        WHEN( "the plugin's Predefined Filter is checked in the Filters panel" )
        {
            auto* panel = window->findChild<FiltersPanel*>();
            auto* tree = panel->findChild<QTreeWidget*>();
            QTreeWidgetItem* group = nullptr;
            for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
                if ( tree->topLevelItem( i )->text( 0 ) == CrashesShown ) {
                    group = tree->topLevelItem( i );
                }
            }
            REQUIRE( group != nullptr );
            REQUIRE( group->childCount() == 1 );
            QSignalSpy changed( panel, &FiltersPanel::filtersChanged );
            group->child( 0 )->setCheckState( 0, Qt::Checked );

            THEN( "it is applied" )
            {
                REQUIRE( changed.wait( 2000 ) );
                const auto selected = changed.last().at( 0 ).value<QList<PredefinedFilter>>();
                REQUIRE( selected.size() == 1 );
                CHECK( selected.front().pattern == "FATAL EXCEPTION" );
            }
        }

        WHEN( "the plugin's Highlighter Set is switched on in the Highlighters menu" )
        {
            auto* action = highlightersAction( *window, LevelsShown );
            REQUIRE( action != nullptr );
            action->trigger();

            THEN( "it colors the Log Lines" )
            {
                CHECK( HighlighterSetCollection::get().activeSetIds().size() == 1 );
                CHECK( colorsError() );
            }

            THEN( "the settings store holds that it is active, not the set" )
            {
                CHECK_FALSE( anyFromPlugin( storedHighlighterSets() ) );
                CHECK( storedHighlighterSets().size() == 1 );
            }

            AND_WHEN( "the plugin is disabled in Plugin Management" )
            {
                PluginDialog dialog( *plugins );
                REQUIRE( toggleButton( dialog )->text() == "Disable" );
                toggleButton( dialog )->click();
                REQUIRE_FALSE( Configuration::get().enabledPlugins().contains( GroupPluginId ) );

                THEN( "its group and set are gone, and the user's own of the same names stay" )
                {
                    CHECK_FALSE( filterPanelGroups( *window ).contains( CrashesShown ) );
                    CHECK( filterPanelGroups( *window ).contains( "Crashes" ) );
                    CHECK( highlightersAction( *window, LevelsShown ) == nullptr );
                    CHECK( highlightersAction( *window, "Levels" ) != nullptr );
                    CHECK( PredefinedFiltersCollection::get().pluginFilterGroups().isEmpty() );
                    CHECK( HighlighterSetCollection::get().pluginHighlighterSets().isEmpty() );
                }

                THEN( "no set is active any more, and nothing is colored" )
                {
                    CHECK( HighlighterSetCollection::get().activeSetIds().isEmpty() );
                    CHECK_FALSE( colorsError() );
                    auto* none = highlightersAction( *window, "None" );
                    REQUIRE( none != nullptr );
                    CHECK_FALSE( none->isEnabled() );
                }

                AND_WHEN( "it is enabled again" )
                {
                    REQUIRE( toggleButton( dialog )->text() == "Enable" );
                    toggleButton( dialog )->click();

                    THEN( "its group and set are back, the set not active" )
                    {
                        CHECK( filterPanelGroups( *window ).contains( CrashesShown ) );
                        auto* back = highlightersAction( *window, LevelsShown );
                        REQUIRE( back != nullptr );
                        CHECK_FALSE( back->isChecked() );
                        CHECK( HighlighterSetCollection::get().activeSetIds().isEmpty() );
                    }
                }
            }
        }

        THEN( "nothing of the plugin is in the user's saved groups and sets" )
        {
            filters.save();
            highlighters.save();
            CHECK_FALSE( anyFromPlugin( storedFilterGroups() ) );
            CHECK_FALSE( anyNamed( storedFilterGroups(), CrashesShown ) );
            CHECK_FALSE( anyFromPlugin( storedHighlighterSets() ) );
        }

        window.reset();
        plugins.reset();
        QTest::qWait( 20 );
    }
}

SCENARIO( "The dialogs show a plugin's groups read-only, and copy one into the user's own",
          "[ui][plugins][plugingroups]" )
{
    const KeepCollections keep;
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    installShippingPlugin( root.path() );
    PluginCatalog catalog;
    catalog.discoverPlugins( { root.path() } );
    const auto groups
        = logsquirl::plugingroups::readEnabled( catalog.discoveredPlugins(), { GroupPluginId } );
    REQUIRE( groups.filterGroups.size() == 1 );
    REQUIRE( groups.highlighterSets.size() == 1 );
    PredefinedFiltersCollection::get().setPluginFilterGroups( groups.filterGroups );
    HighlighterSetCollection::get().setPluginHighlighterSets( groups.highlighterSets );

    GIVEN( "the Predefined Filters dialog" )
    {
        PredefinedFiltersDialog dialog( QString{} );
        auto* list = pluginList( dialog );
        REQUIRE( list != nullptr );
        auto* copy = pluginCopyButton( dialog );
        REQUIRE( copy != nullptr );

        THEN( "it lists the plugin's group with its Source" )
        {
            REQUIRE( list->count() == 1 );
            CHECK( list->item( 0 )->text() == CrashesShown );
            CHECK_FALSE( copy->isEnabled() );
        }

        WHEN( "the plugin's group is selected and copied, and OK is pressed" )
        {
            list->setCurrentRow( 0 );
            auto* edit = dialog.findChild<PredefinedFilterSetEdit*>();
            REQUIRE( edit != nullptr );
            CHECK( edit->filterSet().name() == "Crashes" );
            REQUIRE( copy->isEnabled() );
            const auto own = dialog.setListWidget->count();
            copy->click();
            QCoreApplication::processEvents();
            CHECK( dialog.setListWidget->count() == own + 1 );
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
            QCoreApplication::processEvents();

            THEN( "the copy is the user's own, stored under an id of its own; the plugin's group "
                  "is not stored" )
            {
                const auto stored = storedFilterGroups();
                CHECK( anyNamed( stored, "Crashes" ) );
                CHECK_FALSE( anyFromPlugin( stored ) );
                CHECK( PredefinedFiltersCollection::get().pluginFilterGroups().size() == 1 );
            }
        }
    }

    GIVEN( "the Highlighters dialog" )
    {
        HighlightersDialog dialog;
        auto* list = pluginList( dialog );
        REQUIRE( list != nullptr );
        auto* copy = pluginCopyButton( dialog );
        REQUIRE( copy != nullptr );

        THEN( "it lists the plugin's set with its Source" )
        {
            REQUIRE( list->count() == 1 );
            CHECK( list->item( 0 )->text() == LevelsShown );
        }

        WHEN( "the plugin's set is selected" )
        {
            list->setCurrentRow( 0 );
            auto* edit = dialog.findChild<HighlighterSetEdit*>();
            REQUIRE( edit != nullptr );

            THEN( "it is shown, and cannot be changed" )
            {
                CHECK( edit->highlighters().name() == "Levels" );
                CHECK_FALSE( edit->isEnabled() );
            }

            AND_WHEN( "it is copied, and OK is pressed" )
            {
                REQUIRE( copy->isEnabled() );
                const auto own = dialog.highlighterListWidget->count();
                copy->click();
                QCoreApplication::processEvents();
                CHECK( dialog.highlighterListWidget->count() == own + 1 );
                dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
                QCoreApplication::processEvents();

                THEN( "the copy is the user's own; the plugin's set stays the plugin's and is not "
                      "stored" )
                {
                    const auto stored = storedHighlighterSets();
                    CHECK( anyNamed( stored, "Levels" ) );
                    CHECK_FALSE( anyFromPlugin( stored ) );
                    const auto pluginSets = HighlighterSetCollection::get().pluginHighlighterSets();
                    REQUIRE( pluginSets.size() == 1 );
                    CHECK( pluginSets.front().pluginName == GroupPluginName );
                }
            }
        }
    }
}

SCENARIO( "Whether a plugin's Highlighter Set is active is stored, the set is not",
          "[ui][plugins][plugingroups]" )
{
    const PluginGroup<HighlighterSet> levels{ levelsSet().withId( "plugin:p:levels" ),
                                              GroupPluginName };
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( "settings.ini" );

    GIVEN( "a collection with the plugin's set active, saved" )
    {
        {
            HighlighterSetCollection before;
            before.setPluginHighlighterSets( { levels } );
            before.activateSet( levels.group.id() );
            REQUIRE( before.activeSetIds() == QStringList{ levels.group.id() } );
            QSettings settings( path, QSettings::IniFormat );
            before.saveToStorage( settings );
        }

        WHEN( "the next start reads it, before the plugins are discovered" )
        {
            HighlighterSetCollection after;
            QSettings settings( path, QSettings::IniFormat );
            after.retrieveFromStorage( settings );

            THEN( "the set is not stored, its activation is" )
            {
                CHECK( after.highlighterSets().isEmpty() );
                CHECK( after.pluginHighlighterSets().isEmpty() );
                after.setPluginHighlighterSets( {}, false );
                CHECK( after.activeSetIds().contains( levels.group.id() ) );
            }

            THEN( "the plugins discovered with the set, it is active and colors" )
            {
                after.setPluginHighlighterSets( { levels } );
                CHECK( after.activeSetIds().contains( levels.group.id() ) );
                HighlightedMatchRanges ranges;
                CHECK( after.currentActiveSet().matchLine( "an ERROR here", ranges )
                       != HighlighterMatchType::NoMatch );
            }

            THEN( "the plugins discovered without the set, it is no longer active" )
            {
                CHECK( after.setPluginHighlighterSets( {} ) );
                CHECK( after.activeSetIds().isEmpty() );
            }
        }
    }
}

SCENARIO( "A plugin's group files are read only while the plugin is enabled",
          "[ui][plugins][plugingroups]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );

    GIVEN( "a plugin naming a fine, a missing and a broken file of each kind" )
    {
        const auto pluginDir = installGroupPlugin(
            root.path(),
            R"(, "predefined_filters": [ "crashes_filter.conf", "missing.conf", "levels_highlighter.conf" ],
                 "highlighter_sets": [ "levels_highlighter.conf", "missing.conf", "crashes_filter.conf" ])" );
        const auto crashes = crashesGroup();
        const auto levels = levelsSet();
        REQUIRE( logsquirl::groupexchange::writeGroup(
            QDir( pluginDir ).filePath( "crashes_filter.conf" ), crashes ) );
        REQUIRE( logsquirl::groupexchange::writeGroup(
            QDir( pluginDir ).filePath( "levels_highlighter.conf" ), levels ) );
        PluginCatalog catalog;
        catalog.discoverPlugins( { root.path() } );
        REQUIRE( catalog.discoveredPlugins().size() == 1 );

        THEN( "enabled, it ships the fine ones under ids of its own, with its name as Source" )
        {
            const auto groups = logsquirl::plugingroups::readEnabled( catalog.discoveredPlugins(),
                                                                      { GroupPluginId } );
            REQUIRE( groups.filterGroups.size() == 1 );
            REQUIRE( groups.highlighterSets.size() == 1 );
            using logsquirl::plugingroups::idOf;
            CHECK( groups.filterGroups.front().group.id() == idOf( GroupPluginId, crashes.id() ) );
            CHECK( groups.highlighterSets.front().group.id()
                   == idOf( GroupPluginId, levels.id() ) );
            CHECK( idOf( GroupPluginId, levels.id() ) != levels.id() );
            CHECK( groups.filterGroups.front().pluginName == GroupPluginName );
            CHECK( groups.highlighterSets.front().group.name() == "Levels" );
        }

        THEN( "disabled, it ships none" )
        {
            const auto groups
                = logsquirl::plugingroups::readEnabled( catalog.discoveredPlugins(), {} );
            CHECK( groups.filterGroups.isEmpty() );
            CHECK( groups.highlighterSets.isEmpty() );
        }
    }

    GIVEN( "a plugin without the keys" )
    {
        installGroupPlugin( root.path(), {} );
        PluginCatalog catalog;
        catalog.discoverPlugins( { root.path() } );
        REQUIRE( catalog.discoveredPlugins().size() == 1 );

        THEN( "enabled, it ships none" )
        {
            const auto groups = logsquirl::plugingroups::readEnabled( catalog.discoveredPlugins(),
                                                                      { GroupPluginId } );
            CHECK( groups.filterGroups.isEmpty() );
            CHECK( groups.highlighterSets.isEmpty() );
        }
    }

    GIVEN( "a plugin naming a file outside its directory" )
    {
        installGroupPlugin( root.path(), R"(, "highlighter_sets": [ "../levels.conf" ])" );
        PluginCatalog catalog;
        catalog.discoverPlugins( { root.path() } );

        THEN( "its manifest is invalid, and it is not listed at all" )
        {
            CHECK( catalog.discoveredPlugins().empty() );
        }
    }
}
