/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PREDEFINEDFILTERSDIALOG_H_
#define PREDEFINEDFILTERSDIALOG_H_

#include <QDialog>
#include <QLabel>
#include <QList>
#include <QListWidget>

#include "predefinedfilters.h"
#include "predefinedfiltersetedit.h"
#include "ui_predefinedfiltersdialog.h"

// Dialog for managing predefined filter groups.
// Mirrors HighlightersDialog: left panel = group list, right panel = group editor.
class PredefinedFiltersDialog : public QDialog, public Ui::PredefinedFiltersDialog {
    Q_OBJECT

public:
    explicit PredefinedFiltersDialog( QWidget* parent = nullptr );
    PredefinedFiltersDialog( const QString& newFilter, QWidget* parent = nullptr );

    // Shows the Team groups in a section of their own below the user's own
    // groups, in the order given (alphabetical, as the Team Folder hands them
    // over). They can be looked at and exported, not changed: this dialog
    // writes only the user's own groups. Without a call there is no section.
    void showTeamGroups( const QList<PredefinedFilterSet>& groups );

Q_SIGNALS:
    void optionsChanged();

private Q_SLOTS:
    void addFilterSet();
    void removeFilterSet();

    void moveFilterSetUp();
    void moveFilterSetDown();

    void exportFilters();
    void importFilters();

    void resolveDialog( QAbstractButton* button );

    // Sync the embedded editor with the selected group.
    void updatePropertyFields();

    // Write changes from the embedded editor back to the selected group.
    void updateFilterSetProperties();

    // Shows the selected Team group, read-only.
    void showSelectedTeamGroup();

private:
    void populateSetList();
    void setCurrentRow( int row );
    void loadIcons();

    PredefinedFilterSetEdit* filterSetEdit_;

    // Temporary copy of the collection, committed on Apply/OK.
    QList<PredefinedFilterSet> filterSets_;

    int selectedRow_;

    QLabel* teamGroupsLabel_ = nullptr;
    QListWidget* teamGroupsList_ = nullptr;
    QList<PredefinedFilterSet> teamGroups_;
    // The row of the Team group shown, -1 when none is.
    int selectedTeamRow_ = -1;
};

#endif