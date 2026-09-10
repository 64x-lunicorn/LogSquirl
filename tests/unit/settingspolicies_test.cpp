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

#include "configuration.h"
#include "settingspolicies.h"

SCENARIO( "A Settings Policy is a value a test can build from literals", "[settingspolicies]" )
{
    // No settings store, no persistable bootstrap, no ambient accessor:
    // that a Policy is buildable this way is what makes it usable as a
    // constructor parameter for the parts that will hold one.
    GIVEN( "Policies built from literals" )
    {
        const SearchPolicy search{ .useParallelSearch = false,
                                   .threadPoolSize = 3,
                                   .readBufferSizeLines = 512,
                                   .useResultsCache = true,
                                   .resultsCacheLines = 99u,
                                   .regexpEngine = RegexpEngine::QRegularExpression };
        const IndexingPolicy indexing{ .readBufferSizeMb = 7,
                                       .useCompressedIndex = false,
                                       .useIndexCache = true,
                                       .cacheMaxSizeMb = 64,
                                       .fastModificationDetection = true };
        const WatchPolicy watch{ .nativeWatchEnabled = false,
                                 .pollingEnabled = true,
                                 .pollIntervalMs = 250 };
        const FileAccessPolicy fileAccess{ .keepFileClosed = true,
                                           .defaultEncodingMib = 106,
                                           .extractArchives = false,
                                           .extractArchivesAlways = true };

        THEN( "each field holds what was written" )
        {
            REQUIRE_FALSE( search.useParallelSearch );
            REQUIRE( search.threadPoolSize == 3 );
            REQUIRE( search.readBufferSizeLines == 512 );
            REQUIRE( search.useResultsCache );
            REQUIRE( search.resultsCacheLines == 99u );
            REQUIRE( search.regexpEngine == RegexpEngine::QRegularExpression );

            REQUIRE( indexing.readBufferSizeMb == 7 );
            REQUIRE_FALSE( indexing.useCompressedIndex );
            REQUIRE( indexing.useIndexCache );
            REQUIRE( indexing.cacheMaxSizeMb == 64 );
            REQUIRE( indexing.fastModificationDetection );

            REQUIRE_FALSE( watch.nativeWatchEnabled );
            REQUIRE( watch.pollingEnabled );
            REQUIRE( watch.pollIntervalMs == 250 );

            REQUIRE( fileAccess.keepFileClosed );
            REQUIRE( fileAccess.defaultEncodingMib == 106 );
            REQUIRE_FALSE( fileAccess.extractArchives );
            REQUIRE( fileAccess.extractArchivesAlways );
        }

        THEN( "they are copyable" )
        {
            const auto copy = search;
            REQUIRE( copy.threadPoolSize == search.threadPoolSize );
            REQUIRE( copy.regexpEngine == search.regexpEngine );
        }
    }

    GIVEN( "a default-built Policy" )
    {
        // A Policy carries no defaults of its own: the shipped values live
        // in Configuration and reach a Policy only through
        // deriveSettingsPolicies(), so an underived one is visibly empty
        // rather than plausibly stale.
        const SearchPolicy search{};
        const IndexingPolicy indexing{};
        const WatchPolicy watch{};
        const FileAccessPolicy fileAccess{};

        THEN( "every field is value-initialised" )
        {
            REQUIRE_FALSE( search.useParallelSearch );
            REQUIRE( search.threadPoolSize == 0 );
            REQUIRE( search.readBufferSizeLines == 0 );
            REQUIRE_FALSE( search.useResultsCache );
            REQUIRE( search.resultsCacheLines == 0u );

            REQUIRE( indexing.readBufferSizeMb == 0 );
            REQUIRE( indexing.cacheMaxSizeMb == 0 );
            REQUIRE_FALSE( indexing.useCompressedIndex );

            REQUIRE( watch.pollIntervalMs == 0 );
            REQUIRE_FALSE( watch.nativeWatchEnabled );

            REQUIRE( fileAccess.defaultEncodingMib == 0 );
            REQUIRE_FALSE( fileAccess.extractArchives );
        }
    }
}

SCENARIO( "The Policies are derived from the Configuration", "[settingspolicies]" )
{
    GIVEN( "a Configuration with a distinctive value in every field a Policy names" )
    {
        Configuration config;

        config.setIndexReadBufferSizeMb( 11 );
        config.setUseCompressedIndex( false );
        config.setUseIndexCache( true );
        config.setIndexCacheMaxSizeMb( 123 );
        config.setFastModificationDetection( true );

        config.setUseParallelSearch( false );
        config.setSearchThreadPoolSize( 5 );
        config.setSearchReadBufferSizeLines( 321 );
        config.setUseSearchResultsCache( false );
        config.setSearchResultsCacheLines( 4242u );
        config.setRegexpEngine( RegexpEngine::QRegularExpression );

        config.setNativeFileWatchEnabled( false );
        config.setPollingEnabled( true );
        config.setPollIntervalMs( 777 );

        config.setKeepFileClosed( true );
        config.setDefaultEncodingMib( 106 );
        config.setExtractArchives( false );
        config.setExtractArchivesAlways( true );

        WHEN( "the Policies are derived from it" )
        {
            const auto policies = deriveSettingsPolicies( config );

            THEN( "the Indexing Policy carries the indexing settings" )
            {
                REQUIRE( policies.indexing.readBufferSizeMb == 11 );
                REQUIRE_FALSE( policies.indexing.useCompressedIndex );
                REQUIRE( policies.indexing.useIndexCache );
                REQUIRE( policies.indexing.cacheMaxSizeMb == 123 );
                REQUIRE( policies.indexing.fastModificationDetection );
            }

            THEN( "the Search Policy carries the search settings" )
            {
                REQUIRE_FALSE( policies.search.useParallelSearch );
                REQUIRE( policies.search.threadPoolSize == 5 );
                REQUIRE( policies.search.readBufferSizeLines == 321 );
                REQUIRE_FALSE( policies.search.useResultsCache );
                REQUIRE( policies.search.resultsCacheLines == 4242u );
                REQUIRE( policies.search.regexpEngine == RegexpEngine::QRegularExpression );
            }

            THEN( "the Watch Policy carries the watch settings" )
            {
                REQUIRE_FALSE( policies.watch.nativeWatchEnabled );
                REQUIRE( policies.watch.pollingEnabled );
                REQUIRE( policies.watch.pollIntervalMs == 777 );
            }

            THEN( "the File Access Policy carries the file access settings" )
            {
                REQUIRE( policies.fileAccess.keepFileClosed );
                REQUIRE( policies.fileAccess.defaultEncodingMib == 106 );
                REQUIRE_FALSE( policies.fileAccess.extractArchives );
                REQUIRE( policies.fileAccess.extractArchivesAlways );
            }
        }
    }
}
