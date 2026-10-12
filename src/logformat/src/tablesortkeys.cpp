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

#include "tablesortkeys.h"

#include <algorithm>
#include <numeric>

TableSortKeys::TableSortKeys( Kind kind )
    : kind_( kind )
{
}

void TableSortKeys::reserve( size_t lines )
{
    hasValue_.reserve( lines );
    switch ( kind_ ) {
    case Kind::Text:
        text_.reserve( lines );
        break;
    case Kind::Number:
        numbers_.reserve( lines );
        break;
    case Kind::Timestamp:
    case Kind::Elapsed:
        milliseconds_.reserve( lines );
        break;
    }
}

void TableSortKeys::append( const Key& key )
{
    hasValue_.push_back( key.hasValue ? 1 : 0 );
    switch ( kind_ ) {
    case Kind::Text:
        text_.push_back( key.hasValue ? key.text : QString{} );
        break;
    case Kind::Number:
        numbers_.push_back( key.hasValue ? key.number : 0.0 );
        break;
    case Kind::Timestamp:
    case Kind::Elapsed:
        milliseconds_.push_back( key.hasValue ? key.milliseconds : 0 );
        break;
    }
}

int TableSortKeys::compareValues( uint32_t a, uint32_t b ) const
{
    switch ( kind_ ) {
    case Kind::Text:
        return QString::compare( text_[ a ], text_[ b ], Qt::CaseSensitive );
    case Kind::Number:
        return numbers_[ a ] < numbers_[ b ] ? -1 : ( numbers_[ a ] > numbers_[ b ] ? 1 : 0 );
    case Kind::Timestamp:
    case Kind::Elapsed:
        return milliseconds_[ a ] < milliseconds_[ b ]
                   ? -1
                   : ( milliseconds_[ a ] > milliseconds_[ b ] ? 1 : 0 );
    }
    return 0;
}

bool TableSortKeys::before( uint32_t a, uint32_t b, Qt::SortOrder order ) const
{
    const bool aHas = hasValue( a );
    const bool bHas = hasValue( b );
    if ( !aHas || !bHas ) {
        // A Log Line with a value comes before one without; two without stay
        // in Log Line order.
        return aHas ? !bHas : ( !bHas && a < b );
    }
    const int compared = compareValues( a, b );
    if ( compared == 0 ) {
        return a < b;
    }
    return order == Qt::AscendingOrder ? compared < 0 : compared > 0;
}

namespace {

// Thrown by the comparison once cancel is set: a sort under way is left
// where it is, which is fine for a result that is thrown away.
struct SortCancelled {};

class Comparison {
public:
    Comparison( const TableSortKeys& keys, Qt::SortOrder order, const std::atomic<bool>& cancel )
        : keys_( keys )
        , order_( order )
        , cancel_( cancel )
    {
    }

    bool operator()( uint32_t a, uint32_t b )
    {
        // The flag is read every few thousand comparisons, not at each.
        if ( ( ++comparisons_ & 0xFFF ) == 0 && cancel_.load() ) {
            throw SortCancelled{};
        }
        return keys_.before( a, b, order_ );
    }

private:
    const TableSortKeys& keys_;
    Qt::SortOrder order_;
    const std::atomic<bool>& cancel_;
    uint32_t comparisons_ = 0;
};

} // namespace

std::vector<uint32_t> sortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                      std::vector<uint32_t> logLines,
                                      const std::atomic<bool>& cancel )
{
    try {
        std::sort( logLines.begin(), logLines.end(), Comparison( keys, order, cancel ) );
    } catch ( const SortCancelled& ) {
        return {};
    }
    if ( cancel.load() ) {
        return {};
    }
    return logLines;
}

std::vector<uint32_t> sortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                      const std::atomic<bool>& cancel )
{
    std::vector<uint32_t> lines( keys.size() );
    std::iota( lines.begin(), lines.end(), 0U );
    return sortedLogLines( keys, order, std::move( lines ), cancel );
}

MergedLogLines mergeSortedLogLines( const TableSortKeys& keys, Qt::SortOrder order,
                                    const std::vector<uint32_t>& sorted,
                                    std::vector<uint32_t> appended,
                                    const std::atomic<bool>& cancel )
{
    MergedLogLines merged;
    try {
        std::sort( appended.begin(), appended.end(), Comparison( keys, order, cancel ) );
    } catch ( const SortCancelled& ) {
        return merged;
    }
    if ( cancel.load() ) {
        return merged;
    }

    merged.logLines.reserve( sorted.size() + appended.size() );
    auto old = sorted.begin();
    auto added = appended.begin();
    while ( old != sorted.end() || added != appended.end() ) {
        const bool takeAdded = old == sorted.end()
                               || ( added != appended.end() && keys.before( *added, *old, order ) );
        if ( takeAdded ) {
            const auto row = static_cast<int>( merged.logLines.size() );
            if ( !merged.inserted.empty()
                 && merged.inserted.back().first + merged.inserted.back().count == row ) {
                ++merged.inserted.back().count;
            }
            else {
                merged.inserted.push_back( RowRun{ row, 1 } );
            }
            merged.logLines.push_back( *added++ );
        }
        else {
            merged.logLines.push_back( *old++ );
        }
    }
    return merged;
}
