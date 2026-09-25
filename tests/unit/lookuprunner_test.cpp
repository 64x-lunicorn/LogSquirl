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

#include "lookuprunner.h"
#include "timelookup.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <chrono>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

namespace {

// A Log File whose every read takes a while, and counts them.
struct SlowLog {
    std::atomic<int> reads{ 0 };

    timelookup::TimestampAt reader()
    {
        return [ this ]( LineNumber line ) -> std::optional<QDateTime> {
            ++reads;
            std::this_thread::sleep_for( 2ms );
            return QDateTime( QDate( 2026, 9, 24 ), QTime( 10, 0, 0 ), QTimeZone::UTC )
                .addSecs( static_cast<qint64>( line.get() ) );
        };
    }
};

bool waitFor( const std::function<bool()>& condition, int milliseconds = 10'000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !condition() && timer.elapsed() < milliseconds ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    }
    return condition();
}

} // namespace

TEST_CASE( "A time lookup runs while the event loop keeps going", "[ui][lookuprunner]" )
{
    SlowLog log;
    LookupRunner runner;
    std::optional<timelookup::Result> found;
    bool done = false;

    runner.start<std::optional<timelookup::Result>>(
        [ &log ]( const std::atomic<bool>& cancelled ) {
            const auto time = QDateTime( QDate( 2026, 9, 24 ), QTime( 10, 5, 0 ), QTimeZone::UTC );
            return timelookup::firstLineAtOrAfter( time, LinesCount( 100'000 ), log.reader(),
                                                   { {}, &cancelled } );
        },
        [ & ]( std::optional<timelookup::Result> result ) {
            found = result;
            done = true;
        } );
    REQUIRE( runner.isRunning() );

    // Timer events are delivered while the worker is still reading.
    int ticks = 0;
    QTimer timer;
    QObject::connect( &timer, &QTimer::timeout, [ & ] { ++ticks; } );
    timer.start( 5 );
    REQUIRE( waitFor( [ & ] { return done; } ) );

    CHECK( ticks > 0 );
    REQUIRE( found );
    CHECK( found->line == LineNumber( 300 ) );
    CHECK( !runner.isRunning() );
}

TEST_CASE( "A cancelled time lookup leaves no result behind", "[ui][lookuprunner]" )
{
    SlowLog log;
    LookupRunner runner;
    bool reported = false;

    runner.start<std::optional<timelookup::Result>>(
        [ &log ]( const std::atomic<bool>& cancelled ) {
            const auto time = QDateTime( QDate( 2026, 9, 24 ), QTime( 12, 0, 0 ), QTimeZone::UTC );
            return timelookup::firstLineAtOrAfter( time, LinesCount( 1'000'000 ), log.reader(),
                                                   { {}, &cancelled } );
        },
        [ & ]( std::optional<timelookup::Result> ) { reported = true; } );
    REQUIRE( waitFor( [ & ] { return log.reads > 0; } ) );

    // The Log File is reloaded meanwhile.
    runner.cancel();
    CHECK( !runner.isRunning() );

    // The worker finishes and its result is dropped.
    const auto reads = log.reads.load();
    waitFor( [] { return false; }, 200 );
    CHECK( !reported );
    CHECK( log.reads.load() - reads < 50 );
}

TEST_CASE( "Starting a lookup cancels the one running", "[ui][lookuprunner]" )
{
    LookupRunner runner;
    std::atomic<bool> firstStopped{ false };
    int reported = 0;
    int value = 0;

    runner.start<int>(
        [ & ]( const std::atomic<bool>& cancelled ) {
            while ( !cancelled ) {
                std::this_thread::sleep_for( 1ms );
            }
            firstStopped = true;
            return 1;
        },
        [ & ]( int ) { ++reported; } );
    runner.start<int>( []( const std::atomic<bool>& ) { return 2; },
                       [ & ]( int result ) {
                           ++reported;
                           value = result;
                       } );

    REQUIRE( waitFor( [ & ] { return reported > 0 && firstStopped.load(); } ) );
    waitFor( [] { return false; }, 100 );
    CHECK( reported == 1 );
    CHECK( value == 2 );
}
