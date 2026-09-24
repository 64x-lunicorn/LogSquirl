/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

#ifndef FILTERSDIALOG_H
#define FILTERSDIALOG_H

#include <memory>
#include <vector>

#include <QDialog>
#include <QLabel>
#include <QList>
#include <QListWidget>

class QPushButton;

#include "highlighterset.h"
#include "highlightersetedit.h"
#include "ui_highlightersdialog.h"

class HighlightersDialog : public QDialog, public Ui::HighlightersDialog {
    Q_OBJECT

public:
    explicit HighlightersDialog( QWidget* parent = nullptr );

    // Shows the Team Highlighter Sets in a section of their own below the
    // user's own sets, in the order given (alphabetical, as the Team Folder
    // hands them over). They can be looked at and exported, not changed here.
    // Whether one is active is chosen in the Highlighters menu. Without a
    // call there is no section.
    void showTeamGroups( const QList<HighlighterSet>& groups );

Q_SIGNALS:
    // Is emitted when new settings must be used
    void optionsChanged();

private Q_SLOTS:
    void addHighlighterSet();
    void removeHighlighterSet();

    void moveHighlighterSetUp();
    void moveHighlighterSetDown();

    void resolveDialog( QAbstractButton* button );

    // Update the edit fields from the selected HighlighterSet.
    void updatePropertyFields();

    // Update the selected HighlighterSet from the values in the property fields.
    void updateHighlighterProperties();

    void exportHighlighters();
    void importHighlighters();

    // Shows the selected Team Highlighter Set, read-only.
    void showSelectedTeamGroup();

private:
    void populateHighlighterList();
    void setCurrentRow( int row );
    void loadIcons();

    // Lets the Color Labels of the copy this dialog edits follow the Theme,
    // as those of the application do (ADR-0006): a Color Label the user has
    // not colored here shows the color of the Theme applied now, and OK
    // writes that rather than the color of the Theme before the switch.
    void showColorLabelsOfTheme();

private:
    HighlighterSetEdit* highlighterSetEdit_;

    // Temporary HighlighterSetCollection modified by the dialog
    // it is copied from the one in Config()
    HighlighterSetCollection highlighterSetCollection_;

    // Index of the row currently selected or -1 if none.
    int selectedRow_;

    QLabel* teamGroupsLabel_ = nullptr;
    QListWidget* teamGroupsList_ = nullptr;
    QList<HighlighterSet> teamGroups_;
    // The row of the Team set shown, -1 when none is.
    int selectedTeamRow_ = -1;

    // The color buttons of the Color Labels, in slot order.
    std::vector<QPushButton*> colorLabelForeButtons_;
    std::vector<QPushButton*> colorLabelBackButtons_;
};

#endif
