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

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <QTest>
#include <QThread>

#include "backgroundrun.h"

// The Background Run with trivial jobs: plain lambdas that stand for a Search
// or an index job, and a reader that only counts.

namespace {

struct TestPolicy {
    int value = 0;
};

using TestRun = BackgroundRun<TestPolicy, int>;

// Everything a Background Run reported, as seen on the thread it lives on.
struct Reports {
    std::vector<RunEnd<int>> finished;
    std::vector<std::pair<RunId, int>> progressed;
    // What the reader counted when each report arrived.
    std::vector<int> readersAtFinish;

    int finishedCount( RunId id ) const
    {
        int count = 0;
        for ( const auto& end : finished ) {
            count += end.id == id ? 1 : 0;
        }
        return count;
    }

    std::optional<RunEnd<int>> endOf( RunId id ) const
    {
        for ( const auto& end : finished ) {
            if ( end.id == id ) {
                return end;
            }
        }
        return {};
    }
};

struct Fixture {
    std::atomic<int> readers{ 0 };
    std::atomic<int> attaches{ 0 };
    Reports reports;
    Qt::HANDLE ownerThread = QThread::currentThreadId();
    std::atomic<bool> reportedOffThread{ false };

    std::unique_ptr<TestRun> makeRun( TestPolicy policy = {} )
    {
        return std::make_unique<TestRun>(
            "Test run", policy,
            TestRun::Reader{ [ this ] {
                                ++readers;
                                ++attaches;
                            },
                             [ this ] { --readers; } },
            [ this ]( RunId id, int percent ) {
                checkThread();
                reports.progressed.emplace_back( id, percent );
            },
            [ this ]( const RunEnd<int>& end ) {
                checkThread();
                reports.finished.push_back( end );
                reports.readersAtFinish.push_back( readers.load() );
            } );
    }

    void checkThread()
    {
        if ( QThread::currentThreadId() != ownerThread ) {
            reportedOffThread = true;
        }
    }

    bool waitForFinished( std::size_t count )
    {
        return QTest::qWaitFor( [ this, count ] { return reports.finished.size() >= count; },
                                10000 );
    }
};

// A job that runs until it is superseded, telling when it has started.
TestRun::Job runUntilSuperseded( std::atomic<bool>& started, std::atomic<bool>& ended )
{
    return [ &started, &ended ]( const RunControl& control, const TestPolicy& ) {
        started = true;
        while ( !control.isSuperseded() ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        ended = true;
        return -1;
    };
}

} // namespace

SCENARIO( "A Background Run runs a job and reports it finished once", "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun( TestPolicy{ 42 } );

    WHEN( "a job that reports progress runs to its end" )
    {
        const auto id = run->start( []( const RunControl& control, const TestPolicy& policy ) {
            control.reportProgress( 50 );
            control.reportProgress( 99 );
            return policy.value;
        } );
        REQUIRE( fixture.waitForFinished( 1 ) );
        QTest::qWait( 50 );

        THEN( "its progress arrives in order, and then one finish report with what it returned" )
        {
            REQUIRE( fixture.reports.progressed.size() == 2 );
            REQUIRE( fixture.reports.progressed[ 0 ] == std::make_pair( id, 50 ) );
            REQUIRE( fixture.reports.progressed[ 1 ] == std::make_pair( id, 99 ) );
            REQUIRE( fixture.reports.finished.size() == 1 );
            const auto& end = fixture.reports.finished.front();
            REQUIRE( end.id == id );
            REQUIRE_FALSE( end.superseded );
            REQUIRE( end.failure.isEmpty() );
            REQUIRE( end.outcome == 42 );
        }

        THEN( "every report arrives on the thread the Background Run lives on" )
        {
            REQUIRE_FALSE( fixture.reportedOffThread );
        }

        THEN( "the reader was attached for the run, and detached before its finish report" )
        {
            REQUIRE( fixture.attaches == 1 );
            REQUIRE( fixture.reports.readersAtFinish.front() == 0 );
            REQUIRE( fixture.readers == 0 );
        }
    }
}

SCENARIO( "A Background Run returns from start once the run has started", "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun();

    GIVEN( "a run in flight" )
    {
        std::atomic<bool> firstStarted{ false };
        std::atomic<bool> firstEnded{ false };
        const auto first = run->start( runUntilSuperseded( firstStarted, firstEnded ) );
        REQUIRE( QTest::qWaitFor( [ &firstStarted ] { return firstStarted.load(); }, 10000 ) );

        THEN( "its reader is attached while it runs" )
        {
            REQUIRE( fixture.readers == 1 );
        }

        WHEN( "another run is started" )
        {
            std::atomic<bool> secondStarted{ false };
            std::atomic<bool> secondEnded{ false };
            const auto second = run->start( runUntilSuperseded( secondStarted, secondEnded ) );

            THEN( "start has returned only once the run before it ended" )
            {
                REQUIRE( firstEnded );
                REQUIRE( second.get() > first.get() );
            }

            run->interrupt();
            REQUIRE( fixture.waitForFinished( 2 ) );
        }
        run->interrupt();
    }
}

SCENARIO( "A newer run supersedes the run in flight", "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun();

    GIVEN( "a run in flight" )
    {
        std::atomic<bool> firstStarted{ false };
        std::atomic<bool> firstEnded{ false };
        const auto first = run->start( runUntilSuperseded( firstStarted, firstEnded ) );

        WHEN( "another run is started" )
        {
            const auto second = run->start( []( const RunControl& control, const TestPolicy& ) {
                return control.isSuperseded() ? 0 : 7;
            } );
            REQUIRE( fixture.waitForFinished( 2 ) );

            THEN( "the first is reported superseded, the second not, each once" )
            {
                REQUIRE( fixture.reports.finishedCount( first ) == 1 );
                REQUIRE( fixture.reports.finishedCount( second ) == 1 );
                REQUIRE( fixture.reports.endOf( first )->superseded );
                REQUIRE_FALSE( fixture.reports.endOf( second )->superseded );
                REQUIRE( fixture.reports.endOf( second )->outcome == 7 );
            }

            THEN( "the first finish report arrives before the second" )
            {
                REQUIRE( fixture.reports.finished[ 0 ].id == first );
                REQUIRE( fixture.reports.finished[ 1 ].id == second );
            }

            THEN( "attached and detached are in balance" )
            {
                REQUIRE( fixture.attaches == 2 );
                REQUIRE( fixture.readers == 0 );
            }
        }

        WHEN( "the run is interrupted" )
        {
            run->interrupt();
            REQUIRE( fixture.waitForFinished( 1 ) );
            QTest::qWait( 50 );

            THEN( "it is reported finished once, superseded, and its reader is detached" )
            {
                REQUIRE( fixture.reports.finished.size() == 1 );
                REQUIRE( fixture.reports.endOf( first )->superseded );
                REQUIRE( fixture.reports.endOf( first )->failure.isEmpty() );
                REQUIRE( fixture.readers == 0 );
            }
        }
    }
}

SCENARIO( "A run whose job fails is reported finished once, with the failure", "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun();

    WHEN( "the job throws an exception" )
    {
        const auto id = run->start( []( const RunControl& control, const TestPolicy& ) -> int {
            control.reportProgress( 10 );
            throw std::runtime_error( "the Log File could not be read" );
        } );
        REQUIRE( fixture.waitForFinished( 1 ) );
        QTest::qWait( 50 );

        THEN( "its finish report carries the failure, after its progress" )
        {
            REQUIRE( fixture.reports.finished.size() == 1 );
            const auto& end = fixture.reports.finished.front();
            REQUIRE( end.id == id );
            REQUIRE_FALSE( end.superseded );
            REQUIRE( end.failure.contains( "Test run" ) );
            REQUIRE( end.failure.contains( "the Log File could not be read" ) );
            REQUIRE( end.outcome == 0 );
            REQUIRE( fixture.reports.progressed.size() == 1 );
        }

        THEN( "its reader is detached" )
        {
            REQUIRE( fixture.readers == 0 );
        }
    }

    WHEN( "the job throws once it has been superseded" )
    {
        const auto id = run->start( []( const RunControl& control, const TestPolicy& ) -> int {
            while ( !control.isSuperseded() ) {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            throw std::runtime_error( "the Log File went away" );
        } );
        run->interrupt();
        REQUIRE( fixture.waitForFinished( 1 ) );

        THEN( "it is reported failed, not superseded" )
        {
            const auto end = fixture.reports.endOf( id );
            REQUIRE( end.has_value() );
            REQUIRE_FALSE( end->superseded );
            REQUIRE( end->failure.contains( "the Log File went away" ) );
            REQUIRE( fixture.readers == 0 );
        }
    }

    WHEN( "the job throws something that is no exception" )
    {
        run->start( []( const RunControl&, const TestPolicy& ) -> int { throw 3; } );
        REQUIRE( fixture.waitForFinished( 1 ) );
        QTest::qWait( 50 );

        THEN( "it is still reported finished once, as failed" )
        {
            REQUIRE( fixture.reports.finished.size() == 1 );
            REQUIRE_FALSE( fixture.reports.finished.front().failure.isEmpty() );
            REQUIRE( fixture.readers == 0 );
        }
    }
}

SCENARIO( "A run keeps the Policy it started with", "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun( TestPolicy{ 1 } );

    GIVEN( "a run in flight, started under one Policy" )
    {
        std::atomic<bool> proceed{ false };
        run->start( [ &proceed ]( const RunControl&, const TestPolicy& policy ) {
            while ( !proceed ) {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            return policy.value;
        } );

        WHEN( "the Policy is replaced while it runs, and another run follows" )
        {
            run->setPolicy( TestPolicy{ 2 } );
            proceed = true;
            REQUIRE( fixture.waitForFinished( 1 ) );
            run->start(
                []( const RunControl&, const TestPolicy& policy ) { return policy.value; } );
            REQUIRE( fixture.waitForFinished( 2 ) );

            THEN( "the run in flight kept the old Policy, the next one has the new" )
            {
                REQUIRE( fixture.reports.finished[ 0 ].outcome == 1 );
                REQUIRE( fixture.reports.finished[ 1 ].outcome == 2 );
            }
        }
    }
}

SCENARIO( "A Background Run shut down during a run stops it and reports nothing more",
          "[backgroundrun]" )
{
    Fixture fixture;
    auto run = fixture.makeRun();

    GIVEN( "a run in flight that reports progress until it is superseded" )
    {
        std::atomic<bool> started{ false };
        std::atomic<bool> ended{ false };
        run->start( [ &started, &ended ]( const RunControl& control, const TestPolicy& ) {
            started = true;
            while ( !control.isSuperseded() ) {
                control.reportProgress( 1 );
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            ended = true;
            return 0;
        } );
        REQUIRE( QTest::qWaitFor( [ &started ] { return started.load(); }, 10000 ) );

        WHEN( "the Background Run is destroyed" )
        {
            const auto shutdownStart = std::chrono::steady_clock::now();
            run.reset();
            const auto shutdownTime = std::chrono::steady_clock::now() - shutdownStart;
            const auto progressBefore = fixture.reports.progressed.size();
            QTest::qWait( 50 );

            THEN( "the job saw itself superseded and ended, well before the time limit" )
            {
                REQUIRE( ended );
                REQUIRE( shutdownTime < std::chrono::seconds( 5 ) );
            }

            THEN( "no report arrives any more, and the reader is detached" )
            {
                REQUIRE( fixture.reports.finished.empty() );
                REQUIRE( fixture.reports.progressed.size() == progressBefore );
                REQUIRE( fixture.readers == 0 );
            }
        }
    }

    GIVEN( "a run that ended, whose finish report was not delivered yet" )
    {
        run->start( []( const RunControl&, const TestPolicy& ) { return 0; } );
        // No event loop runs between the start and the destruction, so the
        // finish report is still in the mailbox.
        WHEN( "the Background Run is destroyed" )
        {
            run.reset();
            QTest::qWait( 50 );

            THEN( "the report is dropped and the reader is still detached" )
            {
                REQUIRE( fixture.reports.finished.empty() );
                REQUIRE( fixture.attaches == 1 );
                REQUIRE( fixture.readers == 0 );
            }
        }
    }
}

SCENARIO( "A finish report may destroy the Background Run that delivers it", "[backgroundrun]" )
{
    std::unique_ptr<TestRun> run;
    std::atomic<int> readers{ 0 };
    int finished = 0;
    run = std::make_unique<TestRun>(
        "Test run", TestPolicy{},
        TestRun::Reader{ [ &readers ] { ++readers; }, [ &readers ] { --readers; } }, nullptr,
        [ &run, &finished ]( const RunEnd<int>& ) {
            ++finished;
            run.reset();
        } );

    WHEN( "a run ends and its report destroys the Background Run" )
    {
        run->start( []( const RunControl& control, const TestPolicy& ) {
            control.reportProgress( 5 );
            return 0;
        } );
        REQUIRE( QTest::qWaitFor( [ &finished ] { return finished == 1; }, 10000 ) );
        QTest::qWait( 50 );

        THEN( "it was reported once and the reader is detached" )
        {
            REQUIRE( run == nullptr );
            REQUIRE( finished == 1 );
            REQUIRE( readers == 0 );
        }
    }
}
