/*
 * Copyright (C) 2010, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <QObject>

#include <memory>

#include "settingspolicies.h"

class EfswFileWatcher;
class QTimer;

namespace KDToolBox {
class KDGenericSignalThrottler;
}

struct EfswFileWatcherDeleter {
    void operator()( EfswFileWatcher* p ) const;
};

class FileWatcher : public QObject {
    Q_OBJECT
public:
    FileWatcher( const FileWatcher& ) = delete;
    FileWatcher( FileWatcher&& ) = delete;

    FileWatcher& operator=( const FileWatcher& ) = delete;
    FileWatcher& operator=( FileWatcher&& ) = delete;

    static FileWatcher& getFileWatcher();

    // Adds the file to the list of file to watch
    // (do nothing if a file is already monitored)
    void addFile( const QString& fileName );

    // Removes the file to the list of file to watch
    // (do nothing if said file is not monitored)
    void removeFile( const QString& fileName );

    // Follows the passed Watch Policy from now on: native watching,
    // polling and the poll interval, all three of them and nothing else.
    // Takes effect immediately on the files already being watched, so a
    // changed setting reaches a running watcher through this call and
    // through no other path.
    //
    // A FileWatcher that has never been given one follows a Policy that
    // watches nothing: this object cannot derive a Policy of its own (it
    // does not link the settings library, by design -- see the CMake
    // file), so whoever owns the settings has to hand it one before the
    // first file is added.
    void setWatchPolicy( const WatchPolicy& policy );

public Q_SLOTS:
    void fileChangedOnDisk( const QString& );

Q_SIGNALS:
    // Sent when the file on disk has changed in any way.
    void fileChanged( const QString& );
    void notifyFileChangedOnDisk();

private Q_SLOTS:
    void checkWatches();
    void sendChangesNotifications();

private:
    // Create an empty object
    FileWatcher();
    ~FileWatcher() override; // for complete EfswFileWatcher

    // Applies the currently held Policy to the watcher and the poll timer.
    void applyWatchPolicy();

    WatchPolicy watchPolicy_{};

    QTimer* checkTimer_;
    KDToolBox::KDGenericSignalThrottler* throttler_;
    std::vector<QString> changes_;

    std::unique_ptr<EfswFileWatcher, EfswFileWatcherDeleter> efswWatcher_;
};

#endif
