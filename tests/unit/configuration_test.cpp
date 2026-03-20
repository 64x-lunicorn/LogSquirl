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

#include <QSettings>
#include <QTemporaryFile>

SCENARIO( "Configuration default values", "[configuration]" )
{
    GIVEN( "A freshly constructed Configuration" )
    {
        Configuration config;

        THEN( "Regexp type defaults to ExtendedRegexp" )
        {
            REQUIRE( config.mainRegexpType() == SearchRegexpType::ExtendedRegexp );
        }

        THEN( "Quickfind regexp type defaults to FixedString" )
        {
            REQUIRE( config.quickfindRegexpType() == SearchRegexpType::FixedString );
        }

        THEN( "Quickfind incremental is enabled by default" )
        {
            REQUIRE( config.isQuickfindIncremental() );
        }

        THEN( "Language defaults to en" )
        {
            REQUIRE( config.language() == "en" );
        }

        THEN( "Native file watch is enabled by default" )
        {
            REQUIRE( config.nativeFileWatchEnabled() );
        }

        THEN( "Load last session is enabled" )
        {
            REQUIRE( config.loadLastSession() );
        }

        THEN( "Follow file on load is disabled" )
        {
            REQUIRE_FALSE( config.followFileOnLoad() );
        }

        THEN( "Multiple windows are disabled" )
        {
            REQUIRE_FALSE( config.allowMultipleWindows() );
        }

        THEN( "Overview is visible" )
        {
            REQUIRE( config.isOverviewVisible() );
        }

        THEN( "Search auto-refresh defaults to off" )
        {
            REQUIRE_FALSE( config.isSearchAutoRefreshDefault() );
        }

        THEN( "Search ignore case defaults to off" )
        {
            REQUIRE_FALSE( config.isSearchIgnoreCaseDefault() );
        }

        THEN( "Parallel search is enabled" )
        {
            REQUIRE( config.useParallelSearch() );
        }

        THEN( "Search results cache is enabled" )
        {
            REQUIRE( config.useSearchResultsCache() );
        }

        THEN( "Compressed index is enabled" )
        {
            REQUIRE( config.useCompressedIndex() );
        }

        THEN( "Version checking is enabled" )
        {
            REQUIRE( config.versionCheckingEnabled() );
        }

        THEN( "Archive extraction is enabled but not always" )
        {
            REQUIRE( config.extractArchives() );
            REQUIRE_FALSE( config.extractArchivesAlways() );
        }

        THEN( "SSL peer verification is on" )
        {
            REQUIRE( config.verifySslPeers() );
        }

        THEN( "Regexp engine defaults to Hyperscan" )
        {
            REQUIRE( config.regexpEngine() == RegexpEngine::Hyperscan );
        }

        THEN( "Text wrap is disabled" )
        {
            REQUIRE_FALSE( config.useTextWrap() );
        }

        THEN( "Hide ANSI color sequences is disabled" )
        {
            REQUIRE_FALSE( config.hideAnsiColorSequences() );
        }

        THEN( "Logging is disabled" )
        {
            REQUIRE_FALSE( config.enableLogging() );
        }
    }
}

SCENARIO( "Configuration setters and getters", "[configuration]" )
{
    GIVEN( "A Configuration object" )
    {
        Configuration config;

        WHEN( "Setting main regexp type to Wildcard" )
        {
            config.setMainRegexpType( SearchRegexpType::Wildcard );
            THEN( "Getter returns Wildcard" )
            {
                REQUIRE( config.mainRegexpType() == SearchRegexpType::Wildcard );
            }
        }

        WHEN( "Setting language to de" )
        {
            config.setLanguage( "de" );
            THEN( "Getter returns de" )
            {
                REQUIRE( config.language() == "de" );
            }
        }

        WHEN( "Toggling file watch settings" )
        {
            config.setNativeFileWatchEnabled( false );
            config.setPollingEnabled( true );
            config.setPollIntervalMs( 5000 );

            THEN( "Values are updated" )
            {
                REQUIRE_FALSE( config.nativeFileWatchEnabled() );
                REQUIRE( config.pollingEnabled() );
                REQUIRE( config.pollIntervalMs() == 5000 );
            }

            THEN( "anyFileWatchEnabled still returns true when polling is on" )
            {
                REQUIRE( config.anyFileWatchEnabled() );
            }
        }

        WHEN( "Disabling all file watching" )
        {
            config.setNativeFileWatchEnabled( false );
            config.setPollingEnabled( false );

            THEN( "anyFileWatchEnabled returns false" )
            {
                REQUIRE_FALSE( config.anyFileWatchEnabled() );
            }
        }

        WHEN( "Setting performance options" )
        {
            config.setUseParallelSearch( false );
            config.setSearchResultsCacheLines( 500000 );
            config.setIndexReadBufferSizeMb( 32 );
            config.setSearchReadBufferSizeLines( 20000 );
            config.setSearchThreadPoolSize( 4 );
            config.setKeepFileClosed( true );

            THEN( "All values are persisted in memory" )
            {
                REQUIRE_FALSE( config.useParallelSearch() );
                REQUIRE( config.searchResultsCacheLines() == 500000 );
                REQUIRE( config.indexReadBufferSizeMb() == 32 );
                REQUIRE( config.searchReadBufferSizeLines() == 20000 );
                REQUIRE( config.searchThreadPoolSize() == 4 );
                REQUIRE( config.keepFileClosed() );
            }
        }

        WHEN( "Setting view options" )
        {
            config.setOverviewVisible( false );
            config.setMainLineNumbersVisible( true );
            config.setFilteredLineNumbersVisible( false );
            config.setMinimizeToTray( true );

            THEN( "Values are reflected" )
            {
                REQUIRE_FALSE( config.isOverviewVisible() );
                REQUIRE( config.mainLineNumbersVisible() );
                REQUIRE_FALSE( config.filteredLineNumbersVisible() );
                REQUIRE( config.minimizeToTray() );
            }
        }

        WHEN( "Setting search defaults" )
        {
            config.setSearchAutoRefreshDefault( true );
            config.setSearchIgnoreCaseDefault( true );
            config.setSearchLogicalCombiningDefault( true );

            THEN( "Defaults are updated" )
            {
                REQUIRE( config.isSearchAutoRefreshDefault() );
                REQUIRE( config.isSearchIgnoreCaseDefault() );
                REQUIRE( config.isSearchLogicalCombiningDefault() );
            }
        }
    }
}

SCENARIO( "Configuration save and restore round-trip", "[configuration]" )
{
    GIVEN( "A Configuration with non-default values" )
    {
        Configuration config;
        config.setMainRegexpType( SearchRegexpType::Wildcard );
        config.setLanguage( "fr" );
        config.setNativeFileWatchEnabled( false );
        config.setPollingEnabled( true );
        config.setPollIntervalMs( 3000 );
        config.setLoadLastSession( false );
        config.setFollowFileOnLoad( true );
        config.setUseParallelSearch( false );
        config.setSearchAutoRefreshDefault( true );
        config.setOverviewVisible( false );
        config.setEnableLogging( true );
        config.setLoggingLevel( 2 );
        config.setHideAnsiColorSequences( true );
        config.setUseTextWrap( true );

        WHEN( "Saved to QSettings and restored" )
        {
            // Use a temporary file for settings to avoid polluting real config
            QTemporaryFile tmpFile;
            REQUIRE( tmpFile.open() );
            QString tmpPath = tmpFile.fileName();
            tmpFile.close();

            {
                QSettings settings( tmpPath, QSettings::IniFormat );
                config.saveToStorage( settings );
                settings.sync();
            }

            Configuration restored;
            {
                QSettings settings( tmpPath, QSettings::IniFormat );
                restored.retrieveFromStorage( settings );
            }

            THEN( "Regexp type is preserved" )
            {
                REQUIRE( restored.mainRegexpType() == SearchRegexpType::Wildcard );
            }

            THEN( "Language is preserved" )
            {
                REQUIRE( restored.language() == "fr" );
            }

            THEN( "File watch settings are preserved" )
            {
                REQUIRE_FALSE( restored.nativeFileWatchEnabled() );
                REQUIRE( restored.pollingEnabled() );
                REQUIRE( restored.pollIntervalMs() == 3000 );
            }

            THEN( "Session settings are preserved" )
            {
                REQUIRE_FALSE( restored.loadLastSession() );
                REQUIRE( restored.followFileOnLoad() );
            }

            THEN( "Performance settings are preserved" )
            {
                REQUIRE_FALSE( restored.useParallelSearch() );
            }

            THEN( "Search defaults are preserved" )
            {
                REQUIRE( restored.isSearchAutoRefreshDefault() );
            }

            THEN( "View settings are preserved" )
            {
                REQUIRE_FALSE( restored.isOverviewVisible() );
            }

            THEN( "Logging settings are preserved" )
            {
                REQUIRE( restored.enableLogging() );
                REQUIRE( restored.loggingLevel() == 2 );
            }

            THEN( "Feature flags are preserved" )
            {
                REQUIRE( restored.hideAnsiColorSequences() );
                REQUIRE( restored.useTextWrap() );
            }
        }
    }
}
