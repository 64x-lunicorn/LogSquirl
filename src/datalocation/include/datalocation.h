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
#include <QStringList>

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
    // What a portable run took over from the locations the portable package
    // kept its data in before #602 (#613), for the log.
    struct TakeOver {
        // Each folder copied, as "from -> to".
        QStringList copied;
        // Each folder found in the old locations but left there, and why.
        QStringList skipped;
        // What could not be copied. A takeover that fails in part still lets
        // the run start.
        QStringList failed;
    };

    // The location of this process. Whether it is portable is decided the
    // first time this is asked and holds for the rest of the run, so a
    // `logsquirl.conf` the run writes itself does not move its data.
    static const DataLocation& current();

    // The location of a run of a build that forces portable or does not,
    // from an executable in executableDirectory.
    DataLocation( bool forcePortable, const QString& executableDirectory );

    // The location of a Benchmark Run (#666): portable, with `directory` in
    // place of the executable's directory, so its settings, Session and data
    // are all there; it takes nothing over from the user's locations.
    static DataLocation isolatedIn( const QString& directory );

    // Makes current() the location isolated in `directory`. Only before
    // current() is first asked: returns false, and changes nothing, once it
    // has been.
    static bool isolateCurrentIn( const QString& directory );

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

    // The file a portable run writes beside its executable once it has taken
    // over the data of the old locations, so it never takes it over again.
    QString takeOverMarkerPath() const;

    // Before #602 the portable package kept its Log Formats, plugins, plugin
    // configuration and Team Folder clone in its AppDataLocation and its theme
    // stylesheets in its AppConfigLocation, both named after the executable
    // (#613). On the first start of a portable run with none of that data
    // beside its executable yet, this copies what the old locations hold there,
    // once, and writes the marker. The old locations are left as they are.
    // Nothing is copied for an installed run or a Benchmark Run, when the
    // marker exists, or when
    // `formats`, `plugin_config`, `teamfolder` or `themes` beside the
    // executable holds anything. A plugin whose folder is already beside the
    // executable, such as one the package ships, is kept and not replaced.
    //
    // The old locations are what QStandardPaths gives this run, so it is
    // called after the application object exists and before anything reads
    // the data directory.
    TakeOver takeOverOldPortableData() const;

    // The same, from old locations given by the caller.
    TakeOver takeOverOldPortableData( const QString& oldDataDirectory,
                                      const QString& oldConfigDirectory ) const;

private:
    // Whether the build forces a portable run. Every executable defines it,
    // the way it chooses its build: true for the portable package, the command
    // line tool and the test binaries, false for the installed application.
    static const bool ForcePortable;

    bool portable_;
    // Whether this is the location of a Benchmark Run, whose "executable
    // directory" is the directory it was isolated in.
    bool isolated_ = false;
    QString executableDirectory_;
};
