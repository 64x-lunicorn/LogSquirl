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
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

#include "groupchooser.h"
#include "teamfolder.h"
#include "teamgroupedits.h"

// The groups a dialog that adds one thing to a group -- Add as Predefined
// Filter..., Name This Value -- offers in its Group Chooser (#955, #956): the
// user's own, as read when the dialog opened, and the Team groups once the
// dialog is given them; and where OK puts the group chosen. The dialog says
// what the chooser lists of a group, and how it chooses a group by its id.
template <typename Group>
class ChooserGroups {
public:
    // The names of what a group holds, which the chooser lists for it.
    using ItemNames = std::function<QStringList( const Group& )>;
    // Chooses the group of the id in the chooser, as the dialog chooses one.
    using Choose = std::function<void( const QString& id )>;

    // Offers own in chooser, which it takes no ownership of; nothing is
    // offered before offer().
    ChooserGroups( GroupChooser* chooser, QList<Group> own, ItemNames itemNames, Choose choose )
        : chooser_( chooser )
        , own_( std::move( own ) )
        , itemNames_( std::move( itemNames ) )
        , choose_( std::move( choose ) )
    {
    }

    // Hands the chooser the groups, choosing the one of the id.
    void offer( const QString& id )
    {
        std::optional<GroupChooser::TeamGroups> team;
        if ( showsTeam_ ) {
            team = GroupChooser::TeamGroups{ choicesOf( teamEdits_.groups() ),
                                             teamEdits_.isEditable() };
        }
        chooser_->setGroups( choicesOf( own_ ), team );
        choose_( id );
    }

    // Offers the Team groups too, the Team Folder being on, the group chosen
    // staying chosen. Only when editable can they be chosen. revisions holds
    // the revision of each group's file, by id.
    void showTeam( const QList<Group>& groups, bool editable,
                   const QHash<QString, QString>& revisions )
    {
        const auto id = chooser_->choice().id;
        teamEdits_.reset( groups, editable, revisions );
        showsTeam_ = true;
        offer( id );
    }

    // The user's own groups: as read when the dialog opened, and after put()
    // with the group OK made or changed among them.
    const QList<Group>& own() const
    {
        return own_;
    }

    // The Team groups that can be chosen: none unless showTeam() gave
    // editable ones.
    QList<Group> editableTeam() const
    {
        return showsTeam_ && teamEdits_.isEditable() ? teamEdits_.groups() : QList<Group>{};
    }

    // The group chosen, as the dialog was given it; none for a new group.
    const Group* chosen() const
    {
        const auto choice = chooser_->choice();
        if ( choice.isNew() ) {
            return nullptr;
        }
        const auto& groups
            = choice.section == GroupChooser::Section::Team ? teamEdits_.groups() : own_;
        for ( const auto& group : groups ) {
            if ( group.id() == choice.id ) {
                return &group;
            }
        }
        return nullptr;
    }

    // The id of the group chosen; empty for a new one until put() put it, its
    // id after that.
    QString chosenId() const
    {
        const auto choice = chooser_->choice();
        return choice.isNew() ? madeId_ : choice.id;
    }

    // Puts group -- the one chosen, as OK makes or changes it -- where it
    // goes: among the Team groups, returning the requests that publish what
    // changed of them against the groups as given, based on their revisions;
    // or among own(), returning none.
    QList<logsquirl::teamfolder::PublishRequest> put( const Group& group )
    {
        const auto choice = chooser_->choice();
        const auto isTeam = choice.section == GroupChooser::Section::Team;
        auto& groups = isTeam ? teamEdits_.groups() : own_;
        if ( choice.isNew() ) {
            madeId_ = group.id();
            groups.append( group );
        }
        else {
            for ( auto& old : groups ) {
                if ( old.id() == choice.id ) {
                    old = group;
                }
            }
        }
        return isTeam ? teamEdits_.takeRequests() : QList<logsquirl::teamfolder::PublishRequest>{};
    }

private:
    QList<GroupChooser::Group> choicesOf( const QList<Group>& groups ) const
    {
        QList<GroupChooser::Group> choices;
        for ( const auto& group : groups ) {
            choices.append( GroupChooser::Group{ group.id(), group.name(), itemNames_( group ) } );
        }
        return choices;
    }

    GroupChooser* chooser_;
    QList<Group> own_;
    ItemNames itemNames_;
    Choose choose_;
    bool showsTeam_ = false;
    TeamGroupEdits<Group> teamEdits_;
    // The id of the group put() made.
    QString madeId_;
};
