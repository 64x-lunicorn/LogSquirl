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

// The keys the Table View sorts by (Sort Order, #797): what TableRowCells
// reads for each kind of column, how the keys order Log Lines, and the sorted
// Row Mapping built from them.

#include <catch2/catch_test_macros.hpp>

#include <atomic>

#include <QHash>
#include <QString>

#include "fake_log_data.h"
#include "logformatdefinition.h"
#include "rowmapping.h"
#include "tablerowcells.h"
#include "tablesortkeys.h"

namespace {

// timestamp, Δt, level (text), size (integer), ratio (float), body
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "tablesortkeys_test" );
    format.setTitle( "Sort keys test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) )"
                       R"((?<level>\w+) (?<size>\S*) (?<ratio>\S*) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );
    QHash<QString, LogFormatValueDef> values;
    values[ "size" ] = LogFormatValueDef{ "integer", false, false };
    values[ "ratio" ] = LogFormatValueDef{ "float", false, false };
    format.setValueDefinitions( values );
    return format;
}

const QStringList Lines = {
    "2026-01-01 10:00:00.000 INFO 30 0.5 beta",   // 0
    "2026-01-01 10:00:02.000 WARN 7 1.25 Alpha",  // 1
    "not a log line",                             // 2
    "2026-01-01 10:00:01.000 ERROR 100 -2 gamma", // 3
    "2026-01-01 10:00:03.000 DEBUG - x delta",    // 4: no number in size or ratio
    "2026-01-01 10:00:03.500 info 30 0.5 BETA",   // 5: ties with 0 in level, size, ratio
};

int columnOf( const TableRowCells& cells, const QString& name )
{
    return static_cast<int>( cells.columnNames().indexOf( name ) );
}

// The keys of every Log Line of Lines in column, read as the sort reads
// them: the Timestamps of the earlier Log Lines are those read before.
TableSortKeys keysOf( const TableRowCells& cells, int column )
{
    TableSortKeys keys( cells.sortKeyKindOf( column ) );
    std::vector<std::optional<QDateTime>> timestamps;
    for ( int line = 0; line < Lines.size(); ++line ) {
        const auto row = cells.rowOf( LineNumber( static_cast<uint64_t>( line ) ), Lines[ line ],
                                      [ &timestamps ]( LineNumber earlier ) {
                                          return timestamps[ static_cast<size_t>( earlier.get() ) ];
                                      } );
        timestamps.push_back( row.timestamp );
        keys.append( cells.sortKeyOf( column, row ) );
    }
    return keys;
}

std::vector<uint32_t> sorted( const TableSortKeys& keys, Qt::SortOrder order )
{
    const std::atomic<bool> cancel{ false };
    return sortedLogLines( keys, order, cancel );
}

} // namespace

SCENARIO( "A column's cells sort by the kind of its field", "[tablesortkeys][sortorder]" )
{
    const auto format = makeFormat();
    const TableRowCells cells( format );

    THEN( "the timestamp field sorts by Timestamp, the elapsed column by duration, a number "
          "field by number and the others by text" )
    {
        REQUIRE( cells.sortKeyKindOf( columnOf( cells, "timestamp" ) )
                 == TableRowCells::SortKeyKind::Timestamp );
        REQUIRE( cells.sortKeyKindOf( cells.elapsedColumn() )
                 == TableRowCells::SortKeyKind::Elapsed );
        REQUIRE( cells.sortKeyKindOf( columnOf( cells, "size" ) )
                 == TableRowCells::SortKeyKind::Number );
        REQUIRE( cells.sortKeyKindOf( columnOf( cells, "ratio" ) )
                 == TableRowCells::SortKeyKind::Number );
        REQUIRE( cells.sortKeyKindOf( columnOf( cells, "level" ) )
                 == TableRowCells::SortKeyKind::Text );
        REQUIRE( cells.sortKeyKindOf( columnOf( cells, "body" ) )
                 == TableRowCells::SortKeyKind::Text );
    }

    GIVEN( "the keys of the text column" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "level" ) );

        THEN( "ascending is case-insensitive, ties keep Log Line order, and the Log Line the "
              "Log Format does not match comes last" )
        {
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 4, 3, 0, 5, 1, 2 } );
        }

        THEN( "descending reverses the values, ties and the unmatched Log Line staying as "
              "they are" )
        {
            REQUIRE( sorted( keys, Qt::DescendingOrder )
                     == std::vector<uint32_t>{ 1, 0, 5, 3, 4, 2 } );
        }
    }

    GIVEN( "the keys of the body column" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "body" ) );

        THEN( "a Log Line the Log Format does not match has no value there, though its cell "
              "shows the whole Log Line" )
        {
            REQUIRE_FALSE( keys.hasValue( 2 ) );
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 1, 0, 5, 4, 3, 2 } );
        }
    }

    GIVEN( "the keys of an integer column" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "size" ) );

        THEN( "the Rows sort by number, not by text, and the Rows without a number come last in "
              "both directions" )
        {
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 1, 0, 5, 3, 2, 4 } );
            REQUIRE( sorted( keys, Qt::DescendingOrder )
                     == std::vector<uint32_t>{ 3, 0, 5, 1, 2, 4 } );
        }
    }

    GIVEN( "the keys of a float column" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "ratio" ) );

        THEN( "the Rows sort by value, a negative one first" )
        {
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 3, 0, 5, 1, 2, 4 } );
        }
    }

    GIVEN( "the keys of the timestamp column" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "timestamp" ) );

        THEN( "the Rows sort by Timestamp, the Log Line without one last" )
        {
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 0, 3, 1, 4, 5, 2 } );
            REQUIRE( sorted( keys, Qt::DescendingOrder )
                     == std::vector<uint32_t>{ 5, 4, 1, 3, 0, 2 } );
        }
    }

    GIVEN( "the keys of the elapsed column" )
    {
        const auto keys = keysOf( cells, cells.elapsedColumn() );

        THEN( "the Rows sort by duration, a negative one first, the Rows with an empty cell "
              "last" )
        {
            // 0: none; 1: +2 s; 2: none; 3: -1 s; 4: +2 s; 5: +0.5 s
            REQUIRE( sorted( keys, Qt::AscendingOrder )
                     == std::vector<uint32_t>{ 3, 5, 1, 4, 0, 2 } );
            REQUIRE( sorted( keys, Qt::DescendingOrder )
                     == std::vector<uint32_t>{ 1, 4, 5, 3, 0, 2 } );
        }
    }
}

SCENARIO( "Appended Log Lines are merged into a Sort Order at their place",
          "[tablesortkeys][sortorder]" )
{
    const auto format = makeFormat();
    const TableRowCells cells( format );
    const std::atomic<bool> cancel{ false };

    GIVEN( "the first four Log Lines sorted by level, descending" )
    {
        const auto column = columnOf( cells, "level" );
        TableSortKeys keys( cells.sortKeyKindOf( column ) );
        for ( int line = 0; line < 4; ++line ) {
            keys.append(
                cells.sortKeyOf( column, LineNumber( static_cast<uint64_t>( line ) ), Lines[ line ],
                                 []( LineNumber ) { return std::optional<QDateTime>{}; } ) );
        }
        const auto before = sortedLogLines( keys, Qt::DescendingOrder, cancel );
        REQUIRE( before == std::vector<uint32_t>{ 1, 0, 3, 2 } );

        WHEN( "the two Log Lines after them are keyed and merged in" )
        {
            for ( int line = 4; line < Lines.size(); ++line ) {
                keys.append( cells.sortKeyOf(
                    column, LineNumber( static_cast<uint64_t>( line ) ), Lines[ line ],
                    []( LineNumber ) { return std::optional<QDateTime>{}; } ) );
            }
            const auto merged
                = mergeSortedLogLines( keys, Qt::DescendingOrder, before, { 4, 5 }, cancel );

            THEN( "the order is the one a sort of every Log Line gives, and the Rows inserted "
                  "are told" )
            {
                REQUIRE( merged.logLines == sortedLogLines( keys, Qt::DescendingOrder, cancel ) );
                REQUIRE( merged.logLines == std::vector<uint32_t>{ 1, 0, 5, 3, 4, 2 } );
                REQUIRE( merged.inserted == std::vector<RowRun>{ { 2, 1 }, { 4, 1 } } );
            }
        }
    }

    GIVEN( "a sort that is cancelled" )
    {
        const auto keys = keysOf( cells, columnOf( cells, "level" ) );
        const std::atomic<bool> cancelled{ true };

        THEN( "it gives nothing" )
        {
            REQUIRE( sortedLogLines( keys, Qt::AscendingOrder, cancelled ).empty() );
            REQUIRE( mergeSortedLogLines( keys, Qt::AscendingOrder, { 0, 1 }, { 2, 3 }, cancelled )
                         .logLines.empty() );
        }
    }
}

SCENARIO( "A sorted Row Mapping finds a Log Line's Row and a Row's Log Line",
          "[rowmapping][sortorder]" )
{
    GIVEN( "Rows showing Log Lines 4, 1, 3 in that order" )
    {
        const SortedRows rows( { 4, 1, 3 } );

        THEN( "there are three Rows, whatever the Log File's length" )
        {
            REQUIRE( rows.rowCount( LinesCount( 5 ) ) == 3 );
            REQUIRE( rows.rowCount( LinesCount( 100 ) ) == 3 );
        }

        THEN( "each Row shows its Log Line, and a Row beyond the last shows the last's" )
        {
            REQUIRE( rows.logLineAt( 0 ) == 4_lnum );
            REQUIRE( rows.logLineAt( 1 ) == 1_lnum );
            REQUIRE( rows.logLineAt( 2 ) == 3_lnum );
            REQUIRE( rows.logLineAt( 7 ) == 3_lnum );
        }

        THEN( "each Log Line finds its Row, and a Log Line without one finds none" )
        {
            REQUIRE( rows.rowOf( 4_lnum ) == 0 );
            REQUIRE( rows.rowOf( 1_lnum ) == 1 );
            REQUIRE( rows.rowOf( 3_lnum ) == 2 );
            REQUIRE_FALSE( rows.rowOf( 0_lnum ).has_value() );
            REQUIRE_FALSE( rows.rowOf( 2_lnum ).has_value() );
            REQUIRE_FALSE( rows.rowOf( 99_lnum ).has_value() );
        }
    }

    GIVEN( "a subset of the keyed Log Lines to order" )
    {
        const auto format = makeFormat();
        const TableRowCells cells( format );
        const auto keys = keysOf( cells, columnOf( cells, "level" ) );
        const std::atomic<bool> cancel{ false };

        THEN( "only they are Rows, in the order of their keys" )
        {
            const SortedRows rows(
                sortedLogLines( keys, Qt::AscendingOrder, { 0, 1, 2, 4 }, cancel ) );
            REQUIRE( rows.logLines() == std::vector<uint32_t>{ 4, 0, 1, 2 } );
            REQUIRE_FALSE( rows.rowOf( 3_lnum ).has_value() );
            REQUIRE_FALSE( rows.rowOf( 5_lnum ).has_value() );
        }
    }

    GIVEN( "no Rows" )
    {
        const SortedRows rows( {} );

        THEN( "there is no Row and no Log Line" )
        {
            REQUIRE( rows.rowCount( LinesCount( 5 ) ) == 0 );
            REQUIRE_FALSE( rows.rowOf( 0_lnum ).has_value() );
            REQUIRE( rows.logLineAt( 0 ) == 0_lnum );
        }
    }
}
