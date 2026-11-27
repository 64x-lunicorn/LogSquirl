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

#include "containers.h"

// The key of a filter's check: "groupId/filterName".
static QString checkKey( const QString& groupId, const QString& filterName )
{
    return groupId + QStringLiteral( "/" ) + filterName;
}

FiltersPanel::FiltersPanel( QWidget* parent )
    : SidebarTab( tr( "Search filters..." ), parent )
{
    // A check the user changed: the Search's filters follow.
    // The search of the tab only shows other filters and keeps the checks:
    // the Search is not told again, which re-set its pattern on every key
    // (#928).
    connect( this, &SidebarTab::checksChanged, this, &FiltersPanel::emitCurrentSelection );
    connect( this, &SidebarTab::editRequested, this, &FiltersPanel::editFiltersRequested );

    refreshFilters();
}

void FiltersPanel::refreshFilters()
{
    // The checked filters the Search of the tab in front was built with.
    const auto checkedBefore = checkedFilters();

    auto& collection = PredefinedFiltersCollection::getSynced();
    allFilterSets_ = collection.filterSets();
    pluginFilterGroups_ = collection.pluginFilterGroups();
    for ( const auto& group : logsquirl::as_const( pluginFilterGroups_ ) ) {
        allFilterSets_.append( group.group );
    }
    allFilterSets_.append( teamFilterSets_ );
    rebuildFilterIndex();
    setGroups( shownGroups() );

    // The Search of the tab in front hears of its checked filters only when
    // one of them has changed or gone, never with none checked (#897). A
    // check whose Plugin group or Team group came only now was in the
    // restored Search already (#985). The other tabs hear nothing (#984).
    const auto checkedAfter = checkedFilters();
    for ( auto it = checkedBefore.cbegin(); it != checkedBefore.cend(); ++it ) {
        const auto after = checkedAfter.constFind( it.key() );
        if ( after == checkedAfter.cend() || !after->searchesAs( *it ) ) {
            emitCurrentSelection();
            return;
        }
    }
}

QHash<QString, PredefinedFilter> FiltersPanel::checkedFilters() const
{
    QHash<QString, PredefinedFilter> checked;
    for ( const auto& key : checkedFilterKeys_ ) {
        const auto it = filterIndex_.constFind( key );
        if ( it != filterIndex_.cend() ) {
            checked.insert( key, *it );
        }
    }
    return checked;
}

void FiltersPanel::showChecks( const QStringList& keys )
{
    checkedFilterKeys_ = QSet<QString>( keys.cbegin(), keys.cend() );
    setGroups( shownGroups() );
    // What Restore previous selection brought back was of the tab shown before.
    forgetPreviousSelection();
}

QStringList FiltersPanel::keptChecks() const
{
    QStringList keys( checkedFilterKeys_.cbegin(), checkedFilterKeys_.cend() );
    keys.sort();
    return keys;
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
            const auto key = checkKey( set.id(), filter.name );
            group.entries.append( { .key = key,
                                    .name = filter.name,
                                    .pattern = filter.pattern,
                                    .checked = checkedFilterKeys_.contains( key ) } );
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
            filterIndex_.insert( checkKey( set.id(), filter.name ), filter );
        }
    }
}

void FiltersPanel::emitCurrentSelection()
{
    QList<PredefinedFilter> selected;
    QSet<QString> checkedKeys;

    // The checks of filters the panel has not are kept: their Plugin group or
    // Team group may come later (#985).
    for ( const auto& key : logsquirl::as_const( checkedFilterKeys_ ) ) {
        if ( !filterIndex_.contains( key ) ) {
            checkedKeys.insert( key );
        }
    }
    for ( const auto& key : SidebarTab::checkedKeys() ) {
        checkedKeys.insert( key );

        // O(1) lookup via pre-built index.
        const auto it = filterIndex_.constFind( key );
        if ( it != filterIndex_.constEnd() ) {
            selected.append( it.value() );
        }
    }

    checkedFilterKeys_ = std::move( checkedKeys );

    Q_EMIT filtersChanged( selected );
}
