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

// Where LogSquirl keeps what it stores (#602), decided in this one place.
//
// A portable run keeps everything beside its executable: its settings in
// `logsquirl.conf` and `logsquirl_session.conf`, and under its data directory,
// which is the executable's directory, the Log Formats (`formats`), the
// plugins (`plugins`), their configuration (`plugin_config`), the Team Folder
// clone (`teamfolder`), the user's theme stylesheets (`themes`) and the crash
// dumps (`logsquirl_dump`). A run is portable when its build forces it -- the
// portable package, the command line tool and the test binaries -- or when it
// finds a `logsquirl.conf` beside its executable.
//
// Any other run keeps its settings where QSettings puts them and its data in
// the platform's application data and configuration locations
// (QStandardPaths::AppDataLocation and AppConfigLocation).
class DataLocation {
public:
    // The location of this process. Whether it is portable is decided the
    // first time this is asked and holds for the rest of the run, so a
    // `logsquirl.conf` the run writes itself does not move its data.
    static const DataLocation& current();

    // The location of a run of a build that forces portable or does not,
    // from an executable in executableDirectory.
    DataLocation( bool forcePortable, QString executableDirectory );

    bool isPortable() const;

    // The directory of the executable, without a trailing separator.
    const QString& executableDirectory() const;

    // The settings file a portable run reads and writes, `logsquirl.conf`
    // beside the executable. Its presence alone makes a run portable.
    QString portableSettingsPath() const;

    // Where the user's Log Formats, plugins, plugin configuration and Team
    // Folder clone are kept, and the crash dumps: the executable's directory
    // for a portable run, QStandardPaths::AppDataLocation otherwise. Empty if
    // the platform has none.
    QString dataDirectory() const;

    // Where the user's theme stylesheets are kept: the executable's directory
    // for a portable run, QStandardPaths::AppConfigLocation otherwise. Empty
    // if the platform has none.
    QString configDirectory() const;

private:
    // Whether the build forces a portable run. Every executable defines it,
    // the way it chooses its build: true for the portable package, the command
    // line tool and the test binaries, false for the installed application.
    static const bool ForcePortable;

    bool portable_;
    QString executableDirectory_;
};
