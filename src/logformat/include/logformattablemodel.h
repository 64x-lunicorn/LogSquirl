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

#include "logfieldextractor.h"
#include "logformatdefinition.h"
#include "rowmapping.h"
#include "timestampreader.h"

#include <QAbstractTableModel>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <list>
#include <memory>
#include <optional>

class AbstractLogData;

// Qt table model that presents parsed log lines as structured columns.
// Uses a virtual/lazy approach: fields are extracted on demand when data() is called,
// with an LRU cache to avoid re-extracting recently displayed rows.
// Column order matches the regex capture group order from the format definition.
// Non-matching lines show the raw text in the body column.
//
// When the Log Format has a timestamp field the model has one more column
// after it, the time elapsed since the previous Log Line of the Log File that
// has a Timestamp (Timestamps are read by the TimestampReader, #435). It is
// empty for a Log Line without a Timestamp, for the first one with a
// Timestamp, and when no Log Line within ElapsedLookBack before it has one.
class LogFormatTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    // Custom role for retrieving the raw (unparsed) log line text.
    static constexpr int RawLineRole = Qt::UserRole + 1;

    // Construct the model from a log format definition and log data source.
    // The model owns neither format nor logData — the caller must ensure both
    // outlive the model. Each Row shows one Log Line.
    LogFormatTableModel( const LogFormatDefinition& format, AbstractLogData* logData,
                         QObject* parent = nullptr );
    // Each Row shows the Log Line rows maps it onto.
    LogFormatTableModel( const LogFormatDefinition& format, AbstractLogData* logData,
                         std::shared_ptr<const RowMapping> rows, QObject* parent = nullptr );

    // How many Log Lines before a Row are looked at for the previous
    // Timestamp; beyond that the elapsed time is left empty.
    static constexpr uint64_t ElapsedLookBack = 100;

    // The elapsed time as shown: "+0.004s", "+12.3s", "+5m02s", "+1h05m",
    // "+2d03h", with a "-" for a negative one (Log Lines out of order).
    static QString formatElapsed( qint64 milliseconds );

    // The date the Log File was last written, which gives Timestamps without
    // a year theirs (ADR 0010). Optional; the current year is used without it.
    void setModificationDate( const QDate& date );

    // Whether the column is the elapsed-time one, which is not a field of the
    // Log Format.
    bool isElapsedColumn( int column ) const
    {
        return column >= 0 && column == elapsedColumn_;
    }

    // Notify the model that the Log File now has lineCount Log Lines.
    void setLineCount( int lineCount );

    // Forget the Rows read so far, and tell the views every Row changed: the
    // Log Lines read differently now, though there are as many of them.
    void rereadRows();

    // Return the raw logData pointer so callers can detect stale references.
    const AbstractLogData* logDataPtr() const
    {
        return logData_;
    }

    // QAbstractTableModel interface
    int rowCount( const QModelIndex& parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex& parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;

private:
    // Extracts fields from a single line into a row of column values.
    // matched tells whether the line matched the Log Format.
    QVector<QString> extractRow( const QString& line, bool& matched ) const;

    // The column of the Log Format's fields behind a model column.
    int fieldColumn( int column ) const
    {
        return elapsedColumn_ >= 0 && column > elapsedColumn_ ? column - 1 : column;
    }

    // The Timestamp of a Log Line, none for one without; remembered, so that
    // scrolling does not read and parse the same Log Lines again.
    std::optional<QDateTime> timestampOfLogLine( LineNumber line ) const;
    void rememberTimestamp( uint64_t line, const std::optional<QDateTime>& timestamp ) const;
    // The elapsed time shown for a Row, given its Timestamp.
    QString elapsedBefore( LineNumber line, const std::optional<QDateTime>& timestamp ) const;
    void forgetTimestamps() const;

    LogFieldExtractor extractor_;
    const LogFormatDefinition& format_;
    QStringList columnNames_;
    // Reads Timestamps; none when the Log Format has no timestamp field.
    std::unique_ptr<TimestampReader> reader_;
    QDate modificationDate_;
    // The model column with the elapsed time, -1 when there is none.
    int elapsedColumn_ = -1;
    // The column of the timestamp field among the Log Format's fields.
    int timestampField_ = -1;
    static constexpr int TimestampCacheCapacity = 16384;
    mutable QHash<uint64_t, std::optional<QDateTime>> timestampCache_;
    AbstractLogData* logData_;
    std::shared_ptr<const RowMapping> rows_;
    // The number of Rows
    int lineCount_ = 0;

    // Cached row entry: stores both the raw line (for RawLineRole) and
    // the extracted columns (for DisplayRole) to avoid repeated disk I/O.
    struct CachedRow {
        QString rawLine;
        QVector<QString> columns;
        QString elapsed;
    };

    // LRU cache for extracted rows (mutable because data() is const)
    static constexpr int RowCacheCapacity = 2000;
    mutable std::list<std::pair<int, CachedRow>> rowCacheList_;
    mutable QHash<int, std::list<std::pair<int, CachedRow>>::iterator> rowCacheMap_;

    // Look up or extract a row, caching the result.
    const CachedRow& cachedRow( int row ) const;
};
