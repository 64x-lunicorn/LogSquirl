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

#include <QDialog>

class QTableWidget;
class QPushButton;

// Dialog for managing tab groups: rename, recolor, and delete operations.
// Opened from Tools → "Manage Tab Groups…".
class TabGroupManagerDialog : public QDialog {
    Q_OBJECT

  public:
    explicit TabGroupManagerDialog( QWidget* parent = nullptr );

  private Q_SLOTS:
    // Renames the selected group via QInputDialog.
    void renameSelectedGroup();

    // Changes the colour of the selected group via QColorDialog.
    void changeSelectedGroupColor();

    // Deletes the selected group after confirmation.
    void deleteSelectedGroup();

  private:
    // Rebuilds the table from the current TabGroupInfo state.
    void populateTable();

    // Returns the group id for the currently selected row, or empty string.
    QString selectedGroupId() const;

    // Updates button enabled state based on selection.
    void updateButtonStates();

    QTableWidget* table_ = nullptr;
    QPushButton* renameButton_ = nullptr;
    QPushButton* changeColorButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};
