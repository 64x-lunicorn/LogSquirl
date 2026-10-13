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

#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

class LogFormatDefinition;

// The Column Layout of a Log Format (CONTEXT.md): which columns of the Table
// View are shown, in which order and how wide. A column is numbered as the
// Log Format's table model numbers it, which keeps the Log Format's order; the
// layout says where each is shown. The elapsed-time column is a column like
// any other in it.
struct ColumnLayout {
    // Every column, in the order they are shown; a hidden column keeps its
    // place for when it is shown again.
    std::vector<int> order;
    // Whether each column, by its number, is hidden.
    std::vector<bool> hidden;
    // The width of each column, by its number, in pixels; 0 for a width the
    // layout does not know.
    std::vector<int> widths;

    // Every column shown, in the Log Format's order, no width known.
    static ColumnLayout defaultsFor( int columnCount );

    int columnCount() const;
    bool isHidden( int column ) const;
    // The columns shown, in their order.
    std::vector<int> shownColumns() const;
};

// The Column Layouts kept in the user's own settings, one group per Log
// Format, holding a fingerprint of its column names: when the Log Format's
// columns change, the whole layout is dropped and the defaults apply. The
// Table View saves the layout it shows and applies the one kept; the
// Filtered View's CSV export reads it. The column names are the Table View's
// (TableRowCells::columnNames(), which head its columns), handed in so that
// a save during a resize does not build them anew.

// The layout kept for format, whose columns are named columnNames; none when
// none is kept or the columns changed since. A width is 0 where none was
// kept. Settings from before the layout kept an order hold the widths alone.
std::optional<ColumnLayout> loadColumnLayout( const LogFormatDefinition& format,
                                              const QStringList& columnNames );

// Keeps layout for format, whose columns are named columnNames; a width of 0
// leaves the one kept before. A layout of another number of columns is not
// kept.
void storeColumnLayout( const LogFormatDefinition& format, const QStringList& columnNames,
                        const ColumnLayout& layout );

// Drops the layout kept for format: the defaults apply.
void dropColumnLayout( const LogFormatDefinition& format );

// The layout the Table View shows format with: the one kept, or the defaults.
ColumnLayout columnLayoutOf( const LogFormatDefinition& format );
