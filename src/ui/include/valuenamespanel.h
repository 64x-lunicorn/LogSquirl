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

#include "sidebartab.h"

// The sidebar's "Value Names" tab (#647), built on the SidebarTab like the
// Filters tab (#924): the Naming Groups as a tree, each with a check per
// Naming Rule, and a check of its own for a group without rules. The checks
// are those of ValueNamesCollection: global for every tab, and kept across
// restarts. Team groups are listed last.
//
// A check changed here is written to the collection and stored at once;
// valueNamesChanged() then asks for the views to be told
// (Changed::ValueNames). refresh() shows what the collection holds now.
// Edit... is told by SidebarTab::editRequested().
class ValueNamesPanel : public SidebarTab {
    Q_OBJECT

public:
    explicit ValueNamesPanel( QWidget* parent = nullptr );

    // Shows the groups and checks the collection holds now, unless this panel
    // shows them already.
    void refresh();

Q_SIGNALS:
    // The checks of the collection were changed here: the views are to be told.
    void valueNamesChanged();

private:
    // The collection's groups as the SidebarTab shows them, with their checks.
    static QList<SidebarTab::Group> shownGroups();
    // Writes the checks of the tab to the collection.
    void commitChecks();

    // The collection's generation the tab shows; 0 before it shows any.
    uint64_t shownGeneration_ = 0;
};
