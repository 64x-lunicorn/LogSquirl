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

#include <QString>
#include <QStringView>

#include "valueproposal.h"

// What Chart this value... proposes for a number selected in a Log Line
// (#898): the chart series' pattern and name.
namespace logsquirl::chart {

// The number the range of the line is, and where: a decimal number, with a
// fraction or not, negative or not, with nothing else in the range but blanks.
// A minus sign right before the digits is the number's, selected or not,
// unless a word comes before it (id-5). None for anything else: more text, part of a
// number, a hexadecimal value, which a chart cannot plot.
std::optional<valuenames::LineRange> numberSelected( QStringView line,
                                                     valuenames::LineRange selection );

// The chart series proposed for the number selected.
struct ChartValueProposal {
    // A regex of one capture group, the number: any decimal number after the
    // text before it in the line, escaped, any number in that text not part
    // of a word taken as any number too. That text is the word before the
    // number and what separates them, or more of the Log Line where that
    // would capture an earlier number in it; the start of the Log Line, ^,
    // when the number starts it. In the line, its first match captures
    // exactly the number selected.
    QString pattern;
    // The last word with a letter before the number, empty when there is
    // none.
    QString seriesName;
};

// None when the selection is no number (numberSelected()).
std::optional<ChartValueProposal> proposeChartValue( QStringView line,
                                                     valuenames::LineRange selection );

} // namespace logsquirl::chart
