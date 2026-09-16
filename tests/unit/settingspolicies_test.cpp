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

#include <QDir>

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
                                   .regexpEngine = RegexpEngine::QRegularExpression,
                                   .contextLinesCount = 4 };
        const IndexingPolicy indexing{ .readBufferSizeMb = 7,
                                       .useCompressedIndex = false,
                                       .useIndexCache = true,
                                       .cacheMaxSizeMb = 64,
                                       .fastModificationDetection = true,
                                       .indexCacheDirectory = "/tmp/index-cache" };
        const WatchPolicy watch{ .nativeWatchEnabled = false,
                                 .pollingEnabled = true,
                                 .pollIntervalMs = 250 };
        const FileAccessPolicy fileAccess{ .keepFileClosed = true,
                                           .defaultEncodingMib = 106,
                                           .extractArchives = false,
                                           .extractArchivesAlways = true };
        const DecodingPolicy decoding{ .hideAnsiColorSequences = true };
        const PresentationPolicy presentation{ .useTextWrap = true,
                                               .fastScrollEnabled = false,
                                               .fastScrollMultiplier = 6,
                                               .allowFollowOnScroll = true,
                                               .autoShowTableView = false };
        const QuickFindPolicy quickFind{ .quickFindRegexpType = SearchRegexpType::Wildcard,
                                         .mainRegexpType = SearchRegexpType::FixedString,
                                         .ignoreCase = true,
                                         .incremental = false,
                                         .autoRunSearchOnPatternChange = true };

        THEN( "each field holds what was written" )
        {
            REQUIRE( decoding.hideAnsiColorSequences );

            REQUIRE( presentation.useTextWrap );
            REQUIRE_FALSE( presentation.fastScrollEnabled );
            REQUIRE( presentation.fastScrollMultiplier == 6 );
            REQUIRE( presentation.allowFollowOnScroll );
            REQUIRE_FALSE( presentation.autoShowTableView );

            REQUIRE( quickFind.quickFindRegexpType == SearchRegexpType::Wildcard );
            REQUIRE( quickFind.mainRegexpType == SearchRegexpType::FixedString );
            REQUIRE( quickFind.ignoreCase );
            REQUIRE_FALSE( quickFind.incremental );
            REQUIRE( quickFind.autoRunSearchOnPatternChange );

            REQUIRE_FALSE( search.useParallelSearch );
            REQUIRE( search.threadPoolSize == 3 );
            REQUIRE( search.readBufferSizeLines == 512 );
            REQUIRE( search.useResultsCache );
            REQUIRE( search.resultsCacheLines == 99u );
            REQUIRE( search.regexpEngine == RegexpEngine::QRegularExpression );
            REQUIRE( search.contextLinesCount == 4 );

            REQUIRE( indexing.readBufferSizeMb == 7 );
            REQUIRE_FALSE( indexing.useCompressedIndex );
            REQUIRE( indexing.useIndexCache );
            REQUIRE( indexing.cacheMaxSizeMb == 64 );
            REQUIRE( indexing.fastModificationDetection );
            REQUIRE( indexing.indexCacheDirectory == "/tmp/index-cache" );

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
        const RecognitionPolicy recognition{};
        const DecodingPolicy decoding{};
        const PresentationPolicy presentation{};
        const QuickFindPolicy quickFind{};

        THEN( "every field is value-initialised" )
        {
            REQUIRE_FALSE( decoding.hideAnsiColorSequences );

            REQUIRE_FALSE( presentation.useTextWrap );
            REQUIRE_FALSE( presentation.fastScrollEnabled );
            REQUIRE( presentation.fastScrollMultiplier == 0 );
            REQUIRE_FALSE( presentation.allowFollowOnScroll );
            REQUIRE_FALSE( presentation.autoShowTableView );

            REQUIRE_FALSE( quickFind.ignoreCase );
            REQUIRE_FALSE( quickFind.incremental );
            REQUIRE_FALSE( quickFind.autoRunSearchOnPatternChange );

            REQUIRE_FALSE( search.useParallelSearch );
            REQUIRE( search.threadPoolSize == 0 );
            REQUIRE( search.readBufferSizeLines == 0 );
            REQUIRE_FALSE( search.useResultsCache );
            REQUIRE( search.resultsCacheLines == 0u );
            REQUIRE( search.contextLinesCount == 0 );

            REQUIRE( indexing.readBufferSizeMb == 0 );
            REQUIRE( indexing.cacheMaxSizeMb == 0 );
            REQUIRE_FALSE( indexing.useCompressedIndex );
            REQUIRE( indexing.indexCacheDirectory.isEmpty() );
            REQUIRE( indexing.indexCacheExcludedDirectory.isEmpty() );

            REQUIRE( watch.pollIntervalMs == 0 );
            REQUIRE_FALSE( watch.nativeWatchEnabled );

            REQUIRE( fileAccess.defaultEncodingMib == 0 );
            REQUIRE_FALSE( fileAccess.extractArchives );

            REQUIRE_FALSE( recognition.enabled );
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
        config.setContextLinesCount( 9 );

        config.setNativeFileWatchEnabled( false );
        config.setPollingEnabled( true );
        config.setPollIntervalMs( 777 );

        config.setKeepFileClosed( true );
        config.setDefaultEncodingMib( 106 );
        config.setExtractArchives( false );
        config.setExtractArchivesAlways( true );

        // Shipped disabled, so enabling it is the distinctive value.
        config.setAutoDetectLogFormats( true );

        // Shipped showing them, so hiding them is the distinctive value.
        config.setHideAnsiColorSequences( true );

        // A different value in each neighbouring field, so a mapping that
        // reads the wrong getter cannot pass unnoticed.
        config.setUseTextWrap( true );
        config.setFastScrollEnabled( false );
        config.setFastScrollMultiplier( 13 );
        config.setAllowFollowOnScroll( true );
        config.setAutoShowTableView( false );

        // The two regexp types differ from each other, so a derivation that
        // reads one where it means the other is caught.
        config.setQuickfindRegexpType( SearchRegexpType::Wildcard );
        config.setMainRegexpType( SearchRegexpType::FixedString );
        config.setQfIgnoreCase( true );
        config.setQuickfindIncremental( false );
        config.setAutoRunSearchOnPatternChange( true );

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
                REQUIRE_FALSE( policies.indexing.indexCacheDirectory.isEmpty() );
                REQUIRE( policies.indexing.indexCacheDirectory == config.indexCacheDirectory() );
                REQUIRE( policies.indexing.indexCacheExcludedDirectory == QDir::tempPath() );
            }

            THEN( "the Search Policy carries the search settings" )
            {
                REQUIRE_FALSE( policies.search.useParallelSearch );
                REQUIRE( policies.search.threadPoolSize == 5 );
                REQUIRE( policies.search.readBufferSizeLines == 321 );
                REQUIRE_FALSE( policies.search.useResultsCache );
                REQUIRE( policies.search.resultsCacheLines == 4242u );
                REQUIRE( policies.search.regexpEngine == RegexpEngine::QRegularExpression );
                REQUIRE( policies.search.contextLinesCount == 9 );
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

            THEN( "the Recognition Policy carries whether Format Recognition is enabled" )
            {
                REQUIRE( policies.recognition.enabled );
            }

            THEN( "the Decoding Policy carries whether ANSI color sequences are hidden" )
            {
                REQUIRE( policies.decoding.hideAnsiColorSequences );
            }

            THEN( "the Presentation Policy carries what showing a Log File needs" )
            {
                REQUIRE( policies.presentation.useTextWrap );
                REQUIRE_FALSE( policies.presentation.fastScrollEnabled );
                REQUIRE( policies.presentation.fastScrollMultiplier == 13 );
                REQUIRE( policies.presentation.allowFollowOnScroll );
                REQUIRE_FALSE( policies.presentation.autoShowTableView );
            }

            THEN( "the QuickFind Policy carries what searching interactively needs" )
            {
                REQUIRE( policies.quickFind.quickFindRegexpType == SearchRegexpType::Wildcard );
                REQUIRE( policies.quickFind.mainRegexpType == SearchRegexpType::FixedString );
                REQUIRE( policies.quickFind.ignoreCase );
                REQUIRE_FALSE( policies.quickFind.incremental );
                REQUIRE( policies.quickFind.autoRunSearchOnPatternChange );
            }
        }

        WHEN( "QuickFind is made incremental and the Policies are derived again" )
        {
            config.setQuickfindIncremental( true );
            const auto policies = deriveSettingsPolicies( config );

            THEN( "the QuickFind Policy says so" )
            {
                REQUIRE( policies.quickFind.incremental );
            }

            THEN( "only the QuickFind axis differs from the non-incremental derivation" )
            {
                config.setQuickfindIncremental( false );
                const auto stepwise = deriveSettingsPolicies( config );
                REQUIRE( policies.quickFind != stepwise.quickFind );
                REQUIRE( policies.indexing == stepwise.indexing );
                REQUIRE( policies.search == stepwise.search );
                REQUIRE( policies.watch == stepwise.watch );
                REQUIRE( policies.fileAccess == stepwise.fileAccess );
                REQUIRE( policies.recognition == stepwise.recognition );
                REQUIRE( policies.decoding == stepwise.decoding );
                REQUIRE( policies.decoration == stepwise.decoration );
                REQUIRE( policies.presentation == stepwise.presentation );
            }
        }

        WHEN( "text wrapping is switched off and the Policies are derived again" )
        {
            config.setUseTextWrap( false );
            const auto policies = deriveSettingsPolicies( config );

            THEN( "the Presentation Policy says so" )
            {
                REQUIRE_FALSE( policies.presentation.useTextWrap );
            }

            THEN( "only the Presentation axis differs from the wrapping derivation" )
            {
                config.setUseTextWrap( true );
                const auto wrapping = deriveSettingsPolicies( config );
                REQUIRE( policies.presentation != wrapping.presentation );
                REQUIRE( policies.indexing == wrapping.indexing );
                REQUIRE( policies.search == wrapping.search );
                REQUIRE( policies.watch == wrapping.watch );
                REQUIRE( policies.fileAccess == wrapping.fileAccess );
                REQUIRE( policies.recognition == wrapping.recognition );
                REQUIRE( policies.decoding == wrapping.decoding );
                REQUIRE( policies.decoration == wrapping.decoration );
                REQUIRE( policies.quickFind == wrapping.quickFind );
            }
        }

        WHEN( "ANSI color sequences are shown and the Policies are derived again" )
        {
            config.setHideAnsiColorSequences( false );
            const auto policies = deriveSettingsPolicies( config );

            THEN( "the Decoding Policy says so" )
            {
                REQUIRE_FALSE( policies.decoding.hideAnsiColorSequences );
            }

            THEN( "only the Decoding axis differs from the hiding derivation" )
            {
                config.setHideAnsiColorSequences( true );
                const auto hiding = deriveSettingsPolicies( config );
                REQUIRE( policies.decoding != hiding.decoding );
                REQUIRE( policies.indexing == hiding.indexing );
                REQUIRE( policies.search == hiding.search );
                REQUIRE( policies.watch == hiding.watch );
                REQUIRE( policies.fileAccess == hiding.fileAccess );
                REQUIRE( policies.recognition == hiding.recognition );
            }
        }

        WHEN( "Format Recognition is disabled and the Policies are derived again" )
        {
            config.setAutoDetectLogFormats( false );
            const auto policies = deriveSettingsPolicies( config );

            THEN( "the Recognition Policy says so" )
            {
                REQUIRE_FALSE( policies.recognition.enabled );
            }

            THEN( "only the Recognition axis differs from the enabled derivation" )
            {
                config.setAutoDetectLogFormats( true );
                const auto enabled = deriveSettingsPolicies( config );
                REQUIRE( policies.recognition != enabled.recognition );
                REQUIRE( policies.indexing == enabled.indexing );
                REQUIRE( policies.search == enabled.search );
                REQUIRE( policies.watch == enabled.watch );
                REQUIRE( policies.fileAccess == enabled.fileAccess );
                REQUIRE( policies.decoding == enabled.decoding );
            }
        }
    }
}
