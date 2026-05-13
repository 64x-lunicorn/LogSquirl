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

#include "logformattablemodel.h"
#include "abstractlogdata.h"

#include <algorithm>

LogFormatTableModel::LogFormatTableModel( const LogFormatDefinition& format,
                                          AbstractLogData* logData, QObject* parent )
    : QAbstractTableModel( parent )
    , format_( format )
    , extractor_( format )
    , columnNames_( extractor_.columnNames() )
    , logData_( logData )
{
}

void LogFormatTableModel::setLineCount( int lineCount )
{
    if ( lineCount == lineCount_ ) {
        return;
    }

    if ( lineCount < lineCount_ ) {
        // Lines removed — full reset
        beginResetModel();
        rowCacheList_.clear();
        rowCacheMap_.clear();
        lineCount_ = lineCount;
        endResetModel();
    }
    else if ( lineCount_ == 0 || ( lineCount - lineCount_ ) > 10000 ) {
        // Large batch or initial load — use reset to avoid per-row overhead
        beginResetModel();
        lineCount_ = lineCount;
        endResetModel();
    }
    else {
        // Small incremental append
        beginInsertRows( QModelIndex(), lineCount_, lineCount - 1 );
        lineCount_ = lineCount;
        endInsertRows();
    }
}

int LogFormatTableModel::rowCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return lineCount_;
}

int LogFormatTableModel::columnCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return static_cast<int>( columnNames_.size() );
}

QVariant LogFormatTableModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() ) {
        return {};
    }

    const int row = index.row();

    if ( row < 0 || row >= lineCount_ ) {
        return {};
    }

    const auto& cached = cachedRow( row );

    // Return the raw unparsed log line for highlighter matching in the delegate.
    if ( role == RawLineRole ) {
        return cached.rawLine;
    }

    if ( role != Qt::DisplayRole ) {
        return {};
    }

    const int col = index.column();
    if ( col < 0 || col >= columnNames_.size() ) {
        return {};
    }

    return cached.columns[ col ];
}

QVariant LogFormatTableModel::headerData( int section, Qt::Orientation orientation,
                                          int role ) const
{
    if ( role != Qt::DisplayRole || orientation != Qt::Horizontal ) {
        return {};
    }

    if ( section < 0 || section >= columnNames_.size() ) {
        return {};
    }

    return columnNames_[ section ];
}

const LogFormatTableModel::CachedRow& LogFormatTableModel::cachedRow( int row ) const
{
    auto it = rowCacheMap_.find( row );
    if ( it != rowCacheMap_.end() ) {
        // Move to front (most recently used)
        rowCacheList_.splice( rowCacheList_.begin(), rowCacheList_, it.value() );
        return it.value()->second;
    }

    // Extract from logData_ — read the line once from disk
    auto line = logData_->getLineString( LineNumber( static_cast<uint64_t>( row ) ) );
    auto extracted = extractRow( line );

    // Evict oldest if cache is full
    if ( static_cast<int>( rowCacheMap_.size() ) >= RowCacheCapacity ) {
        auto oldest = rowCacheList_.back().first;
        rowCacheMap_.remove( oldest );
        rowCacheList_.pop_back();
    }

    CachedRow entry{ std::move( line ), std::move( extracted ) };
    rowCacheList_.emplace_front( row, std::move( entry ) );
    rowCacheMap_[ row ] = rowCacheList_.begin();
    return rowCacheList_.front().second;
}

QVector<QString> LogFormatTableModel::extractRow( const QString& line ) const
{
    auto fields = extractor_.extractFields( line, -1 );

    QVector<QString> row( columnNames_.size() );

    if ( fields.isValid() ) {
        for ( int i = 0; i < columnNames_.size(); ++i ) {
            const auto& colName = columnNames_[ i ];
            const auto val = fields.value( colName );
            if ( !val.isEmpty() ) {
                row[ i ] = val;
            }
        }
    }
    else {
        // Non-matching line: put the raw line in the body column (last)
        if ( !columnNames_.isEmpty() ) {
            row[ columnNames_.size() - 1 ] = line;
        }
    }

    return row;
}
