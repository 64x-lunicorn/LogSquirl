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
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "predefinedfilters.h"

// Widget that displays all predefined filters grouped into expandable tree items.
// Users can check/uncheck individual filters or entire groups (tri-state).
// Pinned filters persist across sessions. Selected filters are emitted via signal.
class FiltersPanel : public QWidget {
    Q_OBJECT

  public:
    explicit FiltersPanel( QWidget* parent = nullptr );

    ~FiltersPanel() = default;
    FiltersPanel( const FiltersPanel& ) = delete;
    FiltersPanel& operator=( const FiltersPanel& ) = delete;

    // Reload filter sets from PredefinedFiltersCollection and re-populate the tree.
    void refreshFilters();

    // Flush any pending debounced save to disk immediately.
    // Call before destroying the panel when the pinned state must be persisted,
    // or in tests to force a synchronous write.
    void flushPendingSaves();

  Q_SIGNALS:
    // Emitted when the set of checked (active) filters changes.
    void filtersChanged( const QList<PredefinedFilter>& selectedFilters );

    // Emitted when the user clicks the "Edit Filters" button.
    void editFiltersRequested();

  private Q_SLOTS:
    void onItemChanged( QTreeWidgetItem* item, int column );
    void onItemDoubleClicked( QTreeWidgetItem* item, int column );
    void onSearchTextChanged( const QString& text );
    void selectAll();
    void deselectAll();

  protected:
    void showEvent( QShowEvent* event ) override;
    void changeEvent( QEvent* event ) override;

  private:
    void populateTree( const QList<PredefinedFilterSet>& sets );
    void rebuildFilterIndex();
    void emitCurrentSelection();
    void savePinnedFilters();
    void savePinnedFiltersNow();
    void loadPinnedFilters();
    void applyCurrentPalette();

    QLineEdit* searchBox_{ nullptr };
    QTreeWidget* filterTree_{ nullptr };
    QPushButton* selectAllButton_{ nullptr };
    QPushButton* deselectAllButton_{ nullptr };
    QPushButton* editFiltersButton_{ nullptr };

    QList<PredefinedFilterSet> allFilterSets_;
    QSet<QString> pinnedFilterKeys_;

    // Fast lookup: pinKey(groupId, filterName) → PredefinedFilter.
    QHash<QString, PredefinedFilter> filterIndex_;

    // Debounce timer — batches rapid itemChanged signals (e.g. group toggle)
    // into a single emitCurrentSelection() call.
    QTimer* debounceTimer_{ nullptr };

    // Debounce timer for persisting pinned filters to disk.
    QTimer* saveTimer_{ nullptr };

    bool updatingTree_{ false };
    bool filtersDirty_{ true };
};
