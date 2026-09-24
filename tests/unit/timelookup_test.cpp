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

#include "logformatcatalog.h"
#include "timelookup.h"
#include "timestampreader.h"

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

namespace {

using Outcome = LimitsResult::Outcome;

LimitsResult limits( FakeLogFile& file, const QDateTime& start, const QDateTime& end )
{
    return searchLimitsForTimeRange( start, end, file.count(), file.reader() );
}

} // namespace

TEST_CASE( "A time range becomes the Search Limits of the first Log Lines at or after its ends",
           "[logformat][timelookup]" )
{
    auto file = withStackTraces();

    SECTION( "both ends on Timestamps: half-open, the end line is not in" )
    {
        const auto result = limits( file, at( 10, 5 ), at( 10, 10 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 4 ) );
        CHECK( result.end == LineNumber( 6 ) );
    }

    SECTION( "the same lines as the ones found for the ends by hand" )
    {
        const auto result = limits( file, at( 10, 1 ), at( 10, 12 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == file.lookup( at( 10, 1 ) )->line );
        CHECK( result.end == file.lookup( at( 10, 12 ) )->line );
    }

    SECTION( "continuation lines follow the Log Line before them" )
    {
        // 10:10 holds line 6 and its stack trace, 7 and 8; 10:20 is line 9.
        const auto result = limits( file, at( 10, 10 ), at( 10, 20 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 6 ) );
        CHECK( result.end == LineNumber( 9 ) );
    }

    SECTION( "a start inside a stack trace begins after it" )
    {
        const auto result = limits( file, at( 10, 2 ), at( 10, 6 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 4 ) );
        CHECK( result.end == LineNumber( 6 ) );
    }

    SECTION( "an end after the last Timestamp reaches the end of the Log File" )
    {
        const auto result = limits( file, at( 10, 12 ), at( 11, 0 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 9 ) );
        CHECK( result.end == LineNumber( 10 ) );
    }

    SECTION( "a start before the first Timestamp begins at the first Log Line" )
    {
        const auto result = limits( file, at( 9, 0 ), at( 10, 5 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 0 ) );
        CHECK( result.end == LineNumber( 4 ) );
    }

    SECTION( "a range around the whole Log File" )
    {
        const auto result = limits( file, at( 9, 0 ), at( 11, 0 ) );
        REQUIRE( result.outcome == Outcome::Limits );
        CHECK( result.start == LineNumber( 0 ) );
        CHECK( result.end == LineNumber( 10 ) );
    }
}

TEST_CASE( "A time range that does not reach the Log File gives no Search Limits",
           "[logformat][timelookup]" )
{
    auto file = withStackTraces();

    CHECK( limits( file, at( 9, 0 ), at( 9, 30 ) ).outcome == Outcome::BeforeFile );
    // Half-open: ending at the first Timestamp leaves that Log Line out.
    CHECK( limits( file, at( 9, 0 ), at( 10, 0 ) ).outcome == Outcome::BeforeFile );
    CHECK( limits( file, at( 10, 21 ), at( 11, 0 ) ).outcome == Outcome::AfterFile );
    CHECK( limits( file, at( 11, 0 ), at( 12, 0 ) ).outcome == Outcome::AfterFile );
    CHECK( limits( file, at( 10, 11 ), at( 10, 11 ) ).outcome == Outcome::EndNotAfterStart );
    CHECK( limits( file, at( 10, 12 ), at( 10, 11 ) ).outcome == Outcome::EndNotAfterStart );
    // Inside the Log File, between two Timestamps: no Log Line to limit to.
    CHECK( limits( file, at( 10, 6 ), at( 10, 7 ) ).outcome == Outcome::NoLogLines );

    FakeLogFile empty( {} );
    CHECK( limits( empty, at( 10, 0 ), at( 10, 5 ) ).outcome == Outcome::NoTimestamps );
    FakeLogFile untimed( { std::nullopt, std::nullopt } );
    CHECK( limits( untimed, at( 10, 0 ), at( 10, 5 ) ).outcome == Outcome::NoTimestamps );
}

TEST_CASE( "A lookup says when the Log File is not in time order around the result",
           "[logformat][timelookup]" )
{
    SECTION( "a Log File in order never says so" )
    {
        std::vector<std::optional<QDateTime>> lines;
        for ( int i = 0; i < 100; ++i ) {
            lines.push_back( at( 10, i / 2 ) );
        }
        FakeLogFile file( lines );
        for ( int minute : { 0, 1, 25, 49, 50 } ) {
            const auto result = file.lookup( at( 10, minute ) );
            REQUIRE( result );
            CHECK( !result->outOfOrder );
        }
        // Log Lines without a Timestamp between them change nothing.
        auto traced = withStackTraces();
        CHECK( !traced.lookup( at( 10, 7 ) )->outOfOrder );
    }

    SECTION( "timestamps that go back near the result" )
    {
        // 10:00 .. 10:04, then a jump back to 09:00, then in order again.
        std::vector<std::optional<QDateTime>> lines;
        for ( int i = 0; i < 20; ++i ) {
            lines.push_back( i < 10 ? at( 10, i ) : at( 9, i ) );
        }
        FakeLogFile file( lines );
        const auto result = file.lookup( at( 9, 14 ) );
        REQUIRE( result );
        CHECK( result->outOfOrder );
    }

    SECTION( "a disorder far from the result is not looked for" )
    {
        std::vector<std::optional<QDateTime>> lines;
        for ( int i = 0; i < 2000; ++i ) {
            lines.push_back( at( 10, 0, 0 ).addSecs( i ) );
        }
        std::swap( lines[ 1900 ], lines[ 1901 ] );
        FakeLogFile file( lines );
        CHECK( !file.lookup( at( 10, 0, 0 ).addSecs( 5 ) )->outOfOrder );
    }

    SECTION( "Search Limits carry it" )
    {
        std::vector<std::optional<QDateTime>> lines;
        for ( int i = 0; i < 20; ++i ) {
            lines.push_back( i < 10 ? at( 10, i ) : at( 9, i ) );
        }
        FakeLogFile file( lines );
        const auto result = searchLimitsForTimeRange( at( 9, 12 ), at( 9, 16 ), file.count(),
                                                      file.reader() );
        REQUIRE( result.outcome == LimitsResult::Outcome::Limits );
        CHECK( result.outOfOrder );
    }
}

TEST_CASE( "A Log File across New Year is found in January", "[logformat][timelookup]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto& syslog = *catalog.allFormats().value( QStringLiteral( "syslog_log" ) );
    // Written on 2027-01-05: December is 2026, January 2027.
    const TimestampReader reader( syslog, 0, QDate( 2027, 1, 5 ) );

    const QStringList text = {
        QStringLiteral( "Dec 30 10:00:00 host prog: a" ),
        QStringLiteral( "Dec 31 23:59:58 host prog: b" ),
        QStringLiteral( "Jan  1 00:00:01 host prog: c" ),
        QStringLiteral( "Jan  2 08:00:00 host prog: d" ),
        QStringLiteral( "Jan  3 09:00:00 host prog: e" ),
    };
    const TimestampAt read = [ & ]( LineNumber line ) {
        return reader.timestampOf( text.at( static_cast<qsizetype>( line.get() ) ) );
    };
    const auto january = QDateTime( QDate( 2027, 1, 2 ), QTime( 8, 0, 0 ), QTimeZone::UTC );

    const auto result = firstLineAtOrAfter( january, LinesCount( 5 ), read );
    REQUIRE( result );
    CHECK( result->position == Position::AtOrAfter );
    CHECK( result->line == LineNumber( 3 ) );
    CHECK( !result->outOfOrder );
}

TEST_CASE( "A Log File mixing offsets is compared by instant", "[logformat][timelookup]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto& spdlog = *catalog.allFormats().value( QStringLiteral( "spdlog_log" ) );
    const TimestampReader reader( spdlog, 2026 );
    const auto parse = [ & ]( const char* text ) {
        return reader.parseField( QString::fromLatin1( text ) );
    };
    // The same instants, written in two zones, in the order they happened.
    const std::vector<std::optional<QDateTime>> lines = {
        parse( "2026-09-24T07:00:00Z" ),      parse( "2026-09-24T09:30:00+02:00" ),
        parse( "2026-09-24T08:00:00Z" ),      parse( "2026-09-24T10:30:00+02:00" ),
        parse( "2026-09-24T09:30:00Z" ),
    };
    const TimestampAt read = [ & ]( LineNumber line ) { return lines.at( line.get() ); };
    const auto result = firstLineAtOrAfter( parse( "2026-09-24T10:00:00+02:00" ).value(),
                                            LinesCount( 5 ), read );
    REQUIRE( result );
    CHECK( result->line == LineNumber( 2 ) );
    CHECK( !result->outOfOrder );
}
