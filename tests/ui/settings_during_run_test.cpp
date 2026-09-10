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

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include "configuration.h"
#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

// The options dialog is modal to the window, but it does not stop the
// indexing or search pools: pressing Apply writes the settings while a run
// is in flight.
//
// Indexing and searching are handed a Policy when they are built and read
// the settings object never (#94), so a write to it cannot reach a run at
// all -- not mid-flight, and not on the next run either, which is the
// point: what a run does is decided by the Policy it was given, and a
// changed setting reaches a live object only by being handed to it (#95).
//
// These scenarios pin that: a run whose settings are rewritten underneath
// it produces its complete, correct result, and so does the run after it.
// Where the previous version of this file said "the change took effect on
// the next run", it now says the opposite, deliberately.

namespace {

constexpr int LineCount = 20000;
// Lines are numbered %06d, so one line in ten ends in a 9.
constexpr auto MatchingEveryTenth = "line [0-9]{5}9";
constexpr auto MatchingEveryFifth = "line [0-9]{5}[13]";

bool generateTestFile( QTemporaryFile& file, int lineCount )
{
    char line[ 120 ];
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < lineCount; i++ ) {
        snprintf( line, sizeof( line ),
                  "SETTINGS_DURING_RUN_TEST line %06d "
                  "some padding to make lines longer for indexing\n",
                  i );
        file.write( line, static_cast<qint64>( qstrlen( line ) ) );
    }
    file.flush();
    return true;
}

// Writes every setting that used to be read from a worker thread, exactly
// as pressing Apply in the options dialog would, and flipped away from
// whatever the Policies below say.
void applyDifferentSettings()
{
    auto& config = Configuration::get();

    config.setIndexReadBufferSizeMb( 4 );
    config.setUseCompressedIndex( !config.useCompressedIndex() );
    config.setUseIndexCache( !config.useIndexCache() );
    config.setIndexCacheMaxSizeMb( 42 );
    config.setFastModificationDetection( !config.fastModificationDetection() );

    config.setUseParallelSearch( !config.useParallelSearch() );
    config.setSearchThreadPoolSize( 4 );
    config.setSearchReadBufferSizeLines( 5000 );
    config.setRegexpEngine( RegexpEngine::QRegularExpression );
}

bool waitForSearchToComplete( SafeQSignalSpy& searchStateSpy )
{
    return waitUiState( [ & ]() {
        if ( searchStateSpy.count() == 0 ) {
            return false;
        }
        return qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress >= 100;
    } );
}

} // namespace

SCENARIO( "Changing settings during a Search cannot alter the run in flight",
          "[logdata][settings]" )
{
    GIVEN( "a log file large enough that a Search stays busy for a while" )
    {
        QTemporaryFile file{ "settings_during_search_XXXXXX" };
        REQUIRE( generateTestFile( file, LineCount ) );

        auto policies = testSettingsPolicies();
        policies.search.useParallelSearch = true;
        policies.search.threadPoolSize = 2;
        policies.search.readBufferSizeLines = 10;
        // A cache hit would never reach the worker at all, and this test is
        // about what the worker runs on.
        policies.search.useResultsCache = false;
        policies.search.regexpEngine = RegexpEngine::Vectorscan;

        LogData logData{ policies.indexing, policies.search, policies.fileAccess };
        {
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.attachFile( file.fileName() );
            REQUIRE( loadEndSpy.safeWait( 10000 ) );
        }

        auto filtered = logData.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filtered.get(), &LogFilteredData::searchStateChanged };

        WHEN( "every setting the search worker used to read is rewritten mid-run" )
        {
            filtered->request( RegularExpressionPattern( MatchingEveryTenth ) );

            // Long enough for the run to be genuinely under way on the pool
            // threads, short enough that it cannot have finished: the writes
            // below land in the middle of it.
            QTest::qWait( 10 );
            applyDifferentSettings();

            REQUIRE( waitForSearchToComplete( searchStateSpy ) );

            THEN( "the run still produces its complete, correct result set" )
            {
                REQUIRE( filtered->getNbMatches() == LinesCount( LineCount / 10 ) );

                const auto state
                    = qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) );
                REQUIRE( state.phase == SearchSession::Phase::Complete );
                REQUIRE( state.matchCount == LinesCount( LineCount / 10 ) );
            }

            AND_WHEN( "a further Search runs" )
            {
                // Cleared so the wait below cannot be satisfied by the
                // completed state of the run that just finished.
                searchStateSpy.clear();
                filtered->request( RegularExpressionPattern( MatchingEveryFifth ) );
                REQUIRE( waitForSearchToComplete( searchStateSpy ) );

                THEN( "it runs on the Policy this object was built with, not on what "
                      "was written to the settings" )
                {
                    REQUIRE( filtered->getNbMatches() == LinesCount( LineCount / 5 ) );
                }
            }
        }
    }
}

SCENARIO( "Changing settings during indexing cannot alter the run in flight",
          "[logdata][settings]" )
{
    GIVEN( "a log file large enough that indexing takes a moment" )
    {
        QTemporaryFile file{ "settings_during_indexing_XXXXXX" };
        REQUIRE( generateTestFile( file, LineCount ) );

        auto policies = testSettingsPolicies();
        policies.indexing.readBufferSizeMb = 16;
        policies.indexing.useCompressedIndex = true;
        policies.indexing.useIndexCache = false;
        policies.indexing.fastModificationDetection = false;

        WHEN( "every setting the indexing worker used to read is rewritten mid-run" )
        {
            LogData logData{ policies.indexing, policies.search, policies.fileAccess };
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );

            logData.attachFile( file.fileName() );
            // attachFile only queues the operation; a moment's grace lets
            // the pool thread actually start reading the file, so the
            // writes below land inside the pass rather than before it.
            // (Like the destruction tests, this is a timing hint, not a
            // guarantee -- the assertions below hold either way.)
            QTest::qWait( 5 );
            applyDifferentSettings();

            REQUIRE( loadEndSpy.safeWait( 10000 ) );

            THEN( "the file is indexed completely and correctly" )
            {
                REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );
            }
        }
    }
}
