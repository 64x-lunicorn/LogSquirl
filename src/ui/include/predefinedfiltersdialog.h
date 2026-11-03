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
#include <QList>

#include "grouplist.h"
#include "plugingroup.h"
#include "predefinedfilters.h"
#include "predefinedfiltersetedit.h"
#include "teamfolder.h"
#include "teamgroupedits.h"
#include "ui_predefinedfiltersdialog.h"

// Dialog for managing predefined filter groups.
// Mirrors HighlightersDialog: left panel = the Group List, right panel = group editor.
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

    // What Test... for a filter opens the Regex Lab with: the Search's engine
    // and the sample of the tab in front (#660). Without a call, pasted text
    // only.
    void setRegexLabAccess( RegexLabAccess access );

    // The list of the user's own, the Team and the plugins' groups.
    GroupList* groupList() const
    {
        return groupList_;
    }

Q_SIGNALS:
    void optionsChanged();
    // Team groups were added, renamed or changed, and OK or Apply asks for them
    // to be published.
    void publishRequested( const QList<logsquirl::teamfolder::PublishRequest>& requests );

private Q_SLOTS:
    void resolveDialog( QAbstractButton* button );

    // Shows the group selected in the Group List.
    void showGroup( GroupList::Section section, int row );

    // Write changes from the embedded editor back to the selected group.
    void updateFilterSetProperties();

    // What the user asked for in the Group List.
    void addGroup( GroupList::Section section );
    void deleteGroup( GroupList::Section section, int row );
    void moveGroup( int row, int offset );
    void shareGroup( int row );
    void copyGroup( GroupList::Section section, int row );
    void exportFilters();
    void importFilters();

private:
    // Lists the user's own groups; the Default group cannot be deleted.
    void listOwnGroups();

    GroupList* groupList_;
    PredefinedFilterSetEdit* filterSetEdit_;

    // Temporary copy of the collection, committed on Apply/OK.
    QList<PredefinedFilterSet> filterSets_;

    // The row of the group of the user's own shown, -1 when none is.
    int selectedRow_ = -1;

    // The Team groups, below the user's own: listed when the dialog is first
    // given them.
    TeamGroupEdits<PredefinedFilterSet> teamEdits_;
    // The row of the Team group shown, -1 when none is.
    int selectedTeamRow_ = -1;

    // The groups the enabled plugins ship, read-only below the user's own
    // (#865): as the Predefined Filters Collection held them when the dialog
    // opened. No section when there are none.
    QList<PluginGroup<PredefinedFilterSet>> pluginGroups_;
    // The row of the plugin's group shown, -1 when none is.
    int selectedPluginRow_ = -1;
};

#endif