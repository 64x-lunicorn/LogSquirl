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
    , extractor_( format )
    , format_( format )
    , columnNames_( extractor_.columnNames() )
    , logData_( logData )
    , rows_( std::move( rows ) )
{
    if ( TimestampReader::isAvailableFor( format ) ) {
        timestampField_ = static_cast<int>( columnNames_.indexOf( format.timestampField() ) );
        if ( timestampField_ >= 0 ) {
            elapsedColumn_ = timestampField_ + 1;
            reader_ = std::make_unique<TimestampReader>( format );
        }
    }
}

QString LogFormatTableModel::formatElapsed( qint64 milliseconds )
{
    const auto sign = milliseconds < 0 ? QLatin1Char( '-' ) : QLatin1Char( '+' );
    const auto magnitude = milliseconds < 0 ? -milliseconds : milliseconds;

    if ( magnitude < 1000 ) {
        return QString( sign )
               + QStringLiteral( "0.%1s" ).arg( magnitude, 3, 10, QLatin1Char( '0' ) );
    }
    // Rounded before it is cut into units, so 59.96 s does not read "60.0s".
    const auto tenths = ( magnitude + 50 ) / 100;
    if ( tenths < 600 ) {
        return QStringLiteral( "%1%2.%3s" ).arg( sign ).arg( tenths / 10 ).arg( tenths % 10 );
    }
    const auto seconds = ( magnitude + 500 ) / 1000;
    if ( seconds < 3600 ) {
        return QStringLiteral( "%1%2m%3s" )
            .arg( sign )
            .arg( seconds / 60 )
            .arg( seconds % 60, 2, 10, QLatin1Char( '0' ) );
    }
    const auto minutes = ( magnitude + 30'000 ) / 60'000;
    if ( minutes < 24 * 60 ) {
        return QStringLiteral( "%1%2h%3m" )
            .arg( sign )
            .arg( minutes / 60 )
            .arg( minutes % 60, 2, 10, QLatin1Char( '0' ) );
    }
    const auto hours = ( magnitude + 1'800'000 ) / 3'600'000;
    return QStringLiteral( "%1%2d%3h" )
        .arg( sign )
        .arg( hours / 24 )
        .arg( hours % 24, 2, 10, QLatin1Char( '0' ) );
}

void LogFormatTableModel::setModificationDate( const QDate& date )
{
    if ( date == modificationDate_ || !reader_ ) {
        return;
    }
    modificationDate_ = date;
    reader_ = std::make_unique<TimestampReader>( format_, 0, date );
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
    auto timestamp = reader_->timestampOf( logData_->getLineString( line ) );
    rememberTimestamp( line.get(), timestamp );
    return timestamp;
}

QString LogFormatTableModel::elapsedBefore( LineNumber line,
                                            const std::optional<QDateTime>& timestamp ) const
{
    if ( !timestamp ) {
        return {};
    }
    const auto lookBack = std::min( ElapsedLookBack, line.get() );
    for ( uint64_t back = 1; back <= lookBack; ++back ) {
        if ( const auto previous = timestampOfLogLine( LineNumber( line.get() - back ) ) ) {
            return formatElapsed( previous->msecsTo( *timestamp ) );
        }
    }
    return {};
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
    if ( lineCount_ > 0 && !columnNames_.isEmpty() ) {
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
    return static_cast<int>( columnNames_.size() ) + ( elapsedColumn_ >= 0 ? 1 : 0 );
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
    if ( col == elapsedColumn_ ) {
        return cached.elapsed;
    }

    return cached.columns[ fieldColumn( col ) ];
}

QVariant LogFormatTableModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( role != Qt::DisplayRole || orientation != Qt::Horizontal ) {
        return {};
    }

    if ( section < 0 || section >= columnCount() ) {
        return {};
    }
    if ( section == elapsedColumn_ ) {
        return QString( QChar( 0x0394 ) ) + QLatin1Char( 't' );
    }

    return columnNames_[ fieldColumn( section ) ];
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
    auto line = logData_->getLineString( rows_->logLineAt( row ) );
    bool fieldsMatched = false;
    auto extracted = extractRow( line, fieldsMatched );

    // Evict oldest if cache is full
    if ( static_cast<int>( rowCacheMap_.size() ) >= RowCacheCapacity ) {
        auto oldest = rowCacheList_.back().first;
        rowCacheMap_.remove( oldest );
        rowCacheList_.pop_back();
    }

    QString elapsed;
    if ( reader_ ) {
        // The Timestamp comes from the field already extracted where the Log
        // Format is a regex one; the others read the Log Line.
        const auto logLine = rows_->logLineAt( row );
        std::optional<QDateTime> timestamp;
        const auto known = timestampCache_.constFind( logLine.get() );
        if ( known != timestampCache_.constEnd() ) {
            timestamp = known.value();
        }
        else {
            if ( format_.kind() == LogFormatKind::Regex ) {
                const auto& cell = extracted[ timestampField_ ];
                if ( !cell.isEmpty() && fieldsMatched ) {
                    timestamp = reader_->parseField( cell );
                }
            }
            else {
                timestamp = reader_->timestampOf( line );
            }
            rememberTimestamp( logLine.get(), timestamp );
        }
        elapsed = elapsedBefore( logLine, timestamp );
    }

    CachedRow entry{ std::move( line ), std::move( extracted ), std::move( elapsed ) };
    rowCacheList_.emplace_front( row, std::move( entry ) );
    rowCacheMap_[ row ] = rowCacheList_.begin();
    return rowCacheList_.front().second;
}

QVector<QString> LogFormatTableModel::extractRow( const QString& line, bool& matched ) const
{
    auto fields = extractor_.extractFields( line );
    matched = fields.isValid();

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
