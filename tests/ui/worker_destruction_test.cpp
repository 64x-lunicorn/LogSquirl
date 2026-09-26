/*
 * Copyright (C) 2025 LogSquirl Contributors
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
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <memory>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logdataworker.h"
#include "logfiltereddata.h"

namespace {

// Generates a temporary file with the given number of lines.
// Returns true on success.
bool generateTestFile( QTemporaryFile& file, int lineCount )
{
    char line[ 120 ];
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < lineCount; i++ ) {
        snprintf( line, sizeof( line ),
                  "WORKER_DESTRUCTION_TEST line %06d "
                  "some padding to make lines longer for indexing\n",
                  i );
        file.write( line, static_cast<qint64>( qstrlen( line ) ) );
    }
    file.flush();
    return true;
}

// Helper: attach a file and wait for indexing to complete.
void attachAndWaitForIndexing( LogData& logData, const QString& fileName )
{
    SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    logData.attachFile( fileName );
    REQUIRE( loadEndSpy.safeWait( 10000 ) );
}

} // namespace

SCENARIO( "LogData destruction after indexing completes without deadlock",
          "[logdata][destruction]" )
{
    GIVEN( "A temporary log file" )
    {
        QTemporaryFile file{ "destruction_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 500 ) );

        WHEN( "LogData is created, indexes, and is immediately destroyed" )
        {
            {
                LogData logData{ testSettingsPolicies().indexing, testSettingsPolicies().search,
                                 testSettingsPolicies().fileAccess,
                                 testSettingsPolicies().decoding };
                attachAndWaitForIndexing( logData, file.fileName() );
                // LogData destroyed here — must not deadlock or crash
            }

            THEN( "No crash or deadlock occurred" )
            {
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "LogData destruction during active search does not deadlock", "[logdata][destruction]" )
{
    GIVEN( "A temporary log file with enough lines to keep search busy" )
    {
        QTemporaryFile file{ "destruction_search_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 5000 ) );

        WHEN( "A search is started and LogData is destroyed before it completes" )
        {
            const auto threadPoolSize = GENERATE( 0, 1, 2 );

            auto policies = testSettingsPolicies();
            policies.search.threadPoolSize = threadPoolSize;
            policies.search.useParallelSearch = threadPoolSize > 0;

            {
                LogData logData{ policies.indexing, policies.search, policies.fileAccess,
                                 policies.decoding };
                attachAndWaitForIndexing( logData, file.fileName() );

                auto filtered = logData.getNewFilteredData();

                // Start search but don't wait for completion
                filtered->request( RegularExpressionPattern( "line [0-9]{4}9" ) );

                // Small delay to let search begin on pool thread
                QTest::qWait( 10 );

                // Destroy filtered data and LogData while search may still be running
                filtered.reset();
                // logData destroyed here — must not deadlock or crash
            }

            THEN( "No crash or deadlock occurred" )
            {
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Destroying mid-search while the progress throttle is pending does not crash",
          "[logdata][destruction]" )
{
    // Regression test for the fix that shipped without one: the progress
    // throttler's own destructor calls maybeEmitTriggered(), which used to
    // invoke a slot on the partially-destroyed LogFilteredData/SearchSession
    // if a throttled emission was still pending (scheduled, not yet fired)
    // at the moment of destruction. SearchSession::~SearchSession() now
    // disconnects everything -- including the throttler -- before any
    // member is torn down, so this must be safe regardless of timing.
    GIVEN( "a log file large enough that a search stays busy past one throttle tick" )
    {
        QTemporaryFile file{ "destruction_throttle_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 20000 ) );

        WHEN( "LogData is destroyed shortly after a search starts, before it can complete" )
        {
            const auto threadPoolSize = GENERATE( 0, 1, 2 );

            auto policies = testSettingsPolicies();
            policies.search.threadPoolSize = threadPoolSize;
            policies.search.useParallelSearch = threadPoolSize > 0;

            {
                LogData logData{ policies.indexing, policies.search, policies.fileAccess,
                                 policies.decoding };
                attachAndWaitForIndexing( logData, file.fileName() );

                auto filtered = logData.getNewFilteredData();

                filtered->request( RegularExpressionPattern( "line [0-9]{4}9" ) );

                // Long enough for the worker's cross-thread progress signal
                // to be delivered at least once (scheduling the throttler's
                // 100 ms timer), short enough that the timer cannot have
                // fired yet: destruction lands with an emission genuinely
                // pending, not merely possible.
                QTest::qWait( 20 );

                filtered.reset();
                // logData destroyed here — must not deadlock or crash,
                // even though the throttler had a pending emission.
            }

            THEN( "No crash or deadlock occurred" )
            {
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Repeated LogData create-search-destroy cycles are stable", "[logdata][destruction]" )
{
    GIVEN( "A temporary log file" )
    {
        QTemporaryFile file{ "destruction_cycle_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 1000 ) );

        WHEN( "LogData is created, searched, and destroyed 5 times in a row" )
        {
            for ( int cycle = 0; cycle < 5; ++cycle ) {
                LogData logData{ testSettingsPolicies().indexing, testSettingsPolicies().search,
                                 testSettingsPolicies().fileAccess,
                                 testSettingsPolicies().decoding };
                attachAndWaitForIndexing( logData, file.fileName() );

                auto filtered = logData.getNewFilteredData();

                SafeQSignalSpy searchStateSpy{ filtered.get(),
                                               &LogFilteredData::searchStateChanged };

                filtered->request( RegularExpressionPattern( "line [0-9]{4}[13579]" ) );

                const bool completed = waitUiState( [ & ]() {
                    if ( searchStateSpy.count() == 0 ) {
                        return false;
                    }
                    return qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) )
                               .progress
                           >= 100;
                } );
                REQUIRE( completed );

                // Destroy while event loop may still have queued signals
                filtered.reset();
            }

            THEN( "All cycles completed without crash or deadlock" )
            {
                REQUIRE( true );
            }
        }
    }
}

namespace {

// A reader that counts how often it is attached and detached, as the Log
// File held open for an index run would be.
struct CountingReader {
    std::shared_ptr<std::atomic<int>> attached = std::make_shared<std::atomic<int>>( 0 );
    std::shared_ptr<std::atomic<int>> detached = std::make_shared<std::atomic<int>>( 0 );

    LogDataWorker::Reader reader() const
    {
        return { [ count = attached ] { ++*count; }, [ count = detached ] { ++*count; } };
    }
};

} // namespace

SCENARIO( "An index run keeps the reader attached for as long as it lasts",
          "[logdata][destruction]" )
{
    GIVEN( "an index worker with a reader" )
    {
        QTemporaryFile file{ "index_reader_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 500 ) );

        CountingReader counts;
        LogDataWorker worker{ std::make_shared<IndexingData>(), testSettingsPolicies().indexing,
                              counts.reader() };

        WHEN( "a Log File is attached, then checked for changes" )
        {
            SafeQSignalSpy indexed{ &worker, &LogDataWorker::indexingFinished };
            worker.run( AttachJob{ file.fileName() } );
            REQUIRE( indexed.safeWait() );
            const auto attachedWhileIndexed = counts.attached->load();
            const auto detachedWhenIndexed = counts.detached->load();

            SafeQSignalSpy checked{ &worker, &LogDataWorker::checkFileChangesFinished };
            worker.run( CheckForChangesJob{} );
            REQUIRE( checked.safeWait() );

            THEN( "each run attached the reader once, and detached it by the time it was "
                  "reported finished" )
            {
                REQUIRE( attachedWhileIndexed == 1 );
                REQUIRE( detachedWhenIndexed == 1 );
                REQUIRE( counts.attached->load() == 2 );
                REQUIRE( counts.detached->load() == 2 );
            }

            THEN( "the index run was reported Successful, and the check found nothing changed" )
            {
                REQUIRE( qvariant_cast<LoadingStatus>( indexed.first().at( 0 ) )
                         == LoadingStatus::Successful );
                REQUIRE( qvariant_cast<MonitoredFileStatus>( checked.first().at( 0 ) )
                         == MonitoredFileStatus::Unchanged );
            }
        }
    }
}

SCENARIO( "Destroying the index worker while it indexes waits for the run and reports nothing",
          "[logdata][destruction]" )
{
    GIVEN( "an index worker indexing a Log File large enough to still be indexing" )
    {
        QTemporaryFile file{ "index_destruction_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 200000 ) );

        auto policies = testSettingsPolicies();
        policies.indexing.useIndexCache = false;

        CountingReader counts;
        auto worker = std::make_unique<LogDataWorker>( std::make_shared<IndexingData>(),
                                                       policies.indexing, counts.reader() );
        SafeQSignalSpy indexed{ worker.get(), &LogDataWorker::indexingFinished };
        worker->run( AttachJob{ file.fileName() } );

        WHEN( "the worker is destroyed before the run is reported finished" )
        {
            worker.reset();
            QTest::qWait( 50 );

            THEN( "no finish is reported, and the reader is detached again" )
            {
                REQUIRE( indexed.count() == 0 );
                REQUIRE( counts.attached->load() == 1 );
                REQUIRE( counts.detached->load() == 1 );
            }
        }
    }
}

SCENARIO( "LogData destruction during indexing does not deadlock", "[logdata][destruction]" )
{
    GIVEN( "A temporary log file large enough to keep indexing busy" )
    {
        QTemporaryFile file{ "destruction_indexing_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 200000 ) );

        auto policies = testSettingsPolicies();
        policies.indexing.useIndexCache = false;
        const auto keepFileClosed = GENERATE( false, true );
        policies.fileAccess.keepFileClosed = keepFileClosed;

        WHEN( "the Log File is attached and LogData is destroyed while it is indexed" )
        {
            {
                LogData logData{ policies.indexing, policies.search, policies.fileAccess,
                                 policies.decoding };
                logData.attachFile( file.fileName() );
                QTest::qWait( 5 );
                // logData destroyed here -- must not deadlock or crash
            }
            QTest::qWait( 20 );

            THEN( "No crash or deadlock occurred" )
            {
                REQUIRE( true );
            }
        }
    }
}
