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

#ifndef LOGSQUIRL_LINESSAVER_H
#define LOGSQUIRL_LINESSAVER_H

#include <functional>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include "atomicflag.h"
#include "containers.h"
#include "linetypes.h"

class QIODevice;
class QTextCodec;
class QWidget;

// Reads the text of count lines a text view displays, starting at position
// first in the view. A text view hands one over when a save starts, taken on
// the UI thread; it is then called off the UI thread, so nothing it reads may
// be changed by the UI thread while the save runs.
using DisplayedLinesReader
    = std::function<logsquirl::vector<QString>( LineNumber first, LinesCount count )>;

// Writes the lines at positions [begin, end) to output, encoded with codec
// (UTF-8 when null) and preceded by the Byte Order Mark of a Unicode codec.
//
// Blocks on the calling thread until the lines are written or interrupt is
// set. progress receives values from 0 to 1000, on any thread. Returns false
// when the save was interrupted or a write failed: output must then not be
// committed.
bool saveDisplayedLines( const DisplayedLinesReader& readLines, LineNumber begin, LineNumber end,
                         const QTextCodec* codec, QIODevice& output, const AtomicFlag& interrupt,
                         const std::function<void( int )>& progress );

// Runs saveDisplayedLines off the UI thread, and reports its progress and its
// end on the thread the LinesSaver lives in.
class LinesSaver : public QObject {
    Q_OBJECT
public:
    explicit LinesSaver( QObject* parent = nullptr );
    // Waits for a save still running.
    ~LinesSaver() override;

    // Starts saving. output and interrupt must outlive the save, and nothing
    // else may use output until finished() is emitted or waitForResult()
    // returned.
    void save( DisplayedLinesReader readLines, LineNumber begin, LineNumber end,
               const QTextCodec* codec, QIODevice* output, const AtomicFlag& interrupt );

    // Waits for the save to end; true if every line was written.
    bool waitForResult();

Q_SIGNALS:
    // A save's progress, from 0 to 1000.
    void progressed( int value );
    // The save ended; isOk is waitForResult().
    void finished( bool isOk );

private:
    QFuture<bool> future_;
    QFutureWatcher<bool> watcher_;
};

// Saves the lines at positions [begin, end) to filename with a LinesSaver,
// behind an application modal progress dialog shown over parent. filename is
// replaced only when every line was written: a cancelled or failed save leaves
// it as it was.
void saveLinesWithProgress( QWidget* parent, const QString& filename,
                            DisplayedLinesReader readLines, LineNumber begin, LineNumber end,
                            const QTextCodec* codec );

#endif // LOGSQUIRL_LINESSAVER_H
