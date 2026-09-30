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

#ifndef LOGSQUIRL_LOGFILEPROVENANCE_H
#define LOGSQUIRL_LOGFILEPROVENANCE_H

#include <functional>
#include <memory>

#include <QString>

#include "session.h"

class CommandSource;
class CrawlerWidget;

// What a Log File opened in a tab is, handed to the window's one open call
// (#643): where it came from, as the Session keeps it (Ordinary or Transient,
// the archive member, what it was converted from), and what its tab is while
// it is open -- the title and tooltip it opens with, the Command Source that
// feeds it. The window keeps all of it by the Log File's path until the tab
// closes, and forgets it when the open fails.
struct LogFileProvenance {
    LogFileOrigin origin;
    // The title and tooltip the tab opens with instead of its file's name and
    // path (#606); empty for none. An empty tooltip leaves the path.
    QString openingTitle;
    QString toolTip;
    // What feeds the Log File, owned by its tab from the open on (#575); null
    // for none.
    std::unique_ptr<CommandSource> commandSource;
    // Called once the open is done -- later than the open call while the
    // plugins load (#606) -- with the tab's Crawler Widget, or null when no
    // tab of the Log File opened in the window.
    std::function<void( CrawlerWidget* )> whenOpened;

    LogFileProvenance();
    ~LogFileProvenance();
    LogFileProvenance( LogFileProvenance&& ) noexcept;
    LogFileProvenance& operator=( LogFileProvenance&& ) noexcept;
    LogFileProvenance( const LogFileProvenance& ) = delete;
    LogFileProvenance& operator=( const LogFileProvenance& ) = delete;

    // A Log File the user opened, from its own path.
    static LogFileProvenance ordinary();
    // One decompressed from an archive (#596).
    static LogFileProvenance fromArchive( const ArchiveMember& archiveMember );
    // One made for this run alone (#570): standard input, a merge, a data
    // source, the clipboard, a download. Its tab opens with `title`,
    // when one is given.
    static LogFileProvenance transient( const QString& title = {},
                                        const QString& titleToolTip = {} );
    // What a converter plugin wrote for the Log File at `path`, which came
    // from `source` (#605).
    static LogFileProvenance conversionOf( const QString& path, const LogFileOrigin& source );
    // The output of a Command Source, its spool file (#575).
    static LogFileProvenance commandOutput( std::unique_ptr<CommandSource> source,
                                            const QString& title, const QString& titleToolTip );

    // The name the tab of the Log File at `fileName` shows before the user
    // renames it: its opening title, else its file's name.
    QString shownTitle( const QString& fileName ) const;
};

#endif
