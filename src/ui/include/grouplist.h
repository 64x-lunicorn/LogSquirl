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

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "groupexchange.h"
#include "plugingroup.h"

class QAction;
class QMenu;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// The list of the groups of one kind that the Predefined Filters Dialog, the
// Highlighters Dialog and the Value Names Dialog edit (#893): Filter Groups,
// Highlighter Sets or Naming Groups. The user's own groups, the Team groups and
// the Plugin groups are sections of one list, each under a title naming the
// kind; below it one row of buttons and Export... and Import....
//
// The dialog holds the groups. It hands the list their names, section by
// section, and does what the list says the user asked for: the list changes
// no group itself. It does enable only what can be done with the group
// selected, and asks before a Team group is deleted for the whole team.
class GroupList : public QWidget {
    Q_OBJECT

public:
    // Where a group is listed.
    enum class Section { Own, Team, Plugin };
    Q_ENUM( Section )

    explicit GroupList( logsquirl::groupexchange::GroupKind kind, QWidget* parent = nullptr );

    // The user's own groups, by name. Those at the rows in undeletable cannot
    // be deleted (the Default Filter Group).
    void setOwnGroups( const QStringList& names, const QSet<int>& undeletable = {} );
    // The Team groups, by name; the section shows from the first call on.
    // Only when editable can a Team group be added, deleted or shared into.
    void setTeamGroups( const QStringList& names, bool editable );
    // The groups the enabled plugins ship, each listed with its Source. No
    // section when there are none.
    template <typename Group>
    void setPluginGroups( const QList<PluginGroup<Group>>& groups )
    {
        QStringList names;
        for ( const auto& group : groups ) {
            names.append( pluginGroupShownName( group.group.name(), group.pluginName ) );
        }
        setPluginGroupNames( names );
    }
    // Each of set...Groups replaces what its section held: when one of its
    // groups was selected, none is any more, and currentChanged says so.

    // The names of the groups, in order: what set...Groups takes.
    template <typename Group>
    static QStringList namesOf( const QList<Group>& groups )
    {
        QStringList names;
        for ( const auto& group : groups ) {
            names.append( group.name() );
        }
        return names;
    }

    bool hasSection( Section section ) const;
    int count( Section section ) const;
    QString name( Section section, int row ) const;
    // A group was renamed in the dialog.
    void setName( Section section, int row, const QString& name );

    // The section and row of the group selected; the row is -1 when none is.
    Section currentSection() const;
    int currentRow() const;
    // Selects the group at row of the section, none for row -1, and says so
    // with currentChanged, even when it was selected already.
    void setCurrent( Section section, int row );

    // The buttons, for the tests.
    QToolButton* newButton() const
    {
        return newButton_;
    }
    QAction* newTeamAction() const
    {
        return newTeamAction_;
    }
    QToolButton* deleteButton() const
    {
        return deleteButton_;
    }
    QToolButton* upButton() const
    {
        return upButton_;
    }
    QToolButton* downButton() const
    {
        return downButton_;
    }
    QPushButton* shareButton() const
    {
        return shareButton_;
    }
    QPushButton* copyButton() const
    {
        return copyButton_;
    }
    QPushButton* exportButton() const
    {
        return exportButton_;
    }
    QPushButton* importButton() const
    {
        return importButton_;
    }

Q_SIGNALS:
    // Another group is selected, or none is (row -1): the dialog shows it.
    void currentChanged( GroupList::Section section, int row );
    // A new group of the user's own (Own) or a new Team group (Team).
    void newRequested( GroupList::Section section );
    // The group is to go; for a Team group the user has confirmed that it
    // goes for the whole team.
    void deleteRequested( GroupList::Section section, int row );
    // The user's own group at row is to move by offset, -1 or 1, within them.
    void moveRequested( int row, int offset );
    // A Team copy of the user's own group at row is wanted.
    void shareRequested( int row );
    // A copy of the Team or Plugin group among the user's own is wanted.
    void copyRequested( GroupList::Section section, int row );
    // The group selected is to be written to a file.
    void exportRequested( GroupList::Section section, int row );
    // Groups are to be read from files into the user's own.
    void importRequested();

private:
    void setPluginGroupNames( const QStringList& names );
    // The item of the section's title, created at its place when create is
    // true; null when it has none.
    QTreeWidgetItem* sectionItem( Section section, bool create );
    const QTreeWidgetItem* sectionItem( Section section ) const;
    void setGroups( Section section, const QStringList& names );
    void selectionChanged();
    void updateButtons();
    void deleteSelected();
    void loadIcons();

    logsquirl::groupexchange::GroupKind kind_;
    QTreeWidget* tree_ = nullptr;
    QToolButton* newButton_ = nullptr;
    QMenu* newMenu_ = nullptr;
    QAction* newOwnAction_ = nullptr;
    QAction* newTeamAction_ = nullptr;
    QToolButton* deleteButton_ = nullptr;
    QToolButton* upButton_ = nullptr;
    QToolButton* downButton_ = nullptr;
    QPushButton* shareButton_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* importButton_ = nullptr;

    QSet<int> undeletable_;
    bool teamEditable_ = false;
    // The group selected, as last said with currentChanged.
    Section section_ = Section::Own;
    int row_ = -1;
    // Set while the list is changed from here: the selection it changes on
    // the way is not the user's.
    bool updating_ = false;
};
