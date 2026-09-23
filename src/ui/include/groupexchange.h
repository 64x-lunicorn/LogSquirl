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

#include "highlighterset.h"
#include "predefinedfilters.h"

// The Group Exchange: what the Predefined Filters Dialog and the Highlighters
// Dialog share about handing one Filter Group or one Highlighter Set to
// someone else as a file. The dialogs open the file dialogs and call this.
namespace logsquirl::groupexchange {

enum class GroupKind { Filter, Highlighter };

// The file name proposed for exporting a group: its name with every
// character that is not allowed in a file name (/ \ : * ? " < > |) replaced
// by '_', followed by "_filter.conf" or "_highlighter.conf".
QString suggestedFileName( const QString& groupName, GroupKind kind );

// Writes exactly this one group to file, replacing whatever the file held,
// in the .conf layout earlier versions write, so that they import it. A
// Highlighter Set file holds no Color Labels and no list of active sets.
// Whether the file was written.
bool writeGroup( const QString& file, const PredefinedFilterSet& group );
bool writeGroup( const QString& file, const HighlighterSet& group );

// The folder the file dialog of an export opens in: the one last exported to
// during this session, empty before the first export. Never stored.
QString exportFolder();
void rememberExportFolder( const QString& file );

// The file name a user typed, with ".conf" appended when it lacks it.
QString withConfSuffix( const QString& file );

} // namespace logsquirl::groupexchange
