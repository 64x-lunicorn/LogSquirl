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

#include "sparselineread.h"

#include <algorithm>

namespace {

// Log Lines at most this far apart are looked up in the Index at once: less
// than an Index block, so the lookup decodes no block it would not have to.
constexpr std::uint64_t MaxLinesBetweenInLookup = 64;
// The most Log Lines one lookup spans, so the offsets it returns stay few
// however dense the Log Lines asked for are.
constexpr std::uint64_t MaxLinesPerLookup = 64 * 1024;

// Where one Log Line asked for lies in the Log File.
struct LineBytes {
    std::size_t request;
    std::uint64_t line;
    qint64 begin;
    qint64 end;
};

} // namespace

logsquirl::vector<SparseRead> planSparseRead( std::span<const LineNumber> lines, LinesCount nbLines,
                                              const EndOfLineOffsets& endOfLineOffsets,
                                              const SparseReadLimits& limits )
{
    // The requests in ascending order of Log Line, leaving out those past
    // the last Log Line.
    logsquirl::vector<std::size_t> order;
    order.reserve( lines.size() );
    for ( std::size_t request = 0; request < lines.size(); ++request ) {
        if ( lines[ request ] < LineNumber( nbLines.get() ) ) {
            order.push_back( request );
        }
    }
    if ( !std::is_sorted( lines.begin(), lines.end() ) ) {
        std::stable_sort( order.begin(), order.end(), [ &lines ]( std::size_t a, std::size_t b ) {
            return lines[ a ] < lines[ b ];
        } );
    }

    // Where each Log Line lies, looking up Log Lines close to each other at
    // once. A Log Line the Index has no offset for ends the list: every one
    // after it lies further on.
    logsquirl::vector<LineBytes> bytes;
    bytes.reserve( order.size() );
    auto lookupBegin = order.begin();
    while ( lookupBegin != order.end() ) {
        const auto firstLine = lines[ *lookupBegin ].get();
        auto lookupEnd = lookupBegin + 1;
        while ( lookupEnd != order.end() ) {
            const auto line = lines[ *lookupEnd ].get();
            const auto previous = lines[ *( lookupEnd - 1 ) ].get();
            if ( line > previous + MaxLinesBetweenInLookup
                 || line - firstLine >= MaxLinesPerLookup ) {
                break;
            }
            ++lookupEnd;
        }
        const auto lastLine = lines[ *( lookupEnd - 1 ) ].get();

        // The offset before a Log Line is where it starts.
        const auto lookupFirst = firstLine == 0 ? 0 : firstLine - 1;
        const auto offsets = endOfLineOffsets( LineNumber( lookupFirst ),
                                               LinesCount( lastLine - lookupFirst + 1 ) );

        for ( auto request = lookupBegin; request != lookupEnd; ++request ) {
            const auto line = lines[ *request ].get();
            if ( line - lookupFirst >= offsets.size() ) {
                lookupEnd = order.end();
                break;
            }
            const qint64 begin
                = line == 0 ? 0
                            : offsets[ static_cast<std::size_t>( line - 1 - lookupFirst ) ].get();
            const qint64 end = offsets[ static_cast<std::size_t>( line - lookupFirst ) ].get();
            bytes.push_back( { *request, line, begin, end } );
        }

        lookupBegin = lookupEnd;
    }

    // Nearby Log Lines merged into reads.
    logsquirl::vector<SparseRead> reads;
    SparseRead* read = nullptr;
    std::uint64_t readLastLine = 0;
    for ( const auto& line : bytes ) {
        const bool joins = read != nullptr
                           && ( line.line <= readLastLine + 1
                                || ( line.begin - ( read->firstByte.get() + read->size )
                                         <= limits.maxBytesBetween
                                     && read->size < limits.maxReadBytes ) );
        if ( !joins ) {
            read = &reads.emplace_back();
            read->firstByte = OffsetInFile( line.begin );
        }

        const auto firstByte = read->firstByte.get();
        read->lines.push_back( { line.request, line.begin - firstByte, line.end - firstByte } );
        read->size = std::max( read->size, line.end - firstByte );
        readLastLine = line.line;
    }

    return reads;
}
