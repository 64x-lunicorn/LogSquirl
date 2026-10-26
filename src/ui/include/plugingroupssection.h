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
#include <QObject>
#include <QString>
#include <QStringList>

#include "plugingroup.h"

class QBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// What the Predefined Filters Dialog and the Highlighters Dialog share about
// the groups the enabled plugins ship, shown below the user's own groups
// (#865): the label, the list, which names each group with its Source, and
// the button that copies one into the user's own groups. They are read-only;
// each dialog connects the list and the button to what it does with its own
// kind of group.
class PluginGroupsSection : public QObject {
    Q_OBJECT

public:
    // Adds the section to the end of layout: the list under labelText.
    PluginGroupsSection( QWidget* parent, QBoxLayout* layout, const QString& labelText );

    QListWidget* list() const
    {
        return list_;
    }
    QPushButton* copyButton() const
    {
        return copyButton_;
    }

    // Lists the groups, each with its Source, replacing what the list held.
    template <typename Group>
    void setGroups( const QList<PluginGroup<Group>>& groups )
    {
        QStringList names;
        for ( const auto& group : groups ) {
            names.append( pluginGroupShownName( group.group.name(), group.pluginName ) );
        }
        setNames( names );
    }

    // Enables the copy button when a plugin's group is selected.
    void updateButtons( bool pluginGroupSelected );

private:
    void setNames( const QStringList& names );

    QLabel* label_;
    QListWidget* list_;
    QPushButton* copyButton_;
};
