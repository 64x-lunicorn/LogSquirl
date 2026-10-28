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

#include <vector>

#include "highlighterset.h"
#include "plugingroup.h"
#include "pluginmetadata.h"
#include "predefinedfilters.h"

// The Filter Groups and Highlighter Sets the enabled plugins ship (#865): the
// files every enabled plugin names in the `predefined_filters` and
// `highlighter_sets` keys of its manifest, whether or not its library is
// loaded. Each window reads them once the plugins have been discovered and
// again whenever a plugin is enabled or disabled, the points the Log Format
// Catalog takes the plugins' Log Formats at, and hands them to the Predefined
// Filters Collection and the Highlighter Set Collection, which never store
// them (ADR 0029).
namespace logsquirl::plugingroups {

struct PluginGroups {
    QList<PluginGroup<PredefinedFilterSet>> filterGroups;
    QList<PluginGroup<HighlighterSet>> highlighterSets;
};

// The groups the enabled ones of plugins ship, the plugins in the order of
// their ids and each plugin's files in its manifest's order, every group of a
// file in the file's order. A group's id is idOf() the plugin's id and the id
// in the file, so that it is never the id of a group of the user's, nor of
// another plugin's; a second group of the same id in one plugin is skipped. A
// file that cannot be read, holds no group or holds groups of another kind is
// skipped with a logged warning naming the plugin and the file.
PluginGroups readEnabled( const std::vector<logsquirl::plugins::PluginMetadata>& plugins,
                          const QStringList& enabled );

// The id a group of the plugin pluginId goes by, given the id its file holds.
QString idOf( const QString& pluginId, const QString& groupId );

} // namespace logsquirl::plugingroups
