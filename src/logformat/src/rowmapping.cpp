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

#include "rowmapping.h"

#include "logfiltereddata.h"

RowMapping::LineType RowMapping::lineType( const LogFilteredData* currentSearch,
                                           LineNumber logLine ) const
{
    if ( currentSearch != nullptr ) {
        return currentSearch->lineTypeByLine( logLine );
    }
    return AbstractLogData::LineTypeFlags::Plain;
}

SortedRows::SortedRows( std::vector<uint32_t> logLines )
    : logLines_( std::move( logLines ) )
{
    uint32_t largest = 0;
    for ( const auto logLine : logLines_ ) {
        largest = std::max( largest, logLine );
    }
    rowOfLogLine_.assign( logLines_.empty() ? 0 : static_cast<size_t>( largest ) + 1, -1 );
    for ( size_t row = 0; row < logLines_.size(); ++row ) {
        rowOfLogLine_[ logLines_[ row ] ] = static_cast<int32_t>( row );
    }
}

LineNumber SortedRows::logLineAt( int row ) const
{
    if ( logLines_.empty() ) {
        return LineNumber( 0 );
    }
    const auto index
        = static_cast<size_t>( std::clamp( row, 0, static_cast<int>( logLines_.size() ) - 1 ) );
    return LineNumber( logLines_[ index ] );
}

std::optional<int> SortedRows::rowOf( LineNumber logLine ) const
{
    if ( logLine.get() >= rowOfLogLine_.size() ) {
        return std::nullopt;
    }
    const auto row = rowOfLogLine_[ static_cast<size_t>( logLine.get() ) ];
    if ( row < 0 ) {
        return std::nullopt;
    }
    return static_cast<int>( row );
}
