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

#include "containers.h"
#include "linetypes.h"

class QAbstractItemModel;

// What is selected in the Table View: whole Rows, and possibly a run of
// characters inside one cell. It answers the two questions anyone asks of a
// selection -- which Log Lines are selected, and what text is selected -- and
// it is the one place deciding that an in-cell selection wins over the Rows.
//
// Rows map one-to-one onto Log Lines for now: a Row's index is its Log Line.
class TableViewSelection {
public:
    // A run of characters inside one cell. The ends are caret positions and
    // may be in either order: startChar is where the selection began.
    struct InCell {
        int row = -1;
        int column = -1;
        int startChar = 0;
        int endChar = 0;
    };

    // Replace the selected Rows, in the order they should be copied.
    void setRows( std::vector<int> rows );

    // Begin an in-cell selection with a caret at charPos: nothing selected yet.
    void startInCell( int row, int column, int charPos );
    // Move the end of the in-cell selection to charPos, if it is in that same
    // cell. Returns false, changing nothing, for any other cell.
    bool extendInCell( int row, int column, int charPos );
    // Select the characters from startChar to endChar in a cell.
    void selectInCell( int row, int column, int startChar, int endChar );
    void clearInCell();

    // The in-cell selection, including a bare caret selecting no character.
    std::optional<InCell> inCell() const;
    // True when at least one character is selected inside a cell.
    bool hasInCellSelection() const;

    // The Log Lines of the selected Rows.
    logsquirl::vector<LineNumber> selectedLogLines() const;

    // The characters of the in-cell selection if there are any, otherwise the
    // selected Rows, each as its cells separated by tabs, one Row per line.
    QString selectedText( const QAbstractItemModel& model ) const;

private:
    std::vector<int> rows_;
    std::optional<InCell> inCell_;
};
