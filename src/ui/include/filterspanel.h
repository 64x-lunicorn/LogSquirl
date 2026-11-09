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

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "plugingroup.h"
#include "predefinedfilters.h"
#include "sidebartab.h"

class QTimer;

// The sidebar's Filters tab: all Predefined Filters grouped into expandable
// tree items, built on the SidebarTab (#923). Users can check/uncheck
// individual filters or entire groups (tri-state). Checked ("pinned") filters
// persist across sessions. Selected filters are emitted via signal.
// A double-click on a filter or group, or "Show only this" of its context
// menu, checks only it; "Restore previous selection" brings back the checks
// from before (#889).
class FiltersPanel : public SidebarTab {
    Q_OBJECT

public:
    explicit FiltersPanel( QWidget* parent = nullptr );

    ~FiltersPanel() override = default;
    FiltersPanel( const FiltersPanel& ) = delete;
    FiltersPanel& operator=( const FiltersPanel& ) = delete;

    // Reload filter sets from PredefinedFiltersCollection and re-populate the tree.
    // The Filter Groups the enabled plugins ship, as the collection holds
    // them, are listed after the user's own, each with its Source, and are
    // usable like the user's own (#865).
    void refreshFilters();

    // The Team groups to list after the user's own groups and the plugins'
    // groups, alphabetically as
    // the Team Folder hands them over, and marked as Team groups. Usable like
    // the user's own: checked, pinned and searched the same way.
    void setTeamGroups( const QList<PredefinedFilterSet>& groups );

    // Flush any pending debounced save to disk immediately.
    // Call before destroying the panel when the pinned state must be persisted,
    // or in tests to force a synchronous write.
    void flushPendingSaves();

Q_SIGNALS:
    // Emitted when the set of checked (active) filters changes.
    void filtersChanged( const QList<PredefinedFilter>& selectedFilters );

    // Emitted when the user clicks the "Edit Filters" button.
    void editFiltersRequested();

private:
    // The groups of allFilterSets_ as the SidebarTab shows them, with the
    // pinned filters checked.
    QList<SidebarTab::Group> shownGroups() const;
    void rebuildFilterIndex();
    void emitCurrentSelection();
    void savePinnedFilters();
    void savePinnedFiltersNow();
    void loadPinnedFilters();

    QList<PredefinedFilterSet> allFilterSets_;
    QList<PredefinedFilterSet> teamFilterSets_;
    // The plugins' groups, between the user's own and the Team groups in
    // allFilterSets_: the Predefined Filters Collection holds them (#865).
    QList<PluginGroup<PredefinedFilterSet>> pluginFilterGroups_;
    QSet<QString> pinnedFilterKeys_;

    // Fast lookup: pinKey(groupId, filterName) → PredefinedFilter.
    QHash<QString, PredefinedFilter> filterIndex_;

    // Debounce timer for persisting pinned filters to disk.
    QTimer* saveTimer_{ nullptr };
};
