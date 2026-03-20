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

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>

#include "predefinedfilters.h"

// Widget that displays all predefined filters as a checklist.
// Users can check/uncheck filters, pin filters (persists across sessions),
// and search the list by name. Selected filters are emitted via signal.
class FiltersPanel : public QWidget {
    Q_OBJECT

  public:
    explicit FiltersPanel( QWidget* parent = nullptr );

    ~FiltersPanel() = default;
    FiltersPanel( const FiltersPanel& ) = delete;
    FiltersPanel& operator=( const FiltersPanel& ) = delete;

    // Reload filters from PredefinedFiltersCollection and re-populate the list.
    void refreshFilters();

  Q_SIGNALS:
    // Emitted when the set of checked (active) filters changes.
    void filtersChanged( const QList<PredefinedFilter>& selectedFilters );

  private Q_SLOTS:
    void onItemChanged( QListWidgetItem* item );
    void onSearchTextChanged( const QString& text );
    void selectAll();
    void deselectAll();

  protected:
    void showEvent( QShowEvent* event ) override;

  private:
    void populateList( const PredefinedFiltersCollection::Collection& filters );
    void emitCurrentSelection();
    void savePinnedFilters();
    void loadPinnedFilters();

    QLineEdit* searchBox_{ nullptr };
    QListWidget* filterList_{ nullptr };
    QPushButton* selectAllButton_{ nullptr };
    QPushButton* deselectAllButton_{ nullptr };

    PredefinedFiltersCollection::Collection allFilters_;
    QSet<QString> pinnedFilterNames_;

    bool updatingList_{ false };
};
