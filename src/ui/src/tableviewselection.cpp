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

#include "tableviewselection.h"

#include <algorithm>

#include <QAbstractItemModel>
#include <QStringList>

#include "rowmapping.h"

void TableViewSelection::setRows( std::vector<int> rows )
{
    rows_ = std::move( rows );
}

void TableViewSelection::startInCell( int row, int column, int charPos )
{
    inCell_ = InCell{ row, column, charPos, charPos };
}

bool TableViewSelection::extendInCell( int row, int column, int charPos )
{
    if ( !inCell_ || inCell_->row != row || inCell_->column != column ) {
        return false;
    }
    inCell_->endChar = charPos;
    return true;
}

void TableViewSelection::selectInCell( int row, int column, int startChar, int endChar )
{
    inCell_ = InCell{ row, column, startChar, endChar };
}

void TableViewSelection::clearInCell()
{
    inCell_.reset();
}

std::optional<TableViewSelection::InCell> TableViewSelection::inCell() const
{
    return inCell_;
}

bool TableViewSelection::hasInCellSelection() const
{
    return inCell_ && inCell_->startChar != inCell_->endChar;
}

logsquirl::vector<LineNumber> TableViewSelection::selectedLogLines( const RowMapping& rows ) const
{
    logsquirl::vector<LineNumber> lines;
    lines.reserve( rows_.size() );
    for ( const auto row : rows_ ) {
        lines.push_back( rows.logLineAt( row ) );
    }
    return lines;
}

QString TableViewSelection::selectedText( const QAbstractItemModel& model ) const
{
    if ( inCell_ && hasInCellSelection() ) {
        const auto cellText
            = model.index( inCell_->row, inCell_->column ).data( Qt::DisplayRole ).toString();
        const int lo = std::min( inCell_->startChar, inCell_->endChar );
        const int hi = std::min( std::max( inCell_->startChar, inCell_->endChar ),
                                 static_cast<int>( cellText.size() ) );
        return lo < hi ? cellText.mid( lo, hi - lo ) : QString{};
    }

    QStringList lines;
    lines.reserve( static_cast<qsizetype>( rows_.size() ) );
    const int columnCount = model.columnCount();
    for ( const auto row : rows_ ) {
        QStringList cells;
        cells.reserve( columnCount );
        for ( int column = 0; column < columnCount; ++column ) {
            cells << model.index( row, column ).data( Qt::DisplayRole ).toString();
        }
        lines << cells.join( '\t' );
    }
    return lines.join( '\n' );
}
