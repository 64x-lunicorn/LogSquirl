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

// A Session File: one window's Session, saved to a file the user picks and
// opened again in a new window, here or on another machine (#576).
//
// It holds what the automatic Session holds for a window -- the window's
// snapshot: its Log Files in tab order, each with its view state, and the tab
// in front -- and the tab names and groups of these Log Files. Every Log File
// is written with its absolute path and its path relative to the folder of the
// Session File, so a folder of logs and its Session File still opens once it
// is moved or zipped and opened elsewhere.
//
// The file is JSON:
//
//   {
//     "format": "logsquirl-session",
//     "version": 1,
//     "currentFile": 0,
//     "files": [ { "path": "/abs/app.log", "relativePath": "app.log",
//                  "archive": { "path": ..., "relativePath": ..., "member": ... },
//                  "viewState": { what encodeViewState() writes },
//                  "tabName": "App", "group": "Backend" } ],
//     "groups": [ { "name": "Backend", "color": "#3a7bd5" } ]
//   }
//
// "archive", "tabName" and "group" are there only when set. An archive nested
// in an archive also has "members", every level's member; "member" is the
// first one's. Keys a reader does not know are ignored, so what a later
// version adds stays readable.

#include <expected>
#include <functional>

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>

#include "session.h"

// The file name extension of a Session File.
inline constexpr auto SessionFileExtension = "logsquirl-session";

// The one version of the format written; a reader takes this one and older.
inline constexpr int SessionFileVersion = 1;

// Why a Session File cannot be read. None of them opens a window.
enum class SessionFileError {
    // Not JSON, or its "format" is missing or another one.
    NotASessionFile,
    // Its "version" is higher than SessionFileVersion.
    NewerVersion,
};

// What the user is told for it.
QString sessionFileErrorText( SessionFileError error );

// The text of a Session File for `window`, whose tab names and groups are
// taken (see takeTabLabels()), saved in `folder`: pretty-printed UTF-8 JSON.
QByteArray writeSessionFile( const WindowSnapshot& window, const QDir& folder );

// A Session File read, with the Log Files it names that are left out.
struct SessionFileRead {
    // The Log Files found, each by the path it was found at: the absolute one
    // saved, else the one relative to the folder of the Session File. Its tab
    // in front is the one saved; when that one is left out, the one before
    // it, else the first.
    WindowSnapshot window;
    // What was left out, as the user knows it: the path, or the archive and
    // member, as saved.
    QStringList leftOut;

    // Leaves out the Log Files `isLeftOut` says, as reading leaves out a
    // missing one.
    void leaveOut( const std::function<bool( const SessionInfo::OpenFile& )>& isLeftOut );
};

// Reads the text of a Session File saved in `folder`. A Log File found at
// neither of its paths, or from an archive found at neither of its, is left
// out.
std::expected<SessionFileRead, SessionFileError> readSessionFile( const QByteArray& text,
                                                                  const QDir& folder );

// Fills in `window.tabs` and `window.groups` from the stored tab names and
// groups of its Log Files, as their tabs show them.
void takeTabLabels( WindowSnapshot& window );

// Merges the tab names and groups of `window` into the stored ones, for its
// Log Files as they are opened: a name replaces the stored one, a group with
// the same name as a stored one is that group and keeps its color, any other
// is created, and a Log File in another group is moved to its own. A Log File
// with no name or group in it keeps what is stored for it.
void applyTabLabels( const WindowSnapshot& window );

// What a Log File's tab name and group are stored by: its path, or, for one
// decompressed from an archive, the archive and its member (#609).
QString tabLabelKey( const SessionInfo::OpenFile& file );
