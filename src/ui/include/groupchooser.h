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
#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;

// The Group Chooser (#955): where a dialog that adds one thing from the Log
// Line -- Add as Predefined Filter..., Name This Value -- lets the user choose
// the group it goes into. One choice lists the user's own groups and, when the
// Team Folder is on, the Team groups, each section under a heading that
// cannot be chosen, as the Group List shows its sections; below them New
// ...  and New Team .... Choosing one of these shows a name field below the
// choice, filled with the first free name; the group is the dialog's to make,
// on OK. A Team group chosen, or a new one, a line below says that OK shares it
// with the team.
//
// The chooser knows no kind of group: the dialog hands it the groups as plain
// values, and the texts naming the kind. It changes no group itself.
class GroupChooser : public QWidget {
    Q_OBJECT

public:
    enum class Section { Own, Team };
    Q_ENUM( Section )

    // A group offered: its id, its name, and the names of what it holds,
    // which its tooltip lists and a new item's name is checked against.
    struct Group {
        QString id;
        QString name;
        QStringList itemNames;
    };

    // The Team groups offered. Only when editable can one be chosen or a new
    // one made; else they are listed, greyed.
    struct TeamGroups {
        QList<Group> groups;
        bool editable = true;
    };

    // What the chooser shows that names the kind.
    struct Texts {
        // The headings of the sections: "My Filter Groups", "Team Filter Groups".
        QString ownSection;
        QString teamSection;
        // The entries making a group: "New Filter Group...", "New Team Filter Group...".
        QString newGroup;
        QString newTeamGroup;
        // The name proposed for a new group, numbered when taken: "New Filter Group".
        QString newGroupName;
        // The line shown while a Team group is chosen.
        QString teamHint;
        // Why a new group's name will not do: it is empty, or a group of its
        // section has it (%1 the name).
        QString nameEmpty;
        QString nameTaken;
    };

    // What is chosen.
    struct Choice {
        Section section = Section::Own;
        // The group's id; empty for a new group.
        QString id;
        // The group's name; for a new group what the name field holds, trimmed.
        QString name;

        bool isNew() const
        {
            return id.isEmpty();
        }
    };

    explicit GroupChooser( Texts texts, QWidget* parent = nullptr );

    // Offers the user's own groups and, when there is a Team Folder, its
    // groups; without one there is no Team section and no New Team ....
    // The first of own is chosen.
    void setGroups( const QList<Group>& own, const std::optional<TeamGroups>& team = std::nullopt );

    // Chooses the group of the id; when none can be chosen with it (it is
    // gone), the group of fallbackId, else the first of the user's own.
    void choose( const QString& id, const QString& fallbackId );

    Choice choice() const;
    // The names of what the group chosen holds; none for a new group.
    QStringList itemNames() const;
    // Why the new group's name will not do; empty when it will, and when no
    // new group is chosen.
    QString warning() const;

    bool hasTeamSection() const;

    // The parts, for the tests and the dialog's layout.
    QComboBox* comboBox() const
    {
        return combo_;
    }
    QLineEdit* nameEdit() const
    {
        return name_;
    }
    QLabel* teamHint() const
    {
        return hint_;
    }

Q_SIGNALS:
    // Another group is chosen, or the new group's name changed.
    void choiceChanged();

private:
    void addHeading( const QString& text );
    void addGroup( Section section, const Group& group, bool enabled );
    void addNewEntry( Section section, const QString& text );
    // The row of the combo box holding the group of the id that can be
    // chosen, -1 for none.
    int rowOf( const QString& id ) const;
    // The names the groups of the section have.
    QStringList namesOf( Section section ) const;
    void currentChanged();

    Texts texts_;
    QList<Group> own_;
    std::optional<TeamGroups> team_;

    QComboBox* combo_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* hint_ = nullptr;
};
