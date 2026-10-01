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

#pragma once

#include <cstdint>

#include <QList>
#include <QSet>
#include <QString>

#include "naminggroup.h"
#include "persistable.h"
#include "valuenamer.h"

class QSettings;

// The Naming Groups of Value Names with their checks, held once per process
// as the Highlighter Set Collection is, and the Value Namer built from them
// that every Text View and Filtered View names values with (#647).
//
// The checks are the groups' and the rules' enabled flags
// (NamingGroup::isEnabled(), NamingRule::enabled): global for every tab, as
// the Predefined Filters' checks are. Only the enabled rules of the enabled
// groups are in the namer.
//
// The groups are stored in the settings store under "ValueNamesCollection",
// without their checks. The checks are stored apart, under
// "ValueNamesChecks", as the Filters Panel's pinned filters are: as the keys
// of what is unchecked, by group id and rule name. What is new -- a group
// added, imported or shared by the team, a rule added -- is checked. A
// check toggle writes the checks only, not every Name Table again.
//
// Whoever changes the groups or the checks tells the Session:
// Changed::ValueNames. The views read the namer when they read a Log Line,
// and the generation tells them whether what they named with an older one is
// stale.
class ValueNamesCollection final : public Persistable<ValueNamesCollection> {
public:
    static const char* persistableName()
    {
        return "ValueNamesCollection";
    }

    // The Naming Groups in the order the sidebar shows them -- the user's own,
    // then the Team groups -- with their checks. An earlier rule wins where two
    // overlap.
    const QList<logsquirl::valuenames::NamingGroup>& groups() const
    {
        return groups_;
    }

    // The user's own groups, first in groups().
    const QList<logsquirl::valuenames::NamingGroup>& ownGroups() const
    {
        return ownGroups_;
    }

    // How many groups at the end of groups() are Team groups.
    qsizetype teamGroupCount() const
    {
        return teamGroups_.size();
    }

    // Replaces the user's own groups, with the checks they carry, and builds
    // the namer again. Returns whether groups() changed, checks included;
    // only then does the namer change and the generation grow. Not saved:
    // save() does that.
    bool setGroups( QList<logsquirl::valuenames::NamingGroup> groups );

    // Replaces the Team groups, listed after the user's own. Their checks are
    // the stored ones: a Team group's file holds none. Never saved here, the
    // Team Folder holds them.
    bool setTeamGroups( QList<logsquirl::valuenames::NamingGroup> groups );

    // The keys of what is unchecked, of groupCheckKey() and ruleCheckKey().
    const QSet<QString>& uncheckedKeys() const
    {
        return uncheckedKeys_;
    }

    // Unchecks what these keys name of the groups held and checks the rest of
    // them; the stored checks of groups not held now (a Team group not
    // synced yet) are kept. Returns whether groups() changed.
    bool setUncheckedKeys( const QSet<QString>& keys );

    // What a group's and a rule's checks are kept by: the group's id, and its
    // id and the rule's name. Rule names are unique within their group.
    static QString groupCheckKey( const QString& groupId );
    static QString ruleCheckKey( const QString& groupId, const QString& ruleName );

    // Built from the groups. Never changed once built: a copy may be taken
    // to another thread, as a save of the Log Lines shown does.
    const logsquirl::valuenames::ValueNamer& namer() const
    {
        return namer_;
    }

    // Grows with every change of the groups, from 1: what a view named with
    // an older namer is stale.
    uint64_t generation() const
    {
        return generation_;
    }

    // Writes the checks alone to the settings store.
    void saveChecks() const;

    // The user's own groups and the checks; read keeps the Team groups.
    void retrieveFromStorage( QSettings& settings );
    void saveToStorage( QSettings& settings ) const;

private:
    static constexpr int ValueNamesCollection_VERSION = 1;
    static constexpr int ValueNamesChecks_VERSION = 1;

    void saveChecksToStorage( QSettings& settings ) const;
    // Sets the checks of the groups from uncheckedKeys_.
    void applyChecks( QList<logsquirl::valuenames::NamingGroup>& groups ) const;
    // groups_ again from the own and the Team groups; whether it changed.
    bool rebuild();

    QList<logsquirl::valuenames::NamingGroup> ownGroups_;
    QList<logsquirl::valuenames::NamingGroup> teamGroups_;
    QList<logsquirl::valuenames::NamingGroup> groups_;
    QSet<QString> uncheckedKeys_;
    logsquirl::valuenames::ValueNamer namer_;
    uint64_t generation_ = 1;
};
