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

#include "valuenamespanel.h"

#include <QSet>

#include <utility>

#include "valuenamescollection.h"

ValueNamesPanel::ValueNamesPanel( QWidget* parent )
    : SidebarTab( tr( "Search Value Names..." ), parent )
{
    setEmptyHint( tr( "No Naming Groups yet: Edit... adds one." ) );
    connect( this, &SidebarTab::checksChanged, this, &ValueNamesPanel::commitChecks );

    refresh();
}

void ValueNamesPanel::refresh()
{
    const auto& collection = ValueNamesCollection::get();
    if ( collection.generation() == shownGeneration_ ) {
        return;
    }
    // A check changed and not yet written is overtaken by what the
    // collection holds now.
    setGroups( shownGroups() );
    shownGeneration_ = collection.generation();
}

QList<SidebarTab::Group> ValueNamesPanel::shownGroups()
{
    const auto& collection = ValueNamesCollection::get();
    const auto& namingGroups = collection.groups();

    QList<SidebarTab::Group> groups;
    groups.reserve( namingGroups.size() );

    // The Team groups come last, after the user's own.
    const auto firstTeamGroup = namingGroups.size() - collection.teamGroupCount();
    for ( qsizetype index = 0; index < namingGroups.size(); ++index ) {
        const auto& namingGroup = namingGroups[ index ];
        SidebarTab::Group group;
        group.name = namingGroup.name();
        if ( index >= firstTeamGroup ) {
            group.kind = SidebarTab::Group::Kind::Team;
        }
        // A group without rules has a check of its own; that of a group with
        // rules is theirs.
        group.key = ValueNamesCollection::groupCheckKey( namingGroup.id() );
        group.checked = namingGroup.isEnabled();
        for ( const auto& rule : namingGroup.rules() ) {
            group.entries.append(
                { .key = ValueNamesCollection::ruleCheckKey( namingGroup.id(), rule.name ),
                  .name = rule.name,
                  .pattern = rule.pattern,
                  .checked = namingGroup.isEnabled() && rule.enabled } );
        }
        groups.append( std::move( group ) );
    }
    return groups;
}

void ValueNamesPanel::commitChecks()
{
    const auto uncheckedKeys = SidebarTab::uncheckedKeys();
    const QSet<QString> unchecked( uncheckedKeys.cbegin(), uncheckedKeys.cend() );

    auto& collection = ValueNamesCollection::get();
    if ( !collection.setUncheckedKeys( unchecked ) ) {
        return;
    }
    collection.saveChecks();
    // The tab shows what the collection holds: refresh() leaves it alone.
    shownGeneration_ = collection.generation();
    Q_EMIT valueNamesChanged();
}
