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

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTemporaryDir>

#include <cstddef>

namespace logsquirl::plugins {

/**
 * Thread-safe file writer used by DataSource plugins to stream log lines.
 *
 * DataSource plugins push lines via the host API push_line/push_lines
 * callbacks.  StreamWriter appends them to a temporary file on disk.
 * The host opens this file with the normal LogData pipeline (which uses
 * FileWatcher to detect growth and re-index incrementally), giving us
 * full search, filter, and follow support with zero changes to the
 * existing log data infrastructure.
 *
 * The temporary file is automatically cleaned up on destruction.
 */
class StreamWriter {
  public:
    /**
     * Create a stream writer with the given display name.
     * @param displayName  Human-readable label shown in the tab title.
     */
    explicit StreamWriter( const QString& displayName );
    ~StreamWriter();

    StreamWriter( const StreamWriter& ) = delete;
    StreamWriter& operator=( const StreamWriter& ) = delete;

    /** Return the absolute path to the backing temporary file. */
    QString filePath() const;

    /** Return the display name for this stream. */
    const QString& displayName() const { return displayName_; }

    /**
     * Append a single line to the stream.
     * Thread-safe — may be called from any thread.
     * @param data  UTF-8 line content (newline is appended automatically).
     * @param len   Length of data in bytes.
     */
    void pushLine( const char* data, size_t len );

    /**
     * Append multiple lines to the stream in a single batch.
     * Thread-safe — may be called from any thread.
     */
    void pushLines( const char* const* data, const size_t* lens, size_t count );

    /** Mark the stream as complete (no more data expected). */
    void signalEos();

    /** Return true if end-of-stream has been signalled. */
    bool isFinished() const { return finished_; }

  private:
    QString displayName_;
    QTemporaryDir tempDir_;
    QFile file_;
    mutable QMutex mutex_;
    bool finished_ = false;
};

} // namespace logsquirl::plugins
