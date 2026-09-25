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

#include "valuecountmodel.h"

#include <QLocale>

ValueCountModel::ValueCountModel( QObject* parent )
    : QAbstractTableModel( parent )
{
}

void ValueCountModel::setResult( ValueCountResult result )
{
    beginResetModel();
    result_ = std::move( result );
    endResetModel();
}

void ValueCountModel::clear()
{
    setResult( {} );
}

QString ValueCountModel::valueAt( int row ) const
{
    return row >= 0 && row < result_.entries.size() ? result_.entries[ row ].value : QString();
}

int ValueCountModel::rowCount( const QModelIndex& parent ) const
{
    return parent.isValid() ? 0 : static_cast<int>( result_.entries.size() );
}

int ValueCountModel::columnCount( const QModelIndex& parent ) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ValueCountModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() || index.row() >= result_.entries.size() ) {
        return {};
    }
    const auto& entry = result_.entries[ index.row() ];

    if ( role == IsEmptyRole ) {
        return index.column() == ValueColumn ? QVariant( entry.value.isEmpty() ) : QVariant();
    }
    if ( role == Qt::TextAlignmentRole ) {
        return index.column() == ValueColumn ? QVariant()
                                             : QVariant( Qt::AlignRight | Qt::AlignVCenter );
    }
    if ( role != Qt::DisplayRole ) {
        return {};
    }

    switch ( index.column() ) {
    case ValueColumn:
        return entry.value.isEmpty() ? tr( "(empty)" ) : entry.value;
    case CountColumn:
        return QLocale().toString( entry.count );
    case ShareColumn:
        return tr( "%1 %" ).arg(
            QLocale().toString( result_.sharePercent( entry.count ), 'f', 2 ) );
    default:
        return {};
    }
}

QVariant ValueCountModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) {
        return {};
    }
    switch ( section ) {
    case ValueColumn:
        return tr( "Value" );
    case CountColumn:
        return tr( "Count" );
    case ShareColumn:
        return tr( "Share" );
    default:
        return {};
    }
}
