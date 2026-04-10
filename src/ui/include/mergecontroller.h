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
#include <QStringList>
#include <QTimer>

// Manages the creation and live-updating of a merged log file.
// Concatenates multiple source files into a single temporary file
// that can be opened as a regular LogData tab.
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
    // Actually performs the merge (writes the temp file).
    void doMerge();

    QStringList sourcePaths_;
    QString mergedFilePath_;
    bool dedup_ = false;
    QTimer rebuildTimer_;
};
