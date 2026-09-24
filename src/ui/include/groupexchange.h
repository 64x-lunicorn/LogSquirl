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

#include <functional>
#include <optional>

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

// --- Import: read and merge ---
//
// Everything below is free of dialogs. What to do with a group that meets one
// of the same id or name is asked of a ConflictResolver the caller passes:
// the dialogs show a question, the tests give a fixed answer.

enum class ConflictKind {
    SameId,  // an existing group has the imported group's id
    SameName // only the name is the same
};

enum class ConflictAnswer { Replace, KeepBoth, Skip };

struct ConflictQuestion {
    ConflictKind kind;
    QString importedName;
    QString existingName;
    // The answer to offer first: Replace for the same id, Keep both for the
    // same name only.
    ConflictAnswer preselected;
    // Whether Replace may be offered: the Default Filter Group is never
    // replaced by an import.
    bool replaceAllowed;
};

struct ConflictDecision {
    ConflictAnswer answer = ConflictAnswer::Skip;
    // "Apply to all remaining conflicts" of the current import.
    bool applyToAll = false;
};

using ConflictResolver = std::function<ConflictDecision( const ConflictQuestion& )>;

// The answer preselected for a conflict of this kind.
ConflictAnswer preselectedAnswer( ConflictKind kind );

// The name to give an imported group that is kept next to an existing one:
// name itself when no group has it, else the first free "<name> (n)", n from 2.
QString firstFreeName( const QString& name, const QStringList& takenNames );

// One import, possibly of several files. It remembers an "Apply to all
// remaining conflicts" answer, so that it holds across the files.
class ImportSession {
public:
    explicit ImportSession( ConflictResolver resolver );

    // The answer to a conflict: the remembered one, or the resolver's.
    ConflictAnswer decide( const ConflictQuestion& question );

private:
    ConflictResolver resolver_;
    std::optional<ConflictAnswer> rememberedAnswer_;
};

enum class ReadError {
    None,
    Unreadable, // the file cannot be opened or read as settings
    NoGroups    // it holds no group
};

struct ImportResult {
    ReadError error = ReadError::None;
    int added = 0;
    int replaced = 0;
    int skipped = 0;
};

// The groups a file holds, in file order; error says what is wrong when it
// holds none. A Filter Group file read through the collection carries an
// empty Default group the file did not hold: that one is not a group of the
// file. Color Labels and active sets of a Highlighter Set file are ignored.
template <typename Group>
struct ReadGroups {
    ReadError error = ReadError::None;
    QList<Group> groups;
};
ReadGroups<PredefinedFilterSet> readFilterGroups( const QString& file );
ReadGroups<HighlighterSet> readHighlighterGroups( const QString& file );

// Brings each of the imported groups into groups by these rules: a group of
// the same id (failing that, of the same name) is a conflict the session
// decides. Replace keeps the existing group's position and id; Keep both adds
// the imported group under a fresh id and the first free name; Skip leaves
// the list alone. A group with the Default Filter Group's id never counts as
// a same-id conflict: it arrives with a fresh id and follows the name rule,
// and the Default group is never replaced (Replace against it keeps both).
ImportResult mergeGroups( QList<PredefinedFilterSet>& groups,
                          const QList<PredefinedFilterSet>& imported, ImportSession& session );
ImportResult mergeGroups( QList<HighlighterSet>& groups, const QList<HighlighterSet>& imported,
                          ImportSession& session );

// Reads the file and merges what it holds: the whole import of one file.
ImportResult importFile( const QString& file, QList<PredefinedFilterSet>& groups,
                         ImportSession& session );
ImportResult importFile( const QString& file, QList<HighlighterSet>& groups,
                         ImportSession& session );

} // namespace logsquirl::groupexchange
