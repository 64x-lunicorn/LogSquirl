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

#include <QString>

// A Filter Group or Highlighter Set an enabled plugin ships (#865): the group
// as the plugin's file holds it, under an id of the plugin's own, and the
// name of the plugin, its Source. Read-only, never stored, and gone when the
// plugin is disabled (ADR 0029).
template <typename Group>
struct PluginGroup {
    Group group;
    QString pluginName;
};

// The name a plugin's group is listed under: its own name, followed by its
// Source, so that it is told apart from a group of the user's of the same name.
QString pluginGroupShownName( const QString& groupName, const QString& pluginName );
