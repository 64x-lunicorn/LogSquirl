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
#include "configurationfixture.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

using namespace configuration_fixture;

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

        THEN( "Plugins auto-load is enabled by default" )
        {
            REQUIRE( config.pluginsAutoLoad() );
        }

        THEN( "Enabled plugins list is empty by default" )
        {
            REQUIRE( config.enabledPlugins().isEmpty() );
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

        THEN( "Regexp engine defaults to Vectorscan" )
        {
            REQUIRE( config.regexpEngine() == RegexpEngine::Vectorscan );
        }

        THEN( "Text wrap is disabled" )
        {
            REQUIRE_FALSE( config.useTextWrap() );
        }

        THEN( "Hide ANSI color sequences is disabled" )
        {
            REQUIRE_FALSE( config.hideAnsiColorSequences() );
        }

        THEN( "Context lines count defaults to 5" )
        {
            REQUIRE( config.contextLinesCount() == 5 );
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
            config.setContextLinesCount( 5 );

            THEN( "Values are reflected" )
            {
                REQUIRE_FALSE( config.isOverviewVisible() );
                REQUIRE( config.mainLineNumbersVisible() );
                REQUIRE_FALSE( config.filteredLineNumbersVisible() );
                REQUIRE( config.minimizeToTray() );
                REQUIRE( config.contextLinesCount() == 5 );
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
        config.setContextLinesCount( 10 );

        WHEN( "Saved to QSettings and restored" )
        {
            // Use a temporary directory so QSettings can freely create and write
            // its own file — avoids file-locking issues on Windows.
            QTemporaryDir tmpDir;
            REQUIRE( tmpDir.isValid() );
            QString tmpPath = tmpDir.path() + QDir::separator() + "test_config.ini";

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
                REQUIRE( restored.contextLinesCount() == 10 );
            }
        }
    }
}

SCENARIO( "Configuration plugin settings round-trip", "[configuration][plugins]" )
{
    GIVEN( "A Configuration with plugin settings" )
    {
        Configuration config;
        config.setPluginsAutoLoad( false );
        config.setEnabledPlugins( QStringList{ "com.example.a", "com.example.b" } );

        WHEN( "Saved and restored" )
        {
            QTemporaryDir tmpDir;
            REQUIRE( tmpDir.isValid() );
            QString tmpPath = tmpDir.path() + QDir::separator() + "plugin_config.ini";

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

            THEN( "Auto-load setting is preserved" )
            {
                REQUIRE_FALSE( restored.pluginsAutoLoad() );
            }

            THEN( "Enabled plugins list is preserved" )
            {
                const auto ids = restored.enabledPlugins();
                REQUIRE( ids.size() == 2 );
                REQUIRE( ids.contains( "com.example.a" ) );
                REQUIRE( ids.contains( "com.example.b" ) );
            }
        }
    }
}

SCENARIO( "Log format settings default values", "[configuration]" )
{
    GIVEN( "A freshly constructed Configuration" )
    {
        Configuration config;

        THEN( "Auto-detect log formats is disabled by default" )
        {
            REQUIRE_FALSE( config.autoDetectLogFormats() );
        }

        THEN( "Auto-show table view is disabled by default" )
        {
            REQUIRE_FALSE( config.autoShowTableView() );
        }
    }
}

SCENARIO( "Log format settings round-trip through QSettings", "[configuration]" )
{
    GIVEN( "A temporary settings file" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );
        const auto tmpPath = tmpDir.path() + "/test_logformat.ini";

        WHEN( "Log format settings are saved and restored" )
        {
            {
                Configuration config;
                config.setAutoDetectLogFormats( true );
                config.setAutoShowTableView( true );
                QSettings settings( tmpPath, QSettings::IniFormat );
                config.saveToStorage( settings );
            }

            Configuration restored;
            {
                QSettings settings( tmpPath, QSettings::IniFormat );
                restored.retrieveFromStorage( settings );
            }

            THEN( "Auto-detect is preserved" )
            {
                REQUIRE( restored.autoDetectLogFormats() );
            }

            THEN( "Auto-show table view is preserved" )
            {
                REQUIRE( restored.autoShowTableView() );
            }
        }
    }
}

namespace {

// Every setting the Configuration stores. A setting missing from the write
// path, or stored under a new key, changes this list.
const QStringList StoredSettingNames = {
    "archives.extract",
    "archives.extractAlways",
    "chartPresets",
    "dark",
    "defaultView.encodingMib",
    "defaultView.searchAutoRefresh",
    "defaultView.searchIgnoreCase",
    "defaultView.searchLogicalCombining",
    "defaultView.splitterSizes",
    "filewatch.allowFollowOnScroll",
    "filewatch.fastModificationDetection",
    "filewatch.pollingIntervalMs",
    "filewatch.useNative",
    "filewatch.usePolling",
    "logformat.autoDetect",
    "logformat.autoShowTable",
    "logging.enableLogging",
    "logging.verbosity",
    "mainFont.antialiasing",
    "mainFont.bold",
    "mainFont.family",
    "mainFont.size",
    "net.verifySslPeers",
    "perf.indexCacheMaxSizeMb",
    "perf.indexReadBufferSizeMb",
    "perf.keepFileClosed",
    "perf.optimizeForNotLatinEncodings",
    "perf.searchReadBufferSizeLines",
    "perf.searchResultsCacheLines",
    "perf.searchThreadPoolSize",
    "perf.useCompressedIndex",
    "perf.useIndexCache",
    "perf.useParallelSearch",
    "perf.useSearchResultsCache",
    "plugins.autoLoad",
    "plugins.enabledPlugins",
    "quickfind.ignore_case",
    "quickfind.incremental",
    "regexpType.autoRunSearch",
    "regexpType.engine",
    "regexpType.main",
    "regexpType.mainBackColor",
    "regexpType.mainHighlight",
    "regexpType.mainHighlightVariate",
    "regexpType.quickfind",
    "regexpType.quickfindBackColor",
    "session.confirmTabClose",
    "session.followOnLoad",
    "session.loadLast",
    "session.multipleWindows",
    "shortcuts",
    "versionchecker.betaEnabled",
    "versionchecker.enabled",
    "view.contextLinesCount",
    "view.fastScrollEnabled",
    "view.fastScrollMultiplier",
    "view.hideAnsiColorSequences",
    "view.language",
    "view.lineNumbersVisibleInFiltered",
    "view.lineNumbersVisibleInMain",
    "view.minimizeToTray",
    "view.overviewVisible",
    "view.qtHiDpi",
    "view.scaleFactorRounding",
    "view.showDashboard",
    "view.showSplashScreen",
    "view.style",
    "view.textWrap",
    "view.toolbarIconSize",
};

void checkSameSettings( const Configuration& expected, const Configuration& actual )
{
    CHECK( QFontInfo( actual.mainFont() ).family() == QFontInfo( expected.mainFont() ).family() );
    CHECK( QFontInfo( actual.mainFont() ).pointSize()
           == QFontInfo( expected.mainFont() ).pointSize() );
    CHECK( actual.forceFontAntialiasing() == expected.forceFontAntialiasing() );
    CHECK( actual.useBoldFont() == expected.useBoldFont() );
    CHECK( actual.language() == expected.language() );
    CHECK( actual.enableQtHighDpi() == expected.enableQtHighDpi() );
    CHECK( actual.scaleFactorRounding() == expected.scaleFactorRounding() );

    CHECK( actual.mainRegexpType() == expected.mainRegexpType() );
    CHECK( actual.quickfindRegexpType() == expected.quickfindRegexpType() );
    CHECK( actual.regexpEngine() == expected.regexpEngine() );
    CHECK( actual.isQuickfindIncremental() == expected.isQuickfindIncremental() );
    CHECK( actual.mainSearchHighlight() == expected.mainSearchHighlight() );
    CHECK( actual.variateMainSearchHighlight() == expected.variateMainSearchHighlight() );
    CHECK( actual.mainSearchBackColor() == expected.mainSearchBackColor() );
    CHECK( actual.qfBackColor() == expected.qfBackColor() );
    CHECK( actual.qfIgnoreCase() == expected.qfIgnoreCase() );
    CHECK( actual.autoRunSearchOnPatternChange() == expected.autoRunSearchOnPatternChange() );

    CHECK( actual.nativeFileWatchEnabled() == expected.nativeFileWatchEnabled() );
    CHECK( actual.pollingEnabled() == expected.pollingEnabled() );
    CHECK( actual.pollIntervalMs() == expected.pollIntervalMs() );
    CHECK( actual.fastModificationDetection() == expected.fastModificationDetection() );
    CHECK( actual.allowFollowOnScroll() == expected.allowFollowOnScroll() );
    CHECK( actual.fastScrollEnabled() == expected.fastScrollEnabled() );
    CHECK( actual.fastScrollMultiplier() == expected.fastScrollMultiplier() );

    CHECK( actual.loadLastSession() == expected.loadLastSession() );
    CHECK( actual.allowMultipleWindows() == expected.allowMultipleWindows() );
    CHECK( actual.followFileOnLoad() == expected.followFileOnLoad() );
    CHECK( actual.confirmTabClose() == expected.confirmTabClose() );

    CHECK( actual.enableLogging() == expected.enableLogging() );
    CHECK( actual.loggingLevel() == expected.loggingLevel() );
    CHECK( actual.versionCheckingEnabled() == expected.versionCheckingEnabled() );
    CHECK( actual.betaVersionCheckingEnabled() == expected.betaVersionCheckingEnabled() );
    CHECK( actual.extractArchives() == expected.extractArchives() );
    CHECK( actual.extractArchivesAlways() == expected.extractArchivesAlways() );

    CHECK( actual.useParallelSearch() == expected.useParallelSearch() );
    CHECK( actual.useSearchResultsCache() == expected.useSearchResultsCache() );
    CHECK( actual.searchResultsCacheLines() == expected.searchResultsCacheLines() );
    CHECK( actual.indexReadBufferSizeMb() == expected.indexReadBufferSizeMb() );
    CHECK( actual.searchReadBufferSizeLines() == expected.searchReadBufferSizeLines() );
    CHECK( actual.searchThreadPoolSize() == expected.searchThreadPoolSize() );
    CHECK( actual.keepFileClosed() == expected.keepFileClosed() );
    CHECK( actual.optimizeForNotLatinEncodings() == expected.optimizeForNotLatinEncodings() );
    CHECK( actual.useCompressedIndex() == expected.useCompressedIndex() );
    CHECK( actual.useIndexCache() == expected.useIndexCache() );
    CHECK( actual.indexCacheMaxSizeMb() == expected.indexCacheMaxSizeMb() );
    CHECK( actual.verifySslPeers() == expected.verifySslPeers() );

    CHECK( actual.isOverviewVisible() == expected.isOverviewVisible() );
    CHECK( actual.mainLineNumbersVisible() == expected.mainLineNumbersVisible() );
    CHECK( actual.filteredLineNumbersVisible() == expected.filteredLineNumbersVisible() );
    CHECK( actual.minimizeToTray() == expected.minimizeToTray() );
    CHECK( actual.contextLinesCount() == expected.contextLinesCount() );
    CHECK( actual.hideAnsiColorSequences() == expected.hideAnsiColorSequences() );
    CHECK( actual.useTextWrap() == expected.useTextWrap() );
    CHECK( actual.style() == expected.style() );

    CHECK( actual.isSearchAutoRefreshDefault() == expected.isSearchAutoRefreshDefault() );
    CHECK( actual.isSearchIgnoreCaseDefault() == expected.isSearchIgnoreCaseDefault() );
    CHECK( actual.isSearchLogicalCombiningDefault() == expected.isSearchLogicalCombiningDefault() );
    CHECK( actual.defaultEncodingMib() == expected.defaultEncodingMib() );
    CHECK( actual.splitterSizes() == expected.splitterSizes() );

    CHECK( actual.shortcuts() == expected.shortcuts() );
    CHECK( actual.showSplashScreen() == expected.showSplashScreen() );
    CHECK( actual.showDashboard() == expected.showDashboard() );
    CHECK( actual.toolbarIconSize() == expected.toolbarIconSize() );
    CHECK( actual.autoDetectLogFormats() == expected.autoDetectLogFormats() );
    CHECK( actual.autoShowTableView() == expected.autoShowTableView() );
    CHECK( actual.pluginsAutoLoad() == expected.pluginsAutoLoad() );
    CHECK( actual.enabledPlugins() == expected.enabledPlugins() );
    CHECK( actual.chartPresets() == expected.chartPresets() );
    CHECK( actual.darkPalette() == expected.darkPalette() );
}

} // namespace

SCENARIO( "Every stored setting survives a save and a load", "[configuration]" )
{
    GIVEN( "A Configuration where every setting holds a value other than its default" )
    {
        const auto config = nonDefaultConfiguration();
        REQUIRE_FALSE( nonDefaultFontFamily().isEmpty() );

        SettingsFile file;
        file.write( config );
        const auto stored = file.values();
        const auto defaults = storedSettings( Configuration{} );

        THEN( "Exactly the known settings are written" )
        {
            auto expectedNames = StoredSettingNames;
            expectedNames.sort();
            CHECK( settingNames( stored ) == expectedNames );
        }

        THEN( "No stored value is the default one" )
        {
            for ( const auto& key : stored.keys() ) {
                if ( settingName( key ) == "dark" ) {
                    continue; // no setter; covered by the settings file test
                }
                INFO( key.toStdString() );
                CHECK( stored.value( key ) != defaults.value( key ) );
            }
        }

        WHEN( "It is loaded into a fresh Configuration" )
        {
            const auto restored = file.load();

            THEN( "Every setting has the saved value" )
            {
                checkSameSettings( config, restored );
            }

            THEN( "Saving it again writes the same values" )
            {
                const auto restoredStored = storedSettings( restored );
                CHECK( restoredStored.keys() == stored.keys() );
                for ( const auto& key : stored.keys() ) {
                    INFO( key.toStdString() );
                    CHECK( restoredStored.value( key ) == stored.value( key ) );
                }
            }
        }
    }
}

SCENARIO( "A settings file written by v26.07.0 loads unchanged", "[configuration]" )
{
    GIVEN( "The settings file of v26.07.0 with every setting changed" )
    {
        const auto file
            = SettingsFile::copyOf( QStringLiteral( LOGSQUIRL_CONFIGURATION_TEST_DATA_DIR )
                                    + "/configuration-v26.07.0.ini" );
        const auto release = file->values();
        REQUIRE_FALSE( release.isEmpty() );

        THEN( "No value in it is the default one" )
        {
            const auto defaults = storedSettings( Configuration{} );
            for ( const auto& key : release.keys() ) {
                // The default depends on the platform.
                if ( key == "filewatch.usePolling" ) {
                    continue;
                }
                INFO( key.toStdString() );
                CHECK( release.value( key ) != defaults.value( key ) );
            }
        }

        WHEN( "It is loaded" )
        {
            const auto config = file->load();

            THEN( "Saving writes back every value unchanged" )
            {
                const auto stored = storedSettings( config );
                CHECK( stored.keys() == release.keys() );
                for ( const auto& key : release.keys() ) {
                    // The stored family is the one the platform resolves.
                    if ( key == "mainFont.family" ) {
                        continue;
                    }
                    INFO( key.toStdString() );
                    CHECK( stored.value( key ) == release.value( key ) );
                }
            }

            THEN( "Values that need a conversion are read correctly" )
            {
                CHECK( config.mainFont().family()
                       == release.value( "mainFont.family" ).toString() );
                CHECK( config.mainFont().pointSize() == 14 );
                CHECK( config.mainRegexpType() == SearchRegexpType::FixedString );
                CHECK( config.quickfindRegexpType() == SearchRegexpType::ExtendedRegexp );
                CHECK( config.regexpEngine() == RegexpEngine::QRegularExpression );
                CHECK( config.mainSearchBackColor() == QColor( 0x12, 0x34, 0x56, 0x80 ) );
                CHECK( config.qfBackColor() == QColor( 0xab, 0xcd, 0xef ) );
                CHECK( config.searchResultsCacheLines() == 500000u );
                CHECK( config.style() == StyleManager::DarkStyleKey );
                CHECK( config.defaultEncodingMib() == 106 );
                CHECK( config.splitterSizes() == QList<int>{ 300, 200 } );
                CHECK( config.enabledPlugins() == QStringList{ "com.example.a", "com.example.b" } );
                CHECK( config.shortcuts().at( FixtureShortcutAction ) == FixtureShortcutKeys );
                CHECK( config.chartPresets().value( "Errors" ) == "[]" );
                CHECK( config.chartPresets().value( "Latency" )
                       == R"({"series": [{"field": "duration", "unit": "ms"}]})" );
                CHECK( config.darkPalette().at( "Window" )
                       == release.value( "dark/Window" ).toString() );
                CHECK( config.darkPalette().at( "Window" )
                       != Configuration{}.darkPalette().at( "Window" ) );
            }
        }
    }
}

SCENARIO( "Settings stored under retired keys are moved to the current keys", "[configuration]" )
{
    GIVEN( "A settings file with the retired file watch and shortcut keys" )
    {
        SettingsFile file;
        file.setValue( "nativeFileWatch.enabled", false );
        file.setValue( "polling.enabled", true );
        file.setValue( "polling.intervalMs", 1234 );
        file.setValue( "shortcuts.mapping", QVariantMap{ { ShortcutAction::LogViewJumpToButtom,
                                                           QStringList{ "F7" } } } );

        WHEN( "It is loaded" )
        {
            const auto config = file.load();

            THEN( "The values are taken from the retired keys" )
            {
                CHECK_FALSE( config.nativeFileWatchEnabled() );
                CHECK( config.pollingEnabled() );
                CHECK( config.pollIntervalMs() == 1234 );
                CHECK( config.shortcuts().at( ShortcutAction::LogViewJumpToBottom )
                       == QStringList{ "F7" } );
            }

            THEN( "The retired keys are removed" )
            {
                const auto values = file.values();
                CHECK_FALSE( values.contains( "nativeFileWatch.enabled" ) );
                CHECK_FALSE( values.contains( "polling.enabled" ) );
                CHECK_FALSE( values.contains( "polling.intervalMs" ) );
                CHECK_FALSE( values.contains( "shortcuts.mapping" ) );
            }
        }
    }

    GIVEN( "A settings file with both a retired and a current key" )
    {
        SettingsFile file;
        file.setValue( "nativeFileWatch.enabled", false );
        file.setValue( "filewatch.useNative", true );

        THEN( "The current key wins" )
        {
            CHECK( file.load().nativeFileWatchEnabled() );
        }
    }

    GIVEN( "A shortcut stored under the misspelled action name" )
    {
        SettingsFile file;
        {
            Configuration config;
            config.setShortcuts( { { ShortcutAction::LogViewJumpToButtom, { "F8" } } } );
            file.write( config );
        }

        THEN( "It is loaded under the corrected name" )
        {
            const auto shortcuts = file.load().shortcuts();
            CHECK( shortcuts.at( ShortcutAction::LogViewJumpToBottom ) == QStringList{ "F8" } );
            CHECK_FALSE( shortcuts.contains( ShortcutAction::LogViewJumpToButtom ) );
        }
    }
}

SCENARIO( "Stored values outside their range are corrected on load", "[configuration]" )
{
    GIVEN( "An index cache size below zero" )
    {
        SettingsFile file;
        file.setValue( "perf.indexCacheMaxSizeMb", -5 );
        THEN( "It is loaded as zero" )
        {
            CHECK( file.load().indexCacheMaxSizeMb() == 0 );
        }
    }

    GIVEN( "An index cache size too large to count in bytes" )
    {
        SettingsFile file;
        file.setValue( "perf.indexCacheMaxSizeMb", 9'000'000 );
        THEN( "It is loaded as the largest size allowed" )
        {
            CHECK( file.load().indexCacheMaxSizeMb() == 8'000'000 );
        }
    }

    GIVEN( "A style that does not exist" )
    {
        SettingsFile file;
        file.setValue( "view.style", "No Such Style" );
        THEN( "The platform's default style is loaded" )
        {
            CHECK( file.load().style() == StyleManager::defaultPlatformStyle() );
        }
    }
}
