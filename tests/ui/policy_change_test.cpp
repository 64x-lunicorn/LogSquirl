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

#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

// Settings arrive as a snapshot, so a changed setting needs an explicit
// path back to whatever is already running (#95). A Log File is the end of
// that path for the Indexing and Search axes: it holds the Policies its
// workers run on, and it is what every LogFilteredData was built from --
// including the ones a tab kept from an earlier Search, which nothing else
// holds a list of.

namespace {

constexpr int LineCount = 400;

bool generateTestFile( QTemporaryFile& file )
{
    char line[ 120 ];
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < LineCount; i++ ) {
        snprintf( line, sizeof( line ), "POLICY_CHANGE_TEST this is line %06d\n", i );
        file.write( line, static_cast<qint64>( qstrlen( line ) ) );
    }
    file.flush();
    return true;
}

using LineTypeFlags = LogFilteredData::LineTypeFlags;

LineTypeFlags lineTypeOf( const LogFilteredData& data, LineNumber line )
{
    return static_cast<LineTypeFlags>(
        static_cast<LogFilteredData::LineType::Int>( data.lineTypeByLine( line ) ) );
}

void searchForSingleLine( LogFilteredData* data, const QString& pattern )
{
    SafeQSignalSpy searchStateSpy{ data, &LogFilteredData::searchStateChanged };
    data->request( RegularExpressionPattern( pattern ) );
    // The Session reaches Complete before the throttled notification that
    // carries the results into this object, so wait on the results.
    REQUIRE( waitUiState( [ & ]() {
        return data->searchState().phase == SearchSession::Phase::Complete
               && data->getNbMatches() == 1_lcount;
    } ) );
}

} // namespace

SCENARIO( "A changed Search Policy reaches every Filtered View of a Log File",
          "[logdata][settings]" )
{
    QTemporaryFile file{ "policy_change_XXXXXX" };
    REQUIRE( generateTestFile( file ) );

    GIVEN( "two Filtered Views of one Log File, as a tab that kept a Search has" )
    {
        auto policies = testSettingsPolicies();
        policies.search.useResultsCache = false;
        policies.search.contextLinesCount = 1;

        LogData logData{ policies.indexing, policies.search, policies.fileAccess };
        {
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.attachFile( file.fileName() );
            REQUIRE( loadEndSpy.safeWait( 10000 ) );
        }

        auto first = logData.getNewFilteredData();
        auto second = logData.getNewFilteredData();

        searchForSingleLine( first.get(), "this is line 000100" );
        searchForSingleLine( second.get(), "this is line 000200" );

        // One Context Line either side of each match, as the Policy says.
        REQUIRE( lineTypeOf( *first, 99_lnum ) == LineTypeFlags::Context );
        REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Plain );
        REQUIRE( lineTypeOf( *second, 199_lnum ) == LineTypeFlags::Context );
        REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Plain );

        WHEN( "a Search Policy with a wider Context Lines count arrives" )
        {
            auto changed = policies.search;
            changed.contextLinesCount = 3;
            logData.setSearchPolicy( changed );

            THEN( "both Filtered Views rebuilt their Context Lines, not only the first" )
            {
                REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Context );
            }

            THEN( "neither of them lost its matches" )
            {
                REQUIRE( lineTypeOf( *first, 100_lnum ) == LineTypeFlags::Match );
                REQUIRE( lineTypeOf( *second, 200_lnum ) == LineTypeFlags::Match );
            }
        }

        WHEN( "a Search Policy that changes some other part of the axis arrives" )
        {
            auto changed = policies.search;
            changed.useResultsCache = true;
            logData.setSearchPolicy( changed );

            THEN( "Context Lines are left exactly as they were" )
            {
                REQUIRE( lineTypeOf( *first, 99_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Plain );
                REQUIRE( lineTypeOf( *second, 199_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Plain );
            }
        }

        WHEN( "a Filtered View is destroyed and a Policy arrives afterwards" )
        {
            second.reset();

            auto changed = policies.search;
            changed.contextLinesCount = 3;

            THEN( "the surviving one is still reached, and nothing dangles" )
            {
                logData.setSearchPolicy( changed );
                REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Context );
            }
        }
    }
}

SCENARIO( "A changed Indexing Policy reaches a Log File that is already open",
          "[logdata][settings]" )
{
    QTemporaryFile file{ "policy_change_indexing_XXXXXX" };
    REQUIRE( generateTestFile( file ) );

    GIVEN( "an indexed Log File and a Search on it" )
    {
        auto policies = testSettingsPolicies();
        policies.indexing.useCompressedIndex = true;

        LogData logData{ policies.indexing, policies.search, policies.fileAccess };
        {
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.attachFile( file.fileName() );
            REQUIRE( loadEndSpy.safeWait( 10000 ) );
        }

        auto filtered = logData.getNewFilteredData();
        searchForSingleLine( filtered.get(), "this is line 000100" );

        WHEN( "an Indexing Policy with different storage arrives and the file is reloaded" )
        {
            auto changed = policies.indexing;
            changed.useCompressedIndex = false;
            logData.setIndexingPolicy( changed );

            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.reload();
            REQUIRE( loadEndSpy.safeWait( 10000 ) );

            THEN( "the reindex ran under the new Policy and the file is complete" )
            {
                REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );
            }
        }
    }
}
