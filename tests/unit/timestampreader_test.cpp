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
#include "logformatparser.h"
#include "timestampreader.h"

#include <QDateTime>
#include <QTimeZone>

namespace {

QDateTime asWritten( int year, int month, int day, int hour, int minute, int second, int ms = 0 )
{
    return QDateTime( QDate( year, month, day ), QTime( hour, minute, second, ms ),
                      QTimeZone::UTC );
}

LogFormatDefinition formatOf( const char* json )
{
    auto formats = LogFormatParser::parseJsonString( json );
    REQUIRE( formats.size() == 1 );
    return formats[ 0 ];
}

const char* const EpochMillisecondsJson = R"({
    "epoch_log": {
        "title": "Epoch milliseconds",
        "regex": { "std": { "pattern": "^(?<timestamp>\\d+) (?<body>.*)$" } },
        "timestamp-field": "timestamp",
        "timestamp-format": [ "%s" ],
        "timestamp-divisor": 1000,
        "sample": [ { "line": "1700000000123 hello" } ]
    }
})";

const char* const TwoAlternativesJson = R"({
    "two_log": {
        "title": "Two alternatives",
        "regex": { "std": { "pattern": "^(?<ts>[^ ]+(?: \\d{2}:\\d{2}:\\d{2})?) (?<body>.*)$" } },
        "timestamp-field": "ts",
        "timestamp-format": [ "%Y-%m-%d %H:%M:%S", "%d.%m.%Y" ],
        "sample": [ { "line": "23.09.2026 hello" } ]
    }
})";

const char* const NoTimestampJson = R"({
    "no_ts_log": {
        "title": "No timestamp",
        "regex": { "std": { "pattern": "^(?<level>\\w+) (?<body>.*)$" } },
        "sample": [ { "line": "INFO hello" } ]
    }
})";

} // namespace

TEST_CASE( "The Timestamp reader reads a sample line of every built-in Log Format",
           "[logformat][timestamp]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    REQUIRE( !catalog.allFormats().isEmpty() );

    for ( const auto& format : catalog.allFormats() ) {
        const TimestampReader reader( *format, 2026 );
        INFO( "Log Format " << format->name().toStdString() );
        REQUIRE( reader.isAvailable() );
        REQUIRE( !format->sampleLines().isEmpty() );

        for ( const auto& sample : format->sampleLines() ) {
            INFO( "sample " << sample.line.toStdString() );
            REQUIRE( reader.timestampOf( sample.line ).has_value() );
        }
    }
}

TEST_CASE( "The Timestamp reader reads the point in time as written", "[logformat][timestamp]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto readerOf = [ &catalog ]( const char* name ) {
        return TimestampReader( *catalog.allFormats().value( QString::fromLatin1( name ) ), 2026 );
    };

    SECTION( "ISO 8601 with a fraction, a comma and a zone" )
    {
        const auto sampleOf = [ &catalog ]( const char* name ) {
            return catalog.allFormats()
                .value( QString::fromLatin1( name ) )
                ->sampleLines()[ 0 ]
                .line;
        };
        CHECK( readerOf( "spdlog_log" ).timestampOf( sampleOf( "spdlog_log" ) )
               == asWritten( 2025, 4, 24, 19, 51, 59, 688 ) );
        CHECK( readerOf( "zookeeper_log" ).timestampOf( sampleOf( "zookeeper_log" ) )
               == asWritten( 2024, 4, 23, 9, 24, 31, 484 ) );
        CHECK( readerOf( "mysql_error_log" )
                   .timestampOf( "2020-08-06T14:25:02.835618Z 0 [Note] [MY-1] [InnoDB] x" )
               == asWritten( 2020, 8, 6, 14, 25, 2, 835 ) );
    }

    SECTION( "a written offset names the UTC instant" )
    {
        const auto reader = readerOf( "access_log" );
        const auto line = []( const char* zone ) {
            return QStringLiteral( "10.0.0.1 - - [11/Feb/2013:06:43:36 %1] \"GET / HTTP/1.1\" 200 5 "
                                   "\"-\" \"x\"" )
                .arg( QLatin1String( zone ) );
        };
        CHECK( reader.timestampOf( line( "-0700" ) ) == asWritten( 2013, 2, 11, 13, 43, 36 ) );
        CHECK( reader.timestampOf( line( "+0100" ) ) == asWritten( 2013, 2, 11, 5, 43, 36 ) );
        CHECK( reader.timestampOf( line( "+0530" ) ) == asWritten( 2013, 2, 11, 1, 13, 36 ) );
        // A name that carries no offset is read as written.
        CHECK( reader.timestampOf( line( "PDT" ) ) == asWritten( 2013, 2, 11, 6, 43, 36 ) );
    }

    SECTION( "an offset and Z name the same instant" )
    {
        const auto reader = readerOf( "spdlog_log" );
        CHECK( reader.parseField( u"2026-09-24T10:00:00+02:00" )
               == reader.parseField( u"2026-09-24T08:00:00Z" ) );
        CHECK( reader.parseField( u"2026-09-24T10:00:00+0200" )
               == asWritten( 2026, 9, 24, 8, 0, 0 ) );
        CHECK( reader.parseField( u"2026-09-24T02:00:00-06:00" )
               == asWritten( 2026, 9, 24, 8, 0, 0 ) );
        // No zone: the clock time as written.
        CHECK( reader.parseField( u"2026-09-24T08:00:00" ) == asWritten( 2026, 9, 24, 8, 0, 0 ) );
    }

    SECTION( "a format without a year takes the reference year" )
    {
        CHECK( readerOf( "syslog_log" )
                   .timestampOf( "Apr 28 04:02:03 tstack-centos5 syslogd 1.4.1: restart." )
               == asWritten( 2026, 4, 28, 4, 2, 3 ) );
        CHECK( readerOf( "logcat_log" )
                   .timestampOf( "05-26 11:18:41.509  1000 23402 23402 V Tag: msg" )
               == asWritten( 2026, 5, 26, 11, 18, 41, 509 ) );
    }

    SECTION( "an epoch value, with the fraction the format declares" )
    {
        const auto reader = readerOf( "strace_log" );
        CHECK( reader.timestampOf( "1700000000.250000 read(3, \"\", 1) = 0" )
               == QDateTime::fromMSecsSinceEpoch( 1'700'000'000'250, QTimeZone::UTC ) );
        // The alternative after the epoch one: a time of day, on no date.
        CHECK( reader.timestampOf( "08:09:33.814936 execve(\"/bin/ls\", [\"ls\"], []) = 0" )
               == asWritten( 1970, 1, 1, 8, 9, 33, 814 ) );
    }

    SECTION( "an epoch value divided by the timestamp divisor" )
    {
        const TimestampReader reader( formatOf( EpochMillisecondsJson ) );
        CHECK( reader.timestampOf( "1700000000123 hello" )
               == QDateTime::fromMSecsSinceEpoch( 1'700'000'000'123, QTimeZone::UTC ) );
    }

    SECTION( "the alternatives of a format are tried in turn" )
    {
        const TimestampReader reader( formatOf( TwoAlternativesJson ) );
        CHECK( reader.timestampOf( "2026-09-23 14:02:03 a" )
               == asWritten( 2026, 9, 23, 14, 2, 3 ) );
        CHECK( reader.timestampOf( "23.09.2026 a" ) == asWritten( 2026, 9, 23, 0, 0, 0 ) );
    }
}

TEST_CASE( "A format without a year takes it from the modification date of the Log File",
           "[logformat][timestamp]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto& syslog = *catalog.allFormats().value( QStringLiteral( "syslog_log" ) );
    const TimestampReader reader( syslog, 0, QDate( 2027, 1, 5 ) );
    const auto line = []( const char* stamp ) {
        return QStringLiteral( "%1 host prog: message" ).arg( QLatin1String( stamp ) );
    };

    // Later in the year than the modification date: the year before.
    CHECK( reader.timestampOf( line( "Dec 31 23:59:59" ) ) == asWritten( 2026, 12, 31, 23, 59, 59 ) );
    CHECK( reader.timestampOf( line( "Jan  6 00:00:00" ) ) == asWritten( 2026, 1, 6, 0, 0, 0 ) );
    // Up to the modification date: its year.
    CHECK( reader.timestampOf( line( "Jan  1 00:00:01" ) ) == asWritten( 2027, 1, 1, 0, 0, 1 ) );
    CHECK( reader.timestampOf( line( "Jan  5 12:00:00" ) ) == asWritten( 2027, 1, 5, 12, 0, 0 ) );

    // A Log File from December to January stays in time order.
    const auto december = reader.timestampOf( line( "Dec 31 23:59:59" ) );
    const auto january = reader.timestampOf( line( "Jan  1 00:00:01" ) );
    REQUIRE( ( december && january ) );
    CHECK( *december < *january );

    // Written in the middle of the year, nothing moves back.
    const TimestampReader summer( syslog, 0, QDate( 2026, 9, 24 ) );
    CHECK( summer.timestampOf( line( "Apr 28 04:02:03" ) ) == asWritten( 2026, 4, 28, 4, 2, 3 ) );
}

TEST_CASE( "The Timestamp reader has none for what carries no Timestamp", "[logformat][timestamp]" )
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto& spdlog = *catalog.allFormats().value( QStringLiteral( "spdlog_log" ) );
    const TimestampReader reader( spdlog, 2026 );
    const auto sample = spdlog.sampleLines()[ 0 ].line;

    CHECK( !reader.timestampOf( "    at com.example.Main.run(Main.java:42)" ).has_value() );
    CHECK( !reader.timestampOf( "" ).has_value() );
    // A timestamp field in a format the reader does not know.
    CHECK( !TimestampReader( formatOf( TwoAlternativesJson ) )
                .timestampOf( "yesterday a" )
                .has_value() );
    // A date that does not exist.
    auto impossible = sample;
    impossible.replace( "2025-04-24", "2025-13-45" );
    CHECK( impossible != sample );
    CHECK( !reader.timestampOf( impossible ).has_value() );
}

TEST_CASE( "The Timestamp reader is unavailable without a timestamp field in the patterns",
           "[logformat][timestamp]" )
{
    const TimestampReader reader( formatOf( NoTimestampJson ) );
    CHECK( !reader.isAvailable() );
    CHECK( !reader.timestampOf( "INFO hello" ).has_value() );
}
