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

#include <cstdint>
#include <memory>
#include <vector>

#include <QStringList>

#include "linedecorator.h"
#include "linetypes.h"
#include "tableviewselection.h"

class LogFilteredData;
class QuickFindPattern;

// What the Table View shows over the text of its Rows, held once: by the
// Table View, which changes it, while its delegate reads it at paint time and
// keeps no copy of it (#561).
struct TableViewState {
    // The current Search, whose Marks and Matches are painted. Not owned.
    const LogFilteredData* currentSearch = nullptr;

    // The Search Limits last set: from searchStart up to, not including,
    // searchEnd. Without an end none were set, and no Row is subdued.
    LineNumber searchStart;
    OptionalLineNumber searchEnd;

    // The words of the Color Labels, one list per Color Label. Their colors
    // are the Theme's, read when a Row is painted.
    const std::vector<QStringList>& colorLabelWords() const
    {
        return colorLabelWords_;
    }
    void setColorLabelWords( const std::vector<QStringList>& words )
    {
        colorLabelWords_ = words;
        ++colorLabelsGeneration_;
    }
    // Changes whenever the words are set, so whoever builds something from
    // them learns that they changed without keeping a copy to compare.
    std::uint64_t colorLabelsGeneration() const
    {
        return colorLabelsGeneration_;
    }

    // The window's QuickFind pattern, whose matches are painted.
    std::shared_ptr<QuickFindPattern> quickFindPattern;

    // The Row under the mouse cursor, -1 for none.
    int hoverRow = -1;

    // The characters selected inside a cell; the selected Rows are the
    // table's own.
    TableViewSelection selection;

    // The Search Limits as the Line Decorator takes them.
    SearchLimits searchLimits() const
    {
        return searchEnd ? SearchLimits{ searchStart, *searchEnd } : SearchLimits{};
    }

private:
    std::vector<QStringList> colorLabelWords_;
    std::uint64_t colorLabelsGeneration_ = 0;
};
