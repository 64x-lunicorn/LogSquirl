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

#include <atomic>
#include <cstdint>
#include <vector>

#include <QString>
#include <Qt>

#include "tablerowcells.h"

// The keys one column of the Table View sorts its Rows by (Sort Order): one
// per Log Line, from the first Log Line on, as TableRowCells reads them. Kept
// once read, so that the Log Lines a followed Log File appends are keyed
// alone, and the other direction of the same column needs no key read again.
//
// The keys say how two Log Lines compare; which Log Lines are Rows, and in
// what order they end up, is sortedLogLines() and mergeSortedLogLines() below:
// the set of Rows is their input, so a mapping that narrows the Rows (Matches
// Only, #798) orders its subset with the same keys.
//
// Immutable once handed to a worker or a view: appending makes a copy.
class TableSortKeys {
public:
    using Kind = TableRowCells::SortKeyKind;
    using Key = TableRowCells::SortKey;

    explicit TableSortKeys( Kind kind );

    Kind kind() const
    {
        return kind_;
    }
    // The Log Lines keyed: [0, size()).
    uint32_t size() const
    {
        return static_cast<uint32_t>( hasValue_.size() );
    }
    void reserve( size_t lines );
    // The key of the next Log Line, size().
    void append( const Key& key );

    bool hasValue( uint32_t logLine ) const
    {
        return hasValue_[ logLine ] != 0;
    }

    // Whether Log Line a comes before Log Line b in the given direction: by
    // value in that direction; two of equal value in Log Line order; a Log
    // Line without a value after every one with, in both directions, and
    // among themselves in Log Line order.
    bool before( uint32_t a, uint32_t b, Qt::SortOrder order ) const;

private:
    // The values of a and b compared: negative, 0 or positive. Both must
    // have a value.
    int compareValues( uint32_t a, uint32_t b ) const;

    Kind kind_;
    std::vector<uint8_t> hasValue_;
    // By kind, one of these holds the values; the others stay empty.
    std::vector<QString> text_;
    std::vector<double> numbers_;
    std::vector<qint64> milliseconds_;
};

// A run of Rows: count Rows from first on.
struct RowRun {
    int first = 0;
    int count = 0;

    bool operator==( const RowRun& ) const = default;
};

// The Log Lines given, each keyed, in the order their keys put them, in the
// direction given: Row n shows the Log Line at n. Which Log Lines are Rows is
// the caller's: every Log Line of the Log File, or the Rows of a mapping that
// narrows them. Empty when cancel was set before it was done. Safe off the UI
// thread.
std::vector<uint32_t> sortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                      std::vector<uint32_t> logLines,
                                      const std::atomic<bool>& cancel );
// Every Log Line of keys in that order.
std::vector<uint32_t> sortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                      const std::atomic<bool>& cancel );

// The Log Lines sorted merged with the ones appended.
struct MergedLogLines {
    // Every Log Line of both in order, as sortedLogLines() gives them.
    std::vector<uint32_t> logLines;
    // Where the Log Lines merged in landed: the runs of Rows that are new,
    // in order of their first Row.
    std::vector<RowRun> inserted;
};

// Merges the Log Lines appended, each keyed, into sorted, which
// sortedLogLines() ordered with the same keys and direction: the result is
// what sortedLogLines() would give over all of them, found without sorting
// the ones already in order. Empty when cancel was set before it was done.
// Safe off the UI thread.
MergedLogLines mergeSortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                    const std::vector<uint32_t>& sorted,
                                    std::vector<uint32_t> appended,
                                    const std::atomic<bool>& cancel );
