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

#include <QAbstractTableModel>

#include "valuecount.h"

// The table model of a Value Count: a row per value, with its count and share.
// It holds the counted result and makes the text of a cell when the view asks
// for it, so that showing 100 000 values costs no more than showing a screenful.
class ValueCountModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ValueColumn, CountColumn, ShareColumn, ColumnCount };
    // Role: true for a row whose value is empty text, which is shown as
    // "(empty)" and is not something to search for.
    static constexpr int IsEmptyRole = Qt::UserRole;

    explicit ValueCountModel( QObject* parent = nullptr );

    // Shows this result in place of the one shown before.
    void setResult( ValueCountResult result );
    void clear();

    // The value of a row; empty for a row of no text.
    QString valueAt( int row ) const;

    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;

private:
    ValueCountResult result_;
};
