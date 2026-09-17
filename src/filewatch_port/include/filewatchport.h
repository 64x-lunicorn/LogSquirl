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

#include <QObject>
#include <QString>

// Everything an Open Log File needs from file watching: to have its Log File
// watched and no longer watched, and to hear that a watched file changed on
// disk. It is handed one when it is built; nothing in the engine looks a
// watcher up by itself (#249).
//
// The efsw-based FileWatcher is the adapter the application hands over; the
// tests hand over a fake that reports a change when they say so. What a change
// means -- growth, truncation, the file replaced under its name -- is not told
// here: the log data checks the file on disk once it hears of one.
//
// Its users call it, and hear from it, on the thread it lives in.
class FileWatchPort : public QObject {
    Q_OBJECT

public:
    explicit FileWatchPort( QObject* parent = nullptr );
    ~FileWatchPort() override;

    FileWatchPort( const FileWatchPort& ) = delete;
    FileWatchPort& operator=( const FileWatchPort& ) = delete;

    // Starts watching the file, if it is not watched already.
    virtual void addFile( const QString& fileName ) = 0;

    // Stops watching the file, if it is watched.
    virtual void removeFile( const QString& fileName ) = 0;

Q_SIGNALS:
    // A watched file changed on disk in some way.
    void fileChanged( const QString& fileName );
};
