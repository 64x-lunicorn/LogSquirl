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

#include "decompressionfailure.h"

class AtomicFlag;
class KArchiveDirectory;

// Writes the entries of `root`, the directory tree of an archive, into
// `folder` and nowhere else (#815). Whoever extracts an archive -- a member
// of a compressed Log File, a Plugin installed from a download -- goes
// through here, never through KArchive's own copy:
//
// - A symbolic link entry is never created; it is skipped with a warning.
//   A link could point anywhere, and a later entry would be written through
//   it, on a case-insensitive file system even through a link whose name
//   differs from the entry's directory in letter case only.
// - An entry whose name is not one plain path component (`..`, a separator)
//   is skipped: it could land beside the folder.
// - A directory is written into only once its canonical path is the folder's
//   or below it, whole components compared, so a sibling whose name merely
//   starts with the folder's is outside; a link that was in the folder
//   before is never followed.
//
// At most `budgetBytes` are written, counted over every entry of the
// archive together: an entry whose next bytes would pass it stops the
// extraction with BudgetExceeded, so a small archive crafted to expand
// without end fills no disk (#816).
//
// Creates `folder` when it is missing. Returns why it stopped when an entry
// could not be written, the budget was reached or `interrupt` was set, and
// None when every entry is written; a skipped entry does not fail it. What
// was written before it stopped is left for the caller to remove.
DecompressionFailure extractArchiveDirectory( const KArchiveDirectory& root, const QString& folder,
                                              qint64 budgetBytes, AtomicFlag& interrupt );

// The path of the member `member` of an archive extracted into `folder`: the
// member's path inside the folder when it names a regular file there, not a
// link and not behind one, which resolves inside the folder. Empty otherwise
// (#815).
QString extractedMemberPath( const QString& folder, const QString& member );
