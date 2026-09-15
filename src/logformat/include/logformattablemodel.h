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

#include <QAbstractTableModel>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <list>
#include <memory>

class AbstractLogData;

// Qt table model that presents parsed log lines as structured columns.
// Uses a virtual/lazy approach: fields are extracted on demand when data() is called,
// with an LRU cache to avoid re-extracting recently displayed rows.
// Column order matches the regex capture group order from the format definition.
// Non-matching lines show the raw text in the body column.
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

    // Notify the model that the Log File now has lineCount Log Lines.
    void setLineCount( int lineCount );

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
    QVector<QString> extractRow( const QString& line ) const;

    LogFieldExtractor extractor_;
    QStringList columnNames_;
    AbstractLogData* logData_;
    std::shared_ptr<const RowMapping> rows_;
    // The number of Rows
    int lineCount_ = 0;

    // Cached row entry: stores both the raw line (for RawLineRole) and
    // the extracted columns (for DisplayRole) to avoid repeated disk I/O.
    struct CachedRow {
        QString rawLine;
        QVector<QString> columns;
    };

    // LRU cache for extracted rows (mutable because data() is const)
    static constexpr int RowCacheCapacity = 2000;
    mutable std::list<std::pair<int, CachedRow>> rowCacheList_;
    mutable QHash<int, std::list<std::pair<int, CachedRow>>::iterator> rowCacheMap_;

    // Look up or extract a row, caching the result.
    const CachedRow& cachedRow( int row ) const;
};
