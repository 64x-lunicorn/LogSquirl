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

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
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
                                 testSettingsPolicies().fileAccess };
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

SCENARIO( "LogData destruction during active search does not deadlock",
          "[logdata][destruction]" )
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
                LogData logData{ policies.indexing, policies.search, policies.fileAccess };
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
                LogData logData{ policies.indexing, policies.search, policies.fileAccess };
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

SCENARIO( "Repeated LogData create-search-destroy cycles are stable",
          "[logdata][destruction]" )
{
    GIVEN( "A temporary log file" )
    {
        QTemporaryFile file{ "destruction_cycle_test_XXXXXX" };
        REQUIRE( generateTestFile( file, 1000 ) );

        WHEN( "LogData is created, searched, and destroyed 5 times in a row" )
        {
            for ( int cycle = 0; cycle < 5; ++cycle ) {
                LogData logData{ testSettingsPolicies().indexing, testSettingsPolicies().search,
                                 testSettingsPolicies().fileAccess };
                attachAndWaitForIndexing( logData, file.fileName() );

                auto filtered = logData.getNewFilteredData();

                SafeQSignalSpy searchStateSpy{
                    filtered.get(), &LogFilteredData::searchStateChanged };

                filtered->request(
                    RegularExpressionPattern( "line [0-9]{4}[13579]" ) );

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
