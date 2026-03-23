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

#include <QWidget>

#include "predefinedfilters.h"
#include "ui_predefinedfiltersetedit.h"

// Widget for editing a single PredefinedFilterSet (group name + filter table).
// Mirrors HighlighterSetEdit but uses a table for filter editing.
class PredefinedFilterSetEdit : public QWidget, public Ui::PredefinedFilterSetEdit {
    Q_OBJECT

  public:
    explicit PredefinedFilterSetEdit( QWidget* parent = nullptr );

    // Return the currently edited filter set.
    PredefinedFilterSet filterSet() const;

    // Populate the editor with the given set.
    void setFilterSet( PredefinedFilterSet set );

    // Clear all fields and disable editing controls.
    void reset();

  Q_SIGNALS:
    // Emitted whenever the set name or any filter changes.
    void changed();

  private Q_SLOTS:
    void setName( const QString& name );

    void addFilter();
    void removeFilter();

    void moveFilterUp();
    void moveFilterDown();

    void onCurrentCellChanged( int currentRow, int currentColumn, int previousRow,
                               int previousColumn );
    void onCellChanged( int row, int column );

  private:
    void populateTable();
    void syncTableToSet();
    void updateButtons( int currentRow );

    PredefinedFilterSet filterSet_;
    bool updatingTable_{ false };
};
