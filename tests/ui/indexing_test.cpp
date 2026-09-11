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

// Indexing is built entirely from an Indexing Policy (#94): no
// Configuration, no settings bootstrap, no portable-settings constant.
// These tests build that Policy from literals and check that indexing a
// file produces the expected line count and content under each axis the
// Policy controls.

namespace {

QString writeLines( QTemporaryFile& file, int lineCount, int paddingBytes = 0 )
{
    REQUIRE( file.open() );
    const QString padding( paddingBytes, QChar( 'x' ) );
    for ( int i = 0; i < lineCount; ++i ) {
        file.write( QStringLiteral( "line %1 %2\n" ).arg( i ).arg( padding ).toUtf8() );
    }
    file.flush();
    return file.fileName();
}

void attachAndWaitForIndexing( LogData& logData, const QString& fileName )
{
    SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    logData.attachFile( fileName );
    REQUIRE( loadEndSpy.safeWait( 20000 ) );
}

} // namespace

SCENARIO( "Indexing follows the Indexing Policy it was built with", "[indexing]" )
{
    GIVEN( "a Policy using the compressed index" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.useCompressedIndex = true;
        policy.indexing.useIndexCache = false;

        QTemporaryFile file{ "indexing_compressed_XXXXXX" };
        const auto fileName = writeLines( file, 200 );

        WHEN( "the file is indexed" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "every line is indexed and readable back" )
            {
                REQUIRE( logData.getNbLine().get() == 200 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ) == "line 0 " );
                REQUIRE( logData.getLineString( LineNumber( 199 ) ) == "line 199 " );
            }
        }
    }

    GIVEN( "a Policy using the uncompressed (fast) index" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.useCompressedIndex = false;
        policy.indexing.useIndexCache = false;

        QTemporaryFile file{ "indexing_uncompressed_XXXXXX" };
        const auto fileName = writeLines( file, 200 );

        WHEN( "the file is indexed" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "every line is indexed and readable back" )
            {
                REQUIRE( logData.getNbLine().get() == 200 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ) == "line 0 " );
                REQUIRE( logData.getLineString( LineNumber( 199 ) ) == "line 199 " );
            }
        }
    }

    GIVEN( "a file spanning several indexing blocks and a Policy with a "
           "one-block read buffer" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.readBufferSizeMb = 1;
        policy.indexing.useIndexCache = false;

        // Each line is padded well past 1 KiB so a modest line count
        // still spans several 5 MiB indexing blocks, exercising the
        // prefetch limiter built from readBufferSizeMb with more than one
        // block in flight over the run.
        QTemporaryFile file{ "indexing_buffer_XXXXXX" };
        const auto fileName = writeLines( file, 20000, 1024 );

        WHEN( "the file is indexed with a single-block prefetch buffer" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "the whole file is still indexed correctly" )
            {
                REQUIRE( logData.getNbLine().get() == 20000 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ).startsWith( "line 0 " ) );
                REQUIRE( logData.getLineString( LineNumber( 19999 ) ).startsWith( "line 19999 " ) );
            }
        }

        WHEN( "the same file is indexed with a larger prefetch buffer" )
        {
            policy.indexing.readBufferSizeMb = 16;
            LogData logData{ policy.indexing, policy.search, policy.fileAccess };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "the result matches the single-block run" )
            {
                REQUIRE( logData.getNbLine().get() == 20000 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ).startsWith( "line 0 " ) );
                REQUIRE( logData.getLineString( LineNumber( 19999 ) ).startsWith( "line 19999 " ) );
            }
        }
    }
}
