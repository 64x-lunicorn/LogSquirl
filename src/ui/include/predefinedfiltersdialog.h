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
#include <QPushButton>

#include "predefinedfilters.h"
#include "predefinedfiltersetedit.h"
#include "teamfolder.h"
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
    // over). Read-only when editable is false; otherwise they are edited like
    // the user's own groups, a new one can be added, and OK or Apply publishes
    // what changed through publishRequested. Without a call there is no
    // section.
    void showTeamGroups( const QList<PredefinedFilterSet>& groups, bool editable = false,
                         const QHash<QString, QString>& revisions = {} );

    // The revisions of the files of the groups with these ids are now those
    // given: what a publish of them made. The next publish of them is based
    // on these.
    void updateTeamRevisions( const QStringList& ids, const QHash<QString, QString>& revisions );

Q_SIGNALS:
    void optionsChanged();
    // Team groups were added, renamed or changed, and OK or Apply asks for them
    // to be published.
    void publishRequested( const QList<logsquirl::teamfolder::PublishRequest>& requests );

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
    void addTeamGroup();
    void shareSelectedGroup();
    void copySelectedTeamGroup();
    void deleteSelectedTeamGroup();

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
    // The Team groups as they were given, to tell what OK or Apply publishes.
    QList<PredefinedFilterSet> teamGroupsAsGiven_;
    QPushButton* teamAddButton_ = nullptr;
    QPushButton* teamShareButton_ = nullptr;
    QPushButton* teamCopyButton_ = nullptr;
    QPushButton* teamDeleteButton_ = nullptr;
    // What can be done with the selected group depends on which one it is.
    void updateTeamButtons();
    bool teamEditable_ = false;
    // The revision of each Team group's file when it was loaded, by id.
    QHash<QString, QString> teamRevisions_;
    // The row of the Team group shown, -1 when none is.
    int selectedTeamRow_ = -1;
};

#endif