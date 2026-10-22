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

#include "tablerowcells.h"

#include <algorithm>
#include <cmath>

TableRowCells::TableRowCells( const LogFormatDefinition& format, const QDate& modificationDate )
    : extractor_( format )
    , kind_( format.kind() )
    , fieldNames_( extractor_.columnNames() )
{
    if ( TimestampReader::isAvailableFor( format ) ) {
        timestampField_ = static_cast<int>( fieldNames_.indexOf( format.timestampField() ) );
        if ( timestampField_ >= 0 ) {
            elapsedColumn_ = timestampField_ + 1;
            reader_ = std::make_unique<const TimestampReader>( format, 0, modificationDate );
        }
    }

    const auto& values = format.valueDefinitions();
    sortKeyKinds_.assign( static_cast<size_t>( columnCount() ), SortKeyKind::Text );
    for ( int field = 0; field < fieldNames_.size(); ++field ) {
        auto kind = SortKeyKind::Text;
        if ( field == timestampField_ ) {
            kind = SortKeyKind::Timestamp;
        }
        else if ( const auto value = values.constFind( fieldNames_[ field ] );
                  value != values.constEnd()
                  && ( value->kind == QLatin1String( "integer" )
                       || value->kind == QLatin1String( "float" ) ) ) {
            kind = SortKeyKind::Number;
        }
        sortKeyKinds_[ static_cast<size_t>( columnOfField( field ) ) ] = kind;
    }
    if ( elapsedColumn_ >= 0 ) {
        sortKeyKinds_[ static_cast<size_t>( elapsedColumn_ ) ] = SortKeyKind::Elapsed;
    }
}

QString TableRowCells::formatElapsed( qint64 milliseconds )
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

namespace {

QString elapsedColumnName()
{
    return QString( QChar( 0x0394 ) ) + QLatin1Char( 't' );
}

} // namespace

QStringList TableRowCells::columnNames() const
{
    auto names = fieldNames_;
    if ( elapsedColumn_ >= 0 ) {
        names.insert( elapsedColumn_, elapsedColumnName() );
    }
    return names;
}

QString TableRowCells::columnName( int column ) const
{
    if ( column == elapsedColumn_ ) {
        return elapsedColumnName();
    }
    return fieldNames_.at( fieldOfColumn( column ) );
}

std::optional<QDateTime> TableRowCells::timestampOf( const QString& line ) const
{
    if ( !reader_ ) {
        return std::nullopt;
    }
    return reader_->timestampOf( line );
}

TableRowCells::Row TableRowCells::rowOf( LineNumber logLine, const QString& line,
                                         const TimestampOfLogLine& earlierTimestamp ) const
{
    const auto extracted = extractor_.extractFields( line );

    Row row;
    row.cells.resize( columnCount() );
    row.matchesFormat = extracted.isValid();
    row.undecided = extracted.isUndecided();
    if ( extracted.isValid() ) {
        for ( int field = 0; field < fieldNames_.size(); ++field ) {
            auto value = extracted.value( fieldNames_[ field ] );
            if ( !value.isEmpty() ) {
                row.cells[ columnOfField( field ) ] = std::move( value );
            }
        }
    }
    else if ( !fieldNames_.isEmpty() ) {
        // A Log Line the Log Format does not match: whole in the last field
        row.cells[ columnOfField( static_cast<int>( fieldNames_.size() ) - 1 ) ] = line;
    }

    if ( !reader_ ) {
        return row;
    }

    // The Timestamp comes from the field already extracted where the Log
    // Format is a regex one; the others read the Log Line.
    if ( kind_ == LogFormatKind::Regex ) {
        const auto& cell = row.cells[ columnOfField( timestampField_ ) ];
        if ( extracted.isValid() && !cell.isEmpty() ) {
            row.timestamp = reader_->parseField( cell );
        }
    }
    else {
        row.timestamp = reader_->timestampOf( line );
    }

    if ( row.timestamp ) {
        const auto lookBack = std::min( ElapsedLookBack, logLine.get() );
        for ( uint64_t back = 1; back <= lookBack; ++back ) {
            if ( const auto previous = earlierTimestamp( LineNumber( logLine.get() - back ) ) ) {
                row.elapsedMilliseconds = previous->msecsTo( *row.timestamp );
                row.cells[ elapsedColumn_ ] = formatElapsed( *row.elapsedMilliseconds );
                break;
            }
        }
    }
    return row;
}

TableRowCells::SortKeyKind TableRowCells::sortKeyKindOf( int column ) const
{
    if ( column < 0 || static_cast<size_t>( column ) >= sortKeyKinds_.size() ) {
        return SortKeyKind::Text;
    }
    return sortKeyKinds_[ static_cast<size_t>( column ) ];
}

TableRowCells::SortKey TableRowCells::sortKeyOf( int column, LineNumber logLine,
                                                 const QString& line,
                                                 const TimestampOfLogLine& earlierTimestamp ) const
{
    return sortKeyOf( column, rowOf( logLine, line, earlierTimestamp ) );
}

TableRowCells::SortKey TableRowCells::sortKeyOf( int column, const Row& row ) const
{
    SortKey key;
    if ( column < 0 || column >= columnCount() ) {
        return key;
    }
    switch ( sortKeyKindOf( column ) ) {
    case SortKeyKind::Elapsed:
        if ( row.elapsedMilliseconds ) {
            key.hasValue = true;
            key.milliseconds = *row.elapsedMilliseconds;
        }
        return key;
    case SortKeyKind::Timestamp:
        if ( row.matchesFormat && row.timestamp ) {
            key.hasValue = true;
            key.milliseconds = row.timestamp->toMSecsSinceEpoch();
        }
        return key;
    case SortKeyKind::Number: {
        const auto& cell = row.cells.at( column );
        if ( !row.matchesFormat || cell.isEmpty() ) {
            return key;
        }
        // An integer as written, else a floating-point number; a cell that
        // is neither has no value.
        bool isNumber = false;
        const auto trimmed = cell.trimmed();
        const auto integer = trimmed.toLongLong( &isNumber );
        if ( isNumber ) {
            key.number = static_cast<double>( integer );
        }
        else {
            key.number = trimmed.toDouble( &isNumber );
        }
        key.hasValue = isNumber && !std::isnan( key.number );
        return key;
    }
    case SortKeyKind::Text: {
        const auto& cell = row.cells.at( column );
        if ( !row.matchesFormat || cell.isEmpty() ) {
            return key;
        }
        key.hasValue = true;
        key.text = cell.toCaseFolded();
        return key;
    }
    }
    return key;
}
