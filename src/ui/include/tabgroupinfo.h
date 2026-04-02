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

#include <optional>
#include <vector>

#include <QColor>
#include <QString>
#include <QStringList>

#include "persistable.h"

// Manages named, colored tab groups that persist across sessions.
// Each group holds a set of tab paths (file paths) and provides
// CRUD operations for group membership, naming, and colour.
class TabGroupInfo final : public Persistable<TabGroupInfo, session_settings> {
  public:
    static const char* persistableName()
    {
        return "TabGroupInfo";
    }

    struct TabGroup {
        QString id;
        QString name;
        QColor color;
        QStringList tabPaths;
    };

    // Returns all defined groups.
    const std::vector<TabGroup>& groups() const;

    // Returns the group that contains `tabPath`, or nullopt.
    std::optional<TabGroup> groupForTab( const QString& tabPath ) const;

    // Creates a new group with the given name and colour. Returns its id.
    QString addGroup( const QString& name, const QColor& color );

    // Removes the group identified by `groupId`. Returns *this for chaining.
    TabGroupInfo& removeGroup( const QString& groupId );

    // Renames the group identified by `groupId`. Returns *this for chaining.
    TabGroupInfo& renameGroup( const QString& groupId, const QString& newName );

    // Changes the colour of the group identified by `groupId`. Returns *this.
    TabGroupInfo& setGroupColor( const QString& groupId, const QColor& color );

    // Adds `tabPath` to the group identified by `groupId`.
    // If the tab is already in another group it is moved. Returns *this.
    TabGroupInfo& addTabToGroup( const QString& groupId, const QString& tabPath );

    // Removes `tabPath` from whatever group it belongs to. Returns *this.
    TabGroupInfo& removeTabFromGroup( const QString& tabPath );

    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

  private:
    // Returns an iterator to the group with `groupId`, or groups_.end().
    std::vector<TabGroup>::iterator findGroup( const QString& groupId );
    std::vector<TabGroup>::const_iterator findGroup( const QString& groupId ) const;

    std::vector<TabGroup> groups_;
};
