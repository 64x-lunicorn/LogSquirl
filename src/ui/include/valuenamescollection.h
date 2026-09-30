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

#include "naminggroup.h"
#include "valuenamer.h"

// The Naming Groups of Value Names with their checks, held once per process
// as the Highlighter Set Collection is, and the Value Namer built from them
// that every Text View and Filtered View names values with (#647).
//
// The checks are the groups' and the rules' enabled flags
// (NamingGroup::isEnabled(), NamingRule::enabled): global for every tab, as
// the Predefined Filters' checks are. Only the enabled rules of the enabled
// groups are in the namer.
//
// Whoever changes the groups tells the Session: Changed::ValueNames. The
// views read the namer when they read a Log Line, and the generation tells
// them whether what they named with an older one is stale.
class ValueNamesCollection {
public:
    // The one of this process.
    static ValueNamesCollection& get();

    // The Naming Groups in the order the sidebar shows them, Team groups
    // last, with their checks. An earlier rule wins where two overlap.
    const QList<logsquirl::valuenames::NamingGroup>& groups() const
    {
        return groups_;
    }

    // Replaces the groups and builds the namer again. Returns whether they
    // differ from those held, checks included; only then does the namer
    // change and the generation grow.
    bool setGroups( QList<logsquirl::valuenames::NamingGroup> groups );

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

private:
    QList<logsquirl::valuenames::NamingGroup> groups_;
    logsquirl::valuenames::ValueNamer namer_;
    uint64_t generation_ = 1;
};
