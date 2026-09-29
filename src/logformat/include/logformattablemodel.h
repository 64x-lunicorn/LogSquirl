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

#include "logformatdefinition.h"
#include "rowmapping.h"
#include "tablerowcells.h"

#include <QAbstractTableModel>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <list>
#include <memory>
#include <optional>
#include <vector>

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
//
// What each cell shows is TableRowCells' to compute; the model reads the Log
// Lines and keeps what it computed.
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
    static constexpr uint64_t ElapsedLookBack = TableRowCells::ElapsedLookBack;

    // The elapsed time as shown (see TableRowCells::formatElapsed()).
    static QString formatElapsed( qint64 milliseconds )
    {
        return TableRowCells::formatElapsed( milliseconds );
    }

    // The date the Log File was last written, which gives Timestamps without
    // a year theirs (ADR 0010). Optional; the current year is used without it.
    void setModificationDate( const QDate& date );

    // Whether the column is the elapsed-time one, which is not a field of the
    // Log Format.
    bool isElapsedColumn( int column ) const
    {
        return column >= 0 && column == cells_->elapsedColumn();
    }

    // A run of characters of a Log Line: from start up to, not including, end.
    struct TextSpan {
        int start = 0;
        int end = 0;
    };

    // Where the text each column of a Row shows lies in its Log Line, one
    // entry per column, in the characters of the raw line: none for a column
    // that shows no characters of it -- the elapsed time, a field that
    // captured nothing. A Log Line the Log Format does not match shows whole
    // in the last field's column. Nothing for a JSON or logfmt Log Format,
    // whose fields have no place in the Log Line, or for no Row.
    std::optional<std::vector<std::optional<TextSpan>>> columnSpans( int row ) const;

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
    // The Timestamp of a Log Line, none for one without; remembered, so that
    // scrolling does not read and parse the same Log Lines again.
    std::optional<QDateTime> timestampOfLogLine( LineNumber line ) const;
    void rememberTimestamp( uint64_t line, const std::optional<QDateTime>& timestamp ) const;
    void forgetTimestamps() const;

    const LogFormatDefinition& format_;
    // Computes the cells; made anew when the modification date changes.
    std::unique_ptr<const TableRowCells> cells_;
    QDate modificationDate_;
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
        QStringList cells;
    };

    // LRU cache for extracted rows (mutable because data() is const)
    static constexpr int RowCacheCapacity = 2000;
    mutable std::list<std::pair<int, CachedRow>> rowCacheList_;
    mutable QHash<int, std::list<std::pair<int, CachedRow>>::iterator> rowCacheMap_;

    // Look up or extract a row, caching the result.
    const CachedRow& cachedRow( int row ) const;
};
