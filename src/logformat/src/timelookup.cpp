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

#include "timelookup.h"

#include "abstractlogdata.h"
#include "timestampreader.h"

#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <vector>

namespace timelookup {

namespace {

struct Found {
    uint64_t line;
    QDateTime timestamp;
};

// The first Log Line with a Timestamp in [start, end), looking at no more
// than MaxLinesWithoutTimestamp of them.
std::optional<Found> firstTimestampFrom( uint64_t start, uint64_t end,
                                         const TimestampAt& timestampAt )
{
    const auto stop = std::min( end, start + MaxLinesWithoutTimestamp );
    for ( auto line = start; line < stop; ++line ) {
        if ( auto timestamp = timestampAt( LineNumber( line ) ) ) {
            return Found{ line, std::move( *timestamp ) };
        }
    }
    return std::nullopt;
}

// Reads no more once cancelled: a Log Line is then taken as one without a
// Timestamp, which ends a search quickly; the caller drops what it found.
TimestampAt cancellable( const TimestampAt& timestampAt, const std::atomic<bool>* cancelled )
{
    if ( !cancelled ) {
        return timestampAt;
    }
    return [ &timestampAt, cancelled ]( LineNumber line ) -> std::optional<QDateTime> {
        if ( cancelled->load() ) {
            return std::nullopt;
        }
        return timestampAt( line );
    };
}

bool isCancelled( const std::atomic<bool>* cancelled )
{
    return cancelled && cancelled->load();
}

// Whether the Timestamps in a window around a line are out of time order.
bool isOutOfOrderAround( uint64_t line, uint64_t count, const TimestampAt& timestampAt )
{
    std::vector<QDateTime> before;
    const auto lowest = line > OrderScanLines ? line - OrderScanLines : 0;
    for ( auto probe = line; probe > lowest && before.size() < OrderWindow; ) {
        --probe;
        if ( auto timestamp = timestampAt( LineNumber( probe ) ) ) {
            before.push_back( std::move( *timestamp ) );
        }
    }
    std::reverse( before.begin(), before.end() );

    auto previous = std::optional<QDateTime>();
    if ( !before.empty() ) {
        for ( const auto& timestamp : before ) {
            if ( previous && *previous > timestamp ) {
                return true;
            }
            previous = timestamp;
        }
    }

    size_t taken = 0;
    const auto stop = std::min( count, line + OrderScanLines );
    for ( auto probe = line; probe < stop && taken < OrderWindow; ++probe ) {
        if ( auto timestamp = timestampAt( LineNumber( probe ) ) ) {
            if ( previous && *previous > *timestamp ) {
                return true;
            }
            previous = std::move( timestamp );
            ++taken;
        }
    }
    return false;
}

std::optional<Result> binarySearch( const QDateTime& time, LinesCount lineCount,
                                    const TimestampAt& timestampAt, uint64_t from )
{
    const auto count = lineCount.get();
    if ( count == 0 ) {
        return std::nullopt;
    }

    // The boundary: the first index from which every Log Line that has a
    // Timestamp has one at or after the time.
    uint64_t low = std::min( from, count - 1 );
    uint64_t high = count;
    while ( low < high ) {
        const auto middle = low + ( high - low ) / 2;
        const auto probe = firstTimestampFrom( middle, high, timestampAt );
        if ( probe && probe->timestamp < time ) {
            low = probe->line + 1;
        }
        else {
            // At or after the time, or nothing but Log Lines without a
            // Timestamp up to high: the boundary is not after the middle.
            high = middle;
        }
    }

    const auto found = firstTimestampFrom( low, count, timestampAt );
    if ( !found ) {
        // Only a Timestamp before the time moves the boundary off the start.
        if ( low == 0 ) {
            return Result{ LineNumber( 0 ), Position::NoTimestamps };
        }
        return Result{ LineNumber( count - 1 ), Position::AfterLast };
    }
    if ( low == 0 && found->timestamp > time ) {
        return Result{ LineNumber( 0 ), Position::BeforeFirst };
    }
    return Result{ LineNumber( found->line ), Position::AtOrAfter };
}

} // namespace

std::optional<Result> firstLineAtOrAfter( const QDateTime& time, LinesCount lineCount,
                                          const TimestampAt& unguardedTimestampAt,
                                          const Options& options )
{
    const auto timestampAt = cancellable( unguardedTimestampAt, options.cancelled );
    auto result = binarySearch( time, lineCount, timestampAt, options.from.get() );
    if ( isCancelled( options.cancelled ) ) {
        return std::nullopt;
    }
    if ( result ) {
        result->outOfOrder = isOutOfOrderAround( result->line.get(), lineCount.get(), timestampAt );
    }
    if ( isCancelled( options.cancelled ) ) {
        return std::nullopt;
    }
    return result;
}

std::optional<QDateTime> timestampNear( LineNumber line, LinesCount lineCount,
                                        const TimestampAt& unguardedTimestampAt,
                                        const std::atomic<bool>* cancelled )
{
    const auto timestampAt = cancellable( unguardedTimestampAt, cancelled );
    const auto count = lineCount.get();
    if ( count == 0 ) {
        return std::nullopt;
    }
    const auto center = std::min( line.get(), count - 1 );
    if ( auto timestamp = timestampAt( LineNumber( center ) ) ) {
        return timestamp;
    }
    for ( uint64_t distance = 1; distance <= MaxLinesWithoutTimestamp; ++distance ) {
        if ( center + distance < count ) {
            if ( auto timestamp = timestampAt( LineNumber( center + distance ) ) ) {
                return timestamp;
            }
        }
        if ( distance <= center ) {
            if ( auto timestamp = timestampAt( LineNumber( center - distance ) ) ) {
                return timestamp;
            }
        }
        if ( center + distance >= count && distance > center ) {
            break;
        }
    }
    return std::nullopt;
}

std::optional<Result> firstLineAtOrAfter( const QDateTime& time, const AbstractLogData& logData,
                                          const TimestampReader& reader, const Options& options )
{
    return firstLineAtOrAfter(
        time, logData.getNbLine(),
        [ & ]( LineNumber line ) { return reader.timestampOf( logData.getLineString( line ) ); },
        options );
}

std::optional<QDateTime> timestampNear( LineNumber line, const AbstractLogData& logData,
                                        const TimestampReader& reader,
                                        const std::atomic<bool>* cancelled )
{
    return timestampNear(
        line, logData.getNbLine(),
        [ & ]( LineNumber other ) { return reader.timestampOf( logData.getLineString( other ) ); },
        cancelled );
}

LimitsResult searchLimitsForTimeRange( const QDateTime& startTime, const QDateTime& endTime,
                                       LinesCount lineCount, const TimestampAt& timestampAt,
                                       const std::atomic<bool>* cancelled )
{
    using Outcome = LimitsResult::Outcome;
    const auto outcome = []( Outcome value ) {
        LimitsResult result;
        result.outcome = value;
        return result;
    };

    if ( endTime <= startTime ) {
        return outcome( Outcome::EndNotAfterStart );
    }
    const auto start = firstLineAtOrAfter( startTime, lineCount, timestampAt, { {}, cancelled } );
    if ( isCancelled( cancelled ) ) {
        return outcome( Outcome::Cancelled );
    }
    if ( !start || start->position == Position::NoTimestamps ) {
        return outcome( Outcome::NoTimestamps );
    }
    if ( start->position == Position::AfterLast ) {
        return outcome( Outcome::AfterFile );
    }
    // The end is not before the start: the search for it begins where the
    // start was found.
    const auto end
        = firstLineAtOrAfter( endTime, lineCount, timestampAt, { start->line, cancelled } );
    if ( isCancelled( cancelled ) ) {
        return outcome( Outcome::Cancelled );
    }
    if ( !end ) {
        return outcome( Outcome::NoTimestamps );
    }
    if ( end->position == Position::BeforeFirst ) {
        return outcome( Outcome::BeforeFile );
    }

    const auto endLine
        = end->position == Position::AfterLast ? LineNumber( lineCount.get() ) : end->line;
    if ( endLine <= start->line ) {
        return outcome( start->position == Position::BeforeFirst ? Outcome::BeforeFile
                                                                 : Outcome::NoLogLines );
    }
    LimitsResult result;
    result.outcome = Outcome::Limits;
    result.start = start->line;
    result.end = endLine;
    result.outOfOrder = start->outOfOrder || end->outOfOrder;
    return result;
}

LimitsResult searchLimitsForTimeRange( const QDateTime& startTime, const QDateTime& endTime,
                                       const AbstractLogData& logData,
                                       const TimestampReader& reader,
                                       const std::atomic<bool>* cancelled )
{
    return searchLimitsForTimeRange(
        startTime, endTime, logData.getNbLine(),
        [ & ]( LineNumber line ) { return reader.timestampOf( logData.getLineString( line ) ); },
        cancelled );
}

std::optional<QDateTime> parseTimeInput( const QString& text, const QDate& defaultDate )
{
    static const QRegularExpression pattern(
        QStringLiteral( R"(^\s*(?:(\d{4})[-/](\d{1,2})[-/](\d{1,2})[T\s]+)?)"
                        R"((\d{1,2}):(\d{2})(?::(\d{2})(?:[.,](\d+))?)?\s*$)" ) );

    const auto match = pattern.match( text );
    if ( !match.hasMatch() ) {
        return std::nullopt;
    }

    QDate date = defaultDate;
    if ( !match.captured( 1 ).isEmpty() ) {
        date = QDate( match.captured( 1 ).toInt(), match.captured( 2 ).toInt(),
                      match.captured( 3 ).toInt() );
    }

    auto fraction = match.captured( 7 ).left( 3 );
    while ( fraction.size() < 3 ) {
        fraction += QLatin1Char( '0' );
    }
    const QTime time( match.captured( 4 ).toInt(), match.captured( 5 ).toInt(),
                      match.captured( 6 ).toInt(), fraction.toInt() );
    if ( !date.isValid() || !time.isValid() ) {
        return std::nullopt;
    }
    return QDateTime( date, time, QTimeZone::UTC );
}

} // namespace timelookup
