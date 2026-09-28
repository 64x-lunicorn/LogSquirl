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

#ifndef LOGSQUIRL_TEST_STORED_SESSION_H
#define LOGSQUIRL_TEST_STORED_SESSION_H

// A window of the last Session, saved in the settings store as after an
// earlier run, for a test that restores it (#608).
//
// The application reads the Session from the settings store, and reads it
// again at several points of a restore: building a Session, building a main
// window. A window put into the in-memory Session info only is gone at the
// first of those reads once any earlier test case in the same process has
// saved the Session, and the restore opens no tab. So the window is saved to
// the settings store, as the application finds it at startup, and a test
// passes whatever ran before it in the process.
//
//     StoredSessionWindow stored{ windowId, { { fileName, viewContext } }, 0 };
//     WindowSession window{ appSession, windowId, 0 };
//     window.restore( ... );
//
// It can be written before or after a Session is built, and must be before the
// window's WindowSession, which would otherwise add the window as a new one.
// The settings store and the in-memory Session info are left as they were
// before it once it goes, whatever the test saved in between.

#include <vector>

#include <QString>

#include "sessioninfo.h"

class StoredSessionWindow {
public:
    // Saves the window with these Log Files in tab order, and the index of the
    // one whose tab was in front: -1 for none, as a Session stored before
    // that was saved (#542).
    StoredSessionWindow( const QString& windowId,
                         const std::vector<SessionInfo::OpenFile>& openFiles, int currentFile = -1 )
        : before_( SessionInfo::getSynced() )
    {
        auto& stored = SessionInfo::getSynced();
        stored.add( windowId );
        stored.setOpenFiles( windowId, openFiles, currentFile );
        stored.save();
    }

    ~StoredSessionWindow()
    {
        before_.save();
        SessionInfo::getSynced();
    }

    StoredSessionWindow( const StoredSessionWindow& ) = delete;
    StoredSessionWindow& operator=( const StoredSessionWindow& ) = delete;
    StoredSessionWindow( StoredSessionWindow&& ) = delete;
    StoredSessionWindow& operator=( StoredSessionWindow&& ) = delete;

private:
    const SessionInfo before_;
};

#endif
