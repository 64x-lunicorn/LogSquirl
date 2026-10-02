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

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <utility>

#include "teamfolder.h"

class QBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// What the Predefined Filters Dialog, the Highlighters Dialog and the Value
// Names Dialog share about the Team groups they show below the user's own
// groups (#694).

// The widgets of the Team groups: the label, the list, and the buttons that
// add a Team group, share one of the user's own, copy one into the user's own
// and delete one for the team. Each dialog connects them to what it does with
// its own kind of group.
class TeamGroupsSection : public QObject {
    Q_OBJECT

public:
    // Adds the section to the end of layout: the list under labelText, and
    // the buttons, addText on the one that adds a new Team group.
    TeamGroupsSection( QWidget* parent, QBoxLayout* layout, const QString& labelText,
                       const QString& addText );

    QListWidget* list() const
    {
        return list_;
    }
    QPushButton* addButton() const
    {
        return addButton_;
    }
    QPushButton* shareButton() const
    {
        return shareButton_;
    }
    QPushButton* copyButton() const
    {
        return copyButton_;
    }
    QPushButton* deleteButton() const
    {
        return deleteButton_;
    }

    // Shows the buttons that change the Team groups only when they can be
    // changed.
    void setEditable( bool editable );
    // Lists the names, replacing what the list held.
    void setNames( const QStringList& names );
    // Enables the buttons for what is selected: a group of the user's own, or
    // a Team group.
    void updateButtons( bool editable, bool ownSelected, bool teamSelected );
    // Asks whether the selected Team group is to be deleted for the whole
    // team, under title.
    bool confirmDeletion( const QString& title ) const;

private:
    QWidget* parent_;
    QLabel* label_;
    QListWidget* list_;
    QPushButton* addButton_;
    QPushButton* shareButton_;
    QPushButton* copyButton_;
    QPushButton* deleteButton_;
};

// The Team groups of one kind as a dialog edits them: its copy of them, which
// the user changes in the dialog, and what they were when the dialog was
// given them, so that OK or Apply publishes only what changed.
template <typename Group>
class TeamGroupEdits {
public:
    // Takes the groups the Team Folder has, whether they can be changed, and
    // the revision of each group's file, by group id.
    void reset( const QList<Group>& groups, bool editable, QHash<QString, QString> revisions )
    {
        groups_ = groups;
        asGiven_ = groups;
        editable_ = editable;
        revisions_ = std::move( revisions );
    }

    // A published group's file has a new revision: the next edit of it is
    // based on that one, not on the one it was loaded with.
    void updateRevisions( const QStringList& ids, const QHash<QString, QString>& revisions )
    {
        for ( const auto& id : ids ) {
            if ( const auto found = revisions.constFind( id ); found != revisions.constEnd() ) {
                revisions_.insert( id, *found );
            }
        }
    }

    bool isEditable() const
    {
        return editable_;
    }

    QList<Group>& groups()
    {
        return groups_;
    }
    const QList<Group>& groups() const
    {
        return groups_;
    }

    QStringList names() const
    {
        return namesOf( groups_ );
    }

    // Appends a Team copy of a group of the user's own, under a fresh id and
    // a name no Team group has, and returns it.
    const Group& share( const Group& own )
    {
        groups_.append( logsquirl::teamfolder::copyOfGroup( own, names() ) );
        return groups_.back();
    }

    // A copy of the Team group at row for the user's own groups, under a
    // fresh id and a name none of own has.
    Group copyFor( qsizetype row, const QList<Group>& own ) const
    {
        return logsquirl::teamfolder::copyOfGroup( groups_.at( row ), namesOf( own ) );
    }

    // What was done to the Team groups since they were given or last
    // published, to go to the team; from now on that is how they were given.
    QList<logsquirl::teamfolder::PublishRequest> takeRequests()
    {
        auto requests = logsquirl::teamfolder::requestsForChanges( asGiven_, groups_, revisions_ );
        asGiven_ = groups_;
        return requests;
    }

    static QStringList namesOf( const QList<Group>& groups )
    {
        QStringList names;
        for ( const auto& group : groups ) {
            names.append( group.name() );
        }
        return names;
    }

private:
    QList<Group> groups_;
    QList<Group> asGiven_;
    bool editable_ = false;
    QHash<QString, QString> revisions_;
};
