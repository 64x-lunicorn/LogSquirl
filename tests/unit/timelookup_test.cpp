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

#include <catch2/catch_test_macros.hpp>

#include "timelookup.h"

#include <QDateTime>
#include <QTimeZone>

#include <vector>

using namespace timelookup;

namespace {

QDateTime at( int hour, int minute, int second = 0 )
{
    return QDateTime( QDate( 2026, 9, 23 ), QTime( hour, minute, second ), QTimeZone::UTC );
}

// A Log File given as one entry per Log Line: its Timestamp, none for a
// continuation line. Counts how often a Log Line is read.
class FakeLogFile {
public:
    explicit FakeLogFile( std::vector<std::optional<QDateTime>> lines )
        : lines_( std::move( lines ) )
    {
    }

    LinesCount count() const
    {
        return LinesCount( lines_.size() );
    }

    TimestampAt reader()
    {
        return [ this ]( LineNumber line ) {
            ++reads;
            return lines_.at( line.get() );
        };
    }

    std::optional<Result> lookup( const QDateTime& time )
    {
        return firstLineAtOrAfter( time, count(), reader() );
    }

    int reads = 0;

private:
    std::vector<std::optional<QDateTime>> lines_;
};

FakeLogFile withStackTraces()
{
    // 0: 10:00  1-3: stack trace  4: 10:05  5: 10:05  6: 10:10  7-8: stack trace  9: 10:20
    return FakeLogFile( { at( 10, 0 ), std::nullopt, std::nullopt, std::nullopt, at( 10, 5 ),
                          at( 10, 5 ), at( 10, 10 ), std::nullopt, std::nullopt, at( 10, 20 ) } );
}

} // namespace

TEST_CASE( "The lookup finds the first Log Line at or after a time", "[logformat][timelookup]" )
{
    auto file = withStackTraces();

    SECTION( "a time between two Timestamps" )
    {
        const auto result = file.lookup( at( 10, 7 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 6 ) );
        CHECK( result->position == Position::AtOrAfter );
    }

    SECTION( "a time equal to a Timestamp that several Log Lines carry" )
    {
        const auto result = file.lookup( at( 10, 5 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 4 ) );
        CHECK( result->position == Position::AtOrAfter );
    }

    SECTION( "a time in the middle of a stack trace lands after it" )
    {
        const auto result = file.lookup( at( 10, 12 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 9 ) );
        CHECK( result->position == Position::AtOrAfter );
    }

    SECTION( "the first and the last Timestamp" )
    {
        CHECK( file.lookup( at( 10, 0 ) )->line == LineNumber( 0 ) );
        CHECK( file.lookup( at( 10, 0 ) )->position == Position::AtOrAfter );
        CHECK( file.lookup( at( 10, 20 ) )->line == LineNumber( 9 ) );
        CHECK( file.lookup( at( 10, 20 ) )->position == Position::AtOrAfter );
    }

    SECTION( "a time before the first Timestamp" )
    {
        const auto result = file.lookup( at( 9, 0 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 0 ) );
        CHECK( result->position == Position::BeforeFirst );
    }

    SECTION( "a time after the last Timestamp" )
    {
        const auto result = file.lookup( at( 11, 0 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 9 ) );
        CHECK( result->position == Position::AfterLast );
    }
}

TEST_CASE( "The lookup copes with the edges of a Log File", "[logformat][timelookup]" )
{
    SECTION( "an empty Log File" )
    {
        FakeLogFile file( {} );
        CHECK( !file.lookup( at( 10, 0 ) ).has_value() );
    }

    SECTION( "a Log File without a Timestamp" )
    {
        FakeLogFile file( { std::nullopt, std::nullopt, std::nullopt } );
        const auto result = file.lookup( at( 10, 0 ) );
        REQUIRE( result );
        CHECK( result->line == LineNumber( 0 ) );
        CHECK( result->position == Position::NoTimestamps );
    }

    SECTION( "a Log File that starts with lines without a Timestamp" )
    {
        FakeLogFile file( { std::nullopt, std::nullopt, at( 10, 0 ), at( 10, 5 ) } );
        CHECK( file.lookup( at( 10, 1 ) )->line == LineNumber( 3 ) );
        CHECK( file.lookup( at( 10, 0 ) )->line == LineNumber( 2 ) );
        CHECK( file.lookup( at( 9, 0 ) )->position == Position::BeforeFirst );
    }

    SECTION( "a Log File with a single Log Line" )
    {
        FakeLogFile file( { at( 10, 0 ) } );
        CHECK( file.lookup( at( 10, 0 ) )->position == Position::AtOrAfter );
        CHECK( file.lookup( at( 9, 0 ) )->position == Position::BeforeFirst );
        CHECK( file.lookup( at( 11, 0 ) )->position == Position::AfterLast );
    }
}

TEST_CASE( "The lookup reads a logarithmic number of Log Lines", "[logformat][timelookup]" )
{
    std::vector<std::optional<QDateTime>> lines;
    for ( int i = 0; i < 100'000; ++i ) {
        lines.push_back( at( 0, 0 ).addSecs( i ) );
    }
    FakeLogFile file( std::move( lines ) );

    const auto result = file.lookup( at( 0, 0 ).addSecs( 54'321 ) );
    REQUIRE( result );
    CHECK( result->line == LineNumber( 54'321 ) );
    CHECK( file.reads < 40 );
}

TEST_CASE( "The lookup gives up on a stretch of Log Lines without a Timestamp",
           "[logformat][timelookup]" )
{
    std::vector<std::optional<QDateTime>> lines;
    lines.push_back( at( 10, 0 ) );
    lines.insert( lines.end(), MaxLinesWithoutTimestamp * 3, std::nullopt );
    lines.push_back( at( 11, 0 ) );
    FakeLogFile file( std::move( lines ) );

    // Whatever it decides, it stays inside the Log File and reads little.
    const auto result = file.lookup( at( 10, 30 ) );
    REQUIRE( result );
    CHECK( result->line.get() < file.count().get() );
    CHECK( file.reads < 8 * static_cast<int>( MaxLinesWithoutTimestamp ) );
}

TEST_CASE( "The Timestamp near a Log Line", "[logformat][timelookup]" )
{
    auto file = withStackTraces();
    const auto near = [ &file ]( uint64_t line ) {
        return timestampNear( LineNumber( line ), file.count(), file.reader() );
    };

    CHECK( near( 4 ) == at( 10, 5 ) );
    CHECK( near( 1 ) == at( 10, 0 ) );
    CHECK( near( 3 ) == at( 10, 5 ) );
    CHECK( near( 8 ) == at( 10, 20 ) );
    CHECK( near( 1'000 ) == at( 10, 20 ) );
    CHECK( !timestampNear( 0_lnum, 0_lcount, file.reader() ).has_value() );
    FakeLogFile none( { std::nullopt, std::nullopt } );
    CHECK( !timestampNear( 1_lnum, none.count(), none.reader() ).has_value() );
}

TEST_CASE( "A time typed by a user", "[logformat][timelookup]" )
{
    const QDate day( 2026, 9, 23 );

    SECTION( "a time of day takes the default date" )
    {
        CHECK( parseTimeInput( "14:02", day ) == at( 14, 2 ) );
        CHECK( parseTimeInput( " 14:02:30 ", day ) == at( 14, 2, 30 ) );
        CHECK( parseTimeInput( "9:05", day ) == at( 9, 5 ) );
        CHECK( parseTimeInput( "14:02:30.25", day )
               == QDateTime( day, QTime( 14, 2, 30, 250 ), QTimeZone::UTC ) );
        CHECK( parseTimeInput( "14:02:30,5", day )
               == QDateTime( day, QTime( 14, 2, 30, 500 ), QTimeZone::UTC ) );
    }

    SECTION( "a date, with a space, a T or slashes" )
    {
        const auto expected = QDateTime( QDate( 2025, 1, 2 ), QTime( 14, 2, 30 ), QTimeZone::UTC );
        CHECK( parseTimeInput( "2025-01-02 14:02:30", day ) == expected );
        CHECK( parseTimeInput( "2025-01-02T14:02:30", day ) == expected );
        CHECK( parseTimeInput( "2025/01/02 14:02:30", day ) == expected );
    }

    SECTION( "what is not a time" )
    {
        CHECK( !parseTimeInput( "", day ).has_value() );
        CHECK( !parseTimeInput( "noon", day ).has_value() );
        CHECK( !parseTimeInput( "25:00", day ).has_value() );
        CHECK( !parseTimeInput( "14:61", day ).has_value() );
        CHECK( !parseTimeInput( "2025-13-02 14:02", day ).has_value() );
        CHECK( !parseTimeInput( "14", day ).has_value() );
    }
}
