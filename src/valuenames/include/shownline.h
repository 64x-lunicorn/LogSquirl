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

#include <QList>
#include <QString>
#include <QStringView>

#include <utility>
#include <vector>

#include "valuenamer.h"

namespace logsquirl::valuenames {

// Where a column inside a Named Value goes: to the start or to the end of it.
enum class Snap {
    ToStart,
    ToEnd,
};

// A Log Line as it is shown with its Named Values: the text shown, and which
// column of the raw Log Line is which column of the text shown.
//
// A column is a position between two characters, from 0 to the length. One
// between two characters of a Named Value has no counterpart on the other
// side; it goes to the start or to the end of the value, as asked. So a range
// that takes in part of a Named Value takes in all of it: a Highlighter, a
// Search or the selection covers the whole name, and a copy the whole raw
// value.
//
// Columns are those of the text as read, before tabs are expanded: the text
// shown is expanded on its own, so that a tab after a Named Value stops where
// the shown text puts it.
class ShownLine {
public:
    // An empty Log Line with nothing named.
    ShownLine() = default;

    // namedValues are those ValueNamer::namedValues gave for rawLine.
    ShownLine( QStringView rawLine, QList<NamedValue> namedValues );

    bool hasNamedValues() const
    {
        return !namedValues_.isEmpty();
    }

    // The text shown.
    const QString& text() const
    {
        return text_;
    }

    const QList<NamedValue>& namedValues() const
    {
        return namedValues_;
    }

    // Where what the Named Value at index shows starts and ends in the text
    // shown.
    qsizetype shownStart( qsizetype index ) const;
    qsizetype shownEnd( qsizetype index ) const;

    // The column shown for a column of the raw Log Line.
    qsizetype toShown( qsizetype rawColumn, Snap snap ) const;
    // The raw column for a column of the text shown.
    qsizetype toRaw( qsizetype shownColumn, Snap snap ) const;

    // The index of the Named Value the character at the raw column, or at the
    // column shown, belongs to; -1 for one of none.
    qsizetype valueAtRaw( qsizetype rawColumn ) const;
    qsizetype valueAtShown( qsizetype shownColumn ) const;

    // The raw range [start, end) grown to take in every Named Value it takes
    // in part of.
    std::pair<qsizetype, qsizetype> wholeRawRange( qsizetype start, qsizetype end ) const;

private:
    // The index of the first Named Value whose text shown ends after the
    // column, or the count of them.
    qsizetype firstShownEndingAfter( qsizetype shownColumn ) const;

    QString text_;
    QList<NamedValue> namedValues_;
    // Where each Named Value's text starts in the text shown.
    std::vector<qsizetype> shownStarts_;
};

} // namespace logsquirl::valuenames
