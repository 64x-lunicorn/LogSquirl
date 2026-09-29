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
    : LogFormatTableModel( format, logData, std::make_shared<OneRowPerLogLine>(), parent )
{
}

LogFormatTableModel::LogFormatTableModel( const LogFormatDefinition& format,
                                          AbstractLogData* logData,
                                          std::shared_ptr<const RowMapping> rows, QObject* parent )
    : QAbstractTableModel( parent )
    , format_( format )
    , cells_( std::make_unique<const TableRowCells>( format ) )
    , logData_( logData )
    , rows_( std::move( rows ) )
{
}

void LogFormatTableModel::setModificationDate( const QDate& date )
{
    if ( date == modificationDate_ || cells_->elapsedColumn() < 0 ) {
        return;
    }
    modificationDate_ = date;
    cells_ = std::make_unique<const TableRowCells>( format_, date );
    forgetTimestamps();
    rereadRows();
}

void LogFormatTableModel::forgetTimestamps() const
{
    timestampCache_.clear();
}

void LogFormatTableModel::rememberTimestamp( uint64_t line,
                                             const std::optional<QDateTime>& timestamp ) const
{
    if ( timestampCache_.size() >= TimestampCacheCapacity ) {
        timestampCache_.clear();
    }
    timestampCache_.insert( line, timestamp );
}

std::optional<QDateTime> LogFormatTableModel::timestampOfLogLine( LineNumber line ) const
{
    const auto known = timestampCache_.constFind( line.get() );
    if ( known != timestampCache_.constEnd() ) {
        return known.value();
    }
    auto timestamp = cells_->timestampOf( logData_->getLineString( line ) );
    rememberTimestamp( line.get(), timestamp );
    return timestamp;
}

void LogFormatTableModel::setLineCount( int logLineCount )
{
    const int lineCount
        = rows_->rowCount( LinesCount( static_cast<uint64_t>( std::max( logLineCount, 0 ) ) ) );

    if ( lineCount == lineCount_ ) {
        return;
    }

    if ( lineCount < lineCount_ ) {
        // Lines removed — full reset
        beginResetModel();
        rowCacheList_.clear();
        rowCacheMap_.clear();
        forgetTimestamps();
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

void LogFormatTableModel::rereadRows()
{
    rowCacheList_.clear();
    rowCacheMap_.clear();
    forgetTimestamps();

    // Not a reset: the Rows stay where they are, and so do the selection and
    // the scroll position.
    if ( lineCount_ > 0 && !cells_->fieldNames().isEmpty() ) {
        const auto lastColumn = columnCount() - 1;
        Q_EMIT dataChanged( index( 0, 0 ), index( lineCount_ - 1, lastColumn ) );
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
    return cells_->columnCount();
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
    if ( col < 0 || col >= columnCount() ) {
        return {};
    }
    return cached.cells.at( col );
}

QVariant LogFormatTableModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( role != Qt::DisplayRole || orientation != Qt::Horizontal ) {
        return {};
    }

    if ( section < 0 || section >= columnCount() ) {
        return {};
    }
    return cells_->columnName( section );
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
    const auto logLine = rows_->logLineAt( row );
    auto line = logData_->getLineString( logLine );
    auto computed = cells_->rowOf(
        logLine, line, [ this ]( LineNumber earlier ) { return timestampOfLogLine( earlier ); } );
    if ( cells_->elapsedColumn() >= 0 ) {
        rememberTimestamp( logLine.get(), computed.timestamp );
    }

    // Evict oldest if cache is full
    if ( static_cast<int>( rowCacheMap_.size() ) >= RowCacheCapacity ) {
        auto oldest = rowCacheList_.back().first;
        rowCacheMap_.remove( oldest );
        rowCacheList_.pop_back();
    }

    CachedRow entry{ std::move( line ), std::move( computed.cells ) };
    rowCacheList_.emplace_front( row, std::move( entry ) );
    rowCacheMap_[ row ] = rowCacheList_.begin();
    return rowCacheList_.front().second;
}

std::optional<std::vector<std::optional<LogFormatTableModel::TextSpan>>>
LogFormatTableModel::columnSpans( int row ) const
{
    const auto& fieldNames = cells_->fieldNames();
    if ( format_.kind() != LogFormatKind::Regex || row < 0 || row >= lineCount_
         || fieldNames.isEmpty() ) {
        return std::nullopt;
    }

    const auto& line = cachedRow( row ).rawLine;
    std::vector<std::optional<TextSpan>> spans( static_cast<size_t>( columnCount() ) );
    const auto fieldSpans = cells_->extractor().fieldSpans( line );
    if ( !fieldSpans ) {
        // As extractRow() shows it: the whole Log Line in the last column
        const auto lastField = static_cast<int>( fieldNames.size() ) - 1;
        if ( !line.isEmpty() ) {
            spans[ static_cast<size_t>( cells_->columnOfField( lastField ) ) ]
                = TextSpan{ 0, static_cast<int>( line.size() ) };
        }
        return spans;
    }

    for ( int field = 0; field < fieldNames.size(); ++field ) {
        const auto span = fieldSpans->constFind( fieldNames[ field ] );
        if ( span != fieldSpans->constEnd() ) {
            spans[ static_cast<size_t>( cells_->columnOfField( field ) ) ]
                = TextSpan{ span->first, span->second };
        }
    }
    return spans;
}
