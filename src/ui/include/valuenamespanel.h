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

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// The sidebar's "Value Names" tab (#647), built like the Filters Panel: the
// Naming Groups as a tree, each with a check per Naming Rule. The checks
// are those of ValueNamesCollection: global for every tab, and kept across
// restarts. Team groups are listed last.
//
// A check changed here is written to the collection and stored at once;
// valueNamesChanged() then asks for the views to be told
// (Changed::ValueNames). refresh() shows what the collection holds now.
class ValueNamesPanel : public QWidget {
    Q_OBJECT

public:
    explicit ValueNamesPanel( QWidget* parent = nullptr );

    // Shows the groups and checks the collection holds now, unless this panel
    // shows them already.
    void refresh();

Q_SIGNALS:
    // The checks of the collection were changed here: the views are to be told.
    void valueNamesChanged();

    // "Edit..." was clicked.
    void editRequested();

private Q_SLOTS:
    void onItemChanged( QTreeWidgetItem* item, int column );
    void onItemDoubleClicked( QTreeWidgetItem* item, int column );
    void onSearchTextChanged( const QString& text );
    void selectAll();
    void deselectAll();

private:
    void populateTree();
    void applySearch();
    // Writes the checks of the tree to the collection.
    void commitChecks();
    // Checks or unchecks what the search shows.
    void setShownChecked( bool checked );

    QLineEdit* searchBox_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* deselectAllButton_ = nullptr;
    QPushButton* editButton_ = nullptr;

    // Gathers the itemChanged() a group toggle gives for every rule into one
    // commit.
    QTimer* commitTimer_ = nullptr;

    // The collection's generation the tree shows; 0 before it shows any.
    uint64_t shownGeneration_ = 0;
    bool updatingTree_ = false;
};
