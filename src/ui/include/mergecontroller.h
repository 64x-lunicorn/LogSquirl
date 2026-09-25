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

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// Manages the creation and live-updating of a merged log file.
// Concatenates multiple source files into a single temporary file
// that can be opened as a regular LogData tab. The sources are watched:
// when one changes, the whole merged file is rewritten from what the sources
// contain at that moment (so lines appended to a source appear, and lines
// of a truncated or rewritten source disappear), and mergedFileUpdated()
// is emitted.
//
// On Windows the watcher reports a file as changed only when its modification
// time moves, and that time moves in ticks (on NTFS about every 15 ms): a
// write in the tick a watch began in goes unreported. So each time a source
// starts being watched, its size and modification time are compared once more
// shortly after with what the merge read, and a difference merges again (#500).
class MergeController : public QObject {
    Q_OBJECT

public:
    explicit MergeController( QObject* parent = nullptr );
    ~MergeController() override;

    // Sets the source file paths and the output temp file path.
    // Returns the temp file path where the merged content is written.
    QString merge( const QStringList& sourcePaths, bool dedup );

    // Returns the path to the merged temp file.
    QString mergedFilePath() const;

    // Returns the source paths.
    const QStringList& sourcePaths() const;

    // Triggers a re-merge of the source files (debounced, 300ms).
    void scheduleRebuild();

Q_SIGNALS:
    // Emitted after the merged temp file has been rewritten.
    void mergedFileUpdated();

private:
    // A source as the last merge found it on disk.
    struct SourceState {
        bool exists = false;
        qint64 size = 0;
        QDateTime modified;

        bool operator==( const SourceState& ) const = default;
    };

    static SourceState sourceStateOnDisk( const QString& path );

    // Starts watching every source that is not watched yet, and then checks
    // the sources once more (see recheckSources()).
    void watchSources();

    // Merges again if a source's size or modification time differs from what
    // the last merge read: the change the watcher may have missed.
    void recheckSources();

    // A source changed on disk.
    void onSourceChanged();

    // Actually performs the merge (writes the temp file).
    void doMerge();

    QStringList sourcePaths_;
    QString mergedFilePath_;
    bool dedup_ = false;
    QTimer rebuildTimer_;
    QTimer recheckTimer_;
    QHash<QString, SourceState> mergedSourceStates_;
    QFileSystemWatcher sourceWatcher_;
};
