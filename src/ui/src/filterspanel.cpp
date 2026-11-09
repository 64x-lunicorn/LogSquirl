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

#include "filterspanel.h"

#include <QSettings>
#include <QTimer>

#include "containers.h"
#include "persistentinfo.h"

static constexpr const char* PinnedSettingsKey = "PinnedFilters";

// Composite key for pinning: "groupId/filterName".
static QString pinKey( const QString& groupId, const QString& filterName )
{
    return groupId + QStringLiteral( "/" ) + filterName;
}

FiltersPanel::FiltersPanel( QWidget* parent )
    : SidebarTab( tr( "Search filters..." ), parent )
{
    // A check the user changed: the Search's filters follow, and are saved.
    connect( this, &SidebarTab::checksChanged, this, &FiltersPanel::emitCurrentSelection );
    // The search shows other filters: the Search is told the checked ones
    // again, as when the search built the tree anew (beta2).
    connect( this, &SidebarTab::searchChanged, this, [ this ] {
        if ( !pinnedFilterKeys_.isEmpty() ) {
            emitCurrentSelection();
        }
    } );
    connect( this, &SidebarTab::editRequested, this, &FiltersPanel::editFiltersRequested );

    // Debounce timer for persisting pinned filters — avoids synchronous disk
    // writes on every checkbox click.
    saveTimer_ = new QTimer( this );
    saveTimer_->setSingleShot( true );
    saveTimer_->setInterval( 500 );
    connect( saveTimer_, &QTimer::timeout, this, &FiltersPanel::savePinnedFiltersNow );

    loadPinnedFilters();
    refreshFilters();
}

void FiltersPanel::showEvent( QShowEvent* event )
{
    SidebarTab::showEvent( event );
    if ( filtersDirty_ ) {
        refreshFilters();
    }
}

void FiltersPanel::refreshFilters()
{
    auto& collection = PredefinedFiltersCollection::getSynced();
    allFilterSets_ = collection.filterSets();
    pluginFilterGroups_ = collection.pluginFilterGroups();
    for ( const auto& group : logsquirl::as_const( pluginFilterGroups_ ) ) {
        allFilterSets_.append( group.group );
    }
    allFilterSets_.append( teamFilterSets_ );
    rebuildFilterIndex();
    setGroups( shownGroups() );

    // Emit current selection if any pinned filters were auto-checked, or
    // one that was has gone. With none checked before or now, the Search is
    // left as it is: refilling the tree after a filter was added replaced it
    // with an empty one (#897).
    if ( !pinnedFilterKeys_.isEmpty() ) {
        emitCurrentSelection();
    }
    filtersDirty_ = false;
}

void FiltersPanel::setTeamGroups( const QList<PredefinedFilterSet>& groups )
{
    teamFilterSets_ = groups;
    refreshFilters();
}

QList<SidebarTab::Group> FiltersPanel::shownGroups() const
{
    QList<SidebarTab::Group> groups;
    groups.reserve( allFilterSets_.size() );

    // The plugins' groups come after the user's own, and the Team groups last.
    const auto firstTeamGroup = allFilterSets_.size() - teamFilterSets_.size();
    const auto firstPluginGroup = firstTeamGroup - pluginFilterGroups_.size();
    for ( qsizetype index = 0; index < allFilterSets_.size(); ++index ) {
        const auto& set = allFilterSets_[ index ];
        SidebarTab::Group group;
        group.name = set.name();
        if ( index >= firstPluginGroup && index < firstTeamGroup ) {
            group.kind = SidebarTab::Group::Kind::Plugin;
            group.pluginName = pluginFilterGroups_[ index - firstPluginGroup ].pluginName;
        }
        else if ( index >= firstTeamGroup ) {
            group.kind = SidebarTab::Group::Kind::Team;
        }
        // A Filter Group without filters is no check of its own: no key.
        for ( const auto& filter : set.filters() ) {
            const auto key = pinKey( set.id(), filter.name );
            group.entries.append( { .key = key,
                                    .name = filter.name,
                                    .pattern = filter.pattern,
                                    .checked = pinnedFilterKeys_.contains( key ) } );
        }
        groups.append( std::move( group ) );
    }
    return groups;
}

void FiltersPanel::rebuildFilterIndex()
{
    filterIndex_.clear();
    filterIndex_.reserve( 64 );
    for ( const auto& set : allFilterSets_ ) {
        for ( const auto& filter : set.filters() ) {
            filterIndex_.insert( pinKey( set.id(), filter.name ), filter );
        }
    }
}

void FiltersPanel::emitCurrentSelection()
{
    QList<PredefinedFilter> selected;
    QSet<QString> checkedKeys;

    for ( const auto& key : SidebarTab::checkedKeys() ) {
        checkedKeys.insert( key );

        // O(1) lookup via pre-built index.
        const auto it = filterIndex_.constFind( key );
        if ( it != filterIndex_.constEnd() ) {
            selected.append( it.value() );
        }
    }

    pinnedFilterKeys_ = std::move( checkedKeys );
    savePinnedFilters();

    Q_EMIT filtersChanged( selected );
}
void FiltersPanel::savePinnedFilters()
{
    // Schedule a debounced write instead of flushing to disk immediately.
    saveTimer_->start();
}

void FiltersPanel::flushPendingSaves()
{
    if ( saveTimer_->isActive() ) {
        saveTimer_->stop();
        savePinnedFiltersNow();
    }
}

void FiltersPanel::savePinnedFiltersNow()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( PinnedSettingsKey );
    settings.remove( "" );
    settings.setValue( "count", pinnedFilterKeys_.size() );

    int index = 0;
    for ( const auto& key : pinnedFilterKeys_ ) {
        settings.setValue( QString( "filter%1" ).arg( index ), key );
        index++;
    }

    settings.endGroup();
    settings.sync();
}

void FiltersPanel::loadPinnedFilters()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( PinnedSettingsKey );

    const auto count = settings.value( "count", 0 ).toInt();
    for ( int i = 0; i < count; ++i ) {
        const auto key = settings.value( QString( "filter%1" ).arg( i ) ).toString();
        if ( !key.isEmpty() ) {
            pinnedFilterKeys_.insert( key );
        }
    }

    settings.endGroup();
}
