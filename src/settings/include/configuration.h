/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2015 Nicolas Bonnefon and other
 * contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_CONFIGURATION_H
#define LOGSQUIRL_CONFIGURATION_H

#include <QColor>
#include <QFont>
#include <QSettings>
#include <qcolor.h>
#include <string>
#include <string_view>

#include "persistable.h"

// Type of regexp to use for searches
enum class SearchRegexpType {
    ExtendedRegexp,
    Wildcard,
    FixedString,
};

enum class RegexpEngine { Vectorscan, QRegularExpression };
static constexpr int MAX_RECENT_FILES = 25;

// Configuration class containing everything in the "Settings" dialog
class Configuration final : public Persistable<Configuration> {
  public:
    static const char* persistableName()
    {
        return "Configuration";
    }
    Configuration();

    // Accesses the main font used for display
    QFont mainFont() const;
    void setMainFont( QFont newFont );

    QString language() const
    {
        return language_;
    }

    void setLanguage( QString lang )
    {
        language_ = lang;
    }

    // Accesses the regexp types
    SearchRegexpType mainRegexpType() const
    {
        return mainRegexpType_;
    }
    SearchRegexpType quickfindRegexpType() const
    {
        return quickfindRegexpType_;
    }
    bool isQuickfindIncremental() const
    {
        return quickfindIncremental_;
    }
    void setMainRegexpType( SearchRegexpType type )
    {
        mainRegexpType_ = type;
    }
    void setQuickfindRegexpType( SearchRegexpType type )
    {
        quickfindRegexpType_ = type;
    }
    void setQuickfindIncremental( bool isIncremental )
    {
        quickfindIncremental_ = isIncremental;
    }

    // "Advanced" settings
    bool anyFileWatchEnabled() const
    {
        return nativeFileWatchEnabled() || pollingEnabled();
    }

    bool nativeFileWatchEnabled() const
    {
        return nativeFileWatchEnabled_;
    }
    void setNativeFileWatchEnabled( bool enabled )
    {
        nativeFileWatchEnabled_ = enabled;
    }
    bool pollingEnabled() const
    {
        return pollingEnabled_;
    }
    void setPollingEnabled( bool enabled )
    {
        pollingEnabled_ = enabled;
    }
    int pollIntervalMs() const
    {
        return pollIntervalMs_;
    }
    void setPollIntervalMs( int interval )
    {
        pollIntervalMs_ = interval;
    }

    bool fastModificationDetection() const
    {
        return fastModificationDetection_;
    }

    void setFastModificationDetection( bool fastDetection )
    {
        fastModificationDetection_ = fastDetection;
    }

    bool loadLastSession() const
    {
        return loadLastSession_;
    }
    void setLoadLastSession( bool enabled )
    {
        loadLastSession_ = enabled;
    }
    bool followFileOnLoad() const
    {
        return followFileOnLoad_;
    }
    void setFollowFileOnLoad( bool enabled )
    {
        followFileOnLoad_ = enabled;
    }
    bool allowMultipleWindows() const
    {
        return allowMultipleWindows_;
    }
    void setAllowMultipleWindows( bool enabled )
    {
        allowMultipleWindows_ = enabled;
    }

    bool confirmTabClose() const
    {
        return confirmTabClose_;
    }
    void setConfirmTabClose( bool enabled )
    {
        confirmTabClose_ = enabled;
    }

    // perf settings
    bool useParallelSearch() const
    {
        return useParallelSearch_;
    }
    void setUseParallelSearch( bool enabled )
    {
        useParallelSearch_ = enabled;
    }
    bool useSearchResultsCache() const
    {
        return useSearchResultsCache_;
    }
    void setUseSearchResultsCache( bool enabled )
    {
        useSearchResultsCache_ = enabled;
    }
    unsigned searchResultsCacheLines() const
    {
        return searchResultsCacheLines_;
    }
    void setSearchResultsCacheLines( unsigned lines )
    {
        searchResultsCacheLines_ = lines;
    }
    int indexReadBufferSizeMb() const
    {
        return indexReadBufferSizeMb_;
    }
    void setIndexReadBufferSizeMb( int bufferSizeMb )
    {
        indexReadBufferSizeMb_ = bufferSizeMb;
    }
    int searchReadBufferSizeLines() const
    {
        return searchReadBufferSizeLines_;
    }
    void setSearchReadBufferSizeLines( int lines )
    {
        searchReadBufferSizeLines_ = lines;
    }
    int searchThreadPoolSize() const
    {
        return searchThreadPoolSize_;
    }
    void setSearchThreadPoolSize( int threads )
    {
        searchThreadPoolSize_ = threads;
    }
    bool keepFileClosed() const
    {
        return keepFileClosed_;
    }
    void setKeepFileClosed( bool shouldKeepClosed )
    {
        keepFileClosed_ = shouldKeepClosed;
    }
    bool useCompressedIndex() const
    {
        return useCompressedIndex_;
    }
    void setUseCompressedIndex( bool useCompressedIndex )
    {
        useCompressedIndex_ = useCompressedIndex;
    }

    bool useIndexCache() const
    {
        return useIndexCache_;
    }
    void setUseIndexCache( bool useCache )
    {
        useIndexCache_ = useCache;
    }

    int indexCacheMaxSizeMb() const
    {
        return indexCacheMaxSizeMb_;
    }
    void setIndexCacheMaxSizeMb( int sizeMb )
    {
        indexCacheMaxSizeMb_ = sizeMb;
    }

    RegexpEngine regexpEngine() const
    {
        return regexpEngine_;
    }

    void setRegexpEngine( RegexpEngine engine )
    {
        regexpEngine_ = engine;
    }

    // Accessors
    bool versionCheckingEnabled() const
    {
        return enableVersionChecking_;
    }
    void setVersionCheckingEnabled( bool enabled )
    {
        enableVersionChecking_ = enabled;
    }

    // Whether the user opted in to beta update notifications
    bool betaVersionCheckingEnabled() const
    {
        return enableBetaVersionChecking_;
    }
    void setBetaVersionCheckingEnabled( bool enabled )
    {
        enableBetaVersionChecking_ = enabled;
    }

    // View settings
    bool isOverviewVisible() const
    {
        return overviewVisible_;
    }
    void setOverviewVisible( bool isVisible )
    {
        overviewVisible_ = isVisible;
    }
    bool mainLineNumbersVisible() const
    {
        return lineNumbersVisibleInMain_;
    }
    bool filteredLineNumbersVisible() const
    {
        return lineNumbersVisibleInFiltered_;
    }
    bool minimizeToTray() const
    {
        return minimizeToTray_;
    }
    QString style() const
    {
        return style_;
    }
    void setMainLineNumbersVisible( bool lineNumbersVisible )
    {
        lineNumbersVisibleInMain_ = lineNumbersVisible;
    }
    void setFilteredLineNumbersVisible( bool lineNumbersVisible )
    {
        lineNumbersVisibleInFiltered_ = lineNumbersVisible;
    }
    void setMinimizeToTray( bool minimizeToTray )
    {
        minimizeToTray_ = minimizeToTray;
    }

    // Number of context lines shown around matches in the filtered view (0 = off).
    int contextLinesCount() const
    {
        return contextLinesCount_;
    }
    void setContextLinesCount( int count )
    {
        contextLinesCount_ = count;
    }

    void setStyle( const QString& style )
    {
        style_ = style;
    }

    bool enableLogging() const
    {
        return enableLogging_;
    }
    int loggingLevel() const
    {
        return loggingLevel_;
    }

    void setEnableLogging( bool enableLogging )
    {
        enableLogging_ = enableLogging;
    }
    void setLoggingLevel( int level )
    {
        loggingLevel_ = level;
    }

    // Default settings for new views
    bool isSearchAutoRefreshDefault() const
    {
        return searchAutoRefresh_;
    }
    void setSearchAutoRefreshDefault( bool autoRefresh )
    {
        searchAutoRefresh_ = autoRefresh;
    }
    bool isSearchIgnoreCaseDefault() const
    {
        return searchIgnoreCase_;
    }
    void setSearchIgnoreCaseDefault( bool ignoreCase )
    {
        searchIgnoreCase_ = ignoreCase;
    }
    bool isSearchLogicalCombiningDefault() const
    {
        return searchLogicalCombining_;
    }
    void setSearchLogicalCombiningDefault( bool logicalCombining )
    {
        searchLogicalCombining_ = logicalCombining;
    }
    QList<int> splitterSizes() const
    {
        return splitterSizes_;
    }
    void setSplitterSizes( QList<int> sizes )
    {
        splitterSizes_ = std::move( sizes );
    }

    bool extractArchives() const
    {
        return extractArchives_;
    }
    void setExtractArchives( bool extract )
    {
        extractArchives_ = extract;
    }

    bool extractArchivesAlways() const
    {
        return extractArchivesAlways_;
    }
    void setExtractArchivesAlways( bool extract )
    {
        extractArchivesAlways_ = extract;
    }

    bool verifySslPeers() const
    {
        return verifySslPeers_;
    }
    void setVerifySslPeers( bool verify )
    {
        verifySslPeers_ = verify;
    }

    bool forceFontAntialiasing() const
    {
        return forceFontAntialiasing_;
    }
    void setForceFontAntialiasing( bool force )
    {
        forceFontAntialiasing_ = force;
    }

    bool useBoldFont() const
    {
        return useBoldFont_;
    }
    void setUseBoldFont( bool bold )
    {
        useBoldFont_ = bold;
    }

    bool enableQtHighDpi() const
    {
        return enableQtHighDpi_;
    }
    void setEnableQtHighDpi( bool enable )
    {
        enableQtHighDpi_ = enable;
    }

    int scaleFactorRounding() const
    {
        return scaleFactorRounding_;
    }
    void setScaleFactorRounding( int rounding )
    {
        scaleFactorRounding_ = rounding;
    }

    bool mainSearchHighlight() const
    {
        return enableMainSearchHighlight_;
    }
    void setEnableMainSearchHighlight( bool enable )
    {
        enableMainSearchHighlight_ = enable;
    }

    bool variateMainSearchHighlight() const
    {
        return enableMainSearchHighlightVariance_;
    }
    void setVariateMainSearchHighlight( bool enable )
    {
        enableMainSearchHighlightVariance_ = enable;
    }

    QColor mainSearchBackColor() const
    {
        return mainSearchBackColor_;
    }
    void setMainSearchBackColor( QColor color )
    {
        mainSearchBackColor_ = color;
    }

    QColor qfBackColor() const
    {
        return qfBackColor_;
    }
    void setQfBackColor( QColor color )
    {
        qfBackColor_ = color;
    }

    bool qfIgnoreCase() const
    {
        return qfIgnoreCase_;
    }
    void setQfIgnoreCase( bool ignore )
    {
        qfIgnoreCase_ = ignore;
    }

    std::map<std::string, QStringList> shortcuts() const
    {
        return shortcuts_;
    }
    void setShortcuts( const std::map<std::string, QStringList>& shortcuts )
    {
        shortcuts_ = shortcuts;
    }

    bool allowFollowOnScroll() const
    {
        return allowFollowOnScroll_;
    }
    void setAllowFollowOnScroll( bool enable )
    {
        allowFollowOnScroll_ = enable;
    }

    // Fast scroll: multiply scroll speed when Alt (Option) is held
    bool fastScrollEnabled() const
    {
        return fastScrollEnabled_;
    }
    void setFastScrollEnabled( bool enable )
    {
        fastScrollEnabled_ = enable;
    }
    int fastScrollMultiplier() const
    {
        return fastScrollMultiplier_;
    }
    void setFastScrollMultiplier( int multiplier )
    {
        fastScrollMultiplier_ = multiplier;
    }

    bool useTextWrap() const
    {
        return useTextWrap_;
    }
    void setUseTextWrap( bool enable )
    {
        useTextWrap_ = enable;
    }

    bool autoRunSearchOnPatternChange() const
    {
        return autoRunSearchOnPatternChange_;
    }
    void setAutoRunSearchOnPatternChange( bool enable )
    {
        autoRunSearchOnPatternChange_ = enable;
    }

    bool optimizeForNotLatinEncodings() const
    {
        return optimizeForNotLatinEncodings_;
    }
    void setOptimizeForNotLatinEncodings( bool enable )
    {
        optimizeForNotLatinEncodings_ = enable;
    }

    bool hideAnsiColorSequences() const
    {
        return hideAnsiColorSequences_;
    }
    void setHideAnsiColorSequences( bool hide )
    {
        hideAnsiColorSequences_ = hide;
    }

    int defaultEncodingMib() const
    {
        return defaultEncodingMib_;
    }
    void setDefaultEncodingMib( int mib )
    {
        defaultEncodingMib_ = mib;
    }

    std::map<QString, QString> darkPalette() const {
        return darkPalette_;
    }

    // Splash screen
    bool showSplashScreen() const
    {
        return showSplashScreen_;
    }
    void setShowSplashScreen( bool show )
    {
        showSplashScreen_ = show;
    }

    // Dashboard on startup
    bool showDashboard() const
    {
        return showDashboard_;
    }
    void setShowDashboard( bool show )
    {
        showDashboard_ = show;
    }

    // Toolbar icon size in pixels (default 24)
    int toolbarIconSize() const
    {
        return toolbarIconSize_;
    }
    void setToolbarIconSize( int size )
    {
        toolbarIconSize_ = size;
    }

    // Auto-detect log format (lnav-compatible) — disabled by default
    bool autoDetectLogFormats() const
    {
        return autoDetectLogFormats_;
    }
    void setAutoDetectLogFormats( bool enabled )
    {
        autoDetectLogFormats_ = enabled;
    }

    // Automatically switch to table view when a format is detected
    bool autoShowTableView() const
    {
        return autoShowTableView_;
    }
    void setAutoShowTableView( bool enabled )
    {
        autoShowTableView_ = enabled;
    }

    // Plugin settings
    bool pluginsAutoLoad() const
    {
        return pluginsAutoLoad_;
    }
    void setPluginsAutoLoad( bool enabled )
    {
        pluginsAutoLoad_ = enabled;
    }
    QStringList enabledPlugins() const
    {
        return enabledPlugins_;
    }
    void setEnabledPlugins( const QStringList& plugins )
    {
        enabledPlugins_ = plugins;
    }

    // Chart presets — app-level named chart configurations
    QMap<QString, QString> chartPresets() const
    {
        return chartPresets_;
    }
    void setChartPreset( const QString& name, const QString& jsonDefs )
    {
        chartPresets_[ name ] = jsonDefs;
    }
    void removeChartPreset( const QString& name )
    {
        chartPresets_.remove( name );
    }

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

  private:
    // Configuration settings
    mutable QFont mainFont_ = { "DejaVu Sans Mono", 10 };
    SearchRegexpType mainRegexpType_ = SearchRegexpType::ExtendedRegexp;
    SearchRegexpType quickfindRegexpType_ = SearchRegexpType::FixedString;
    bool quickfindIncremental_ = true;

    QString language_{ "en" };

    bool nativeFileWatchEnabled_ = true;
#ifdef Q_OS_WIN
    bool pollingEnabled_ = true;
#else
    bool pollingEnabled_ = false;
#endif

    int pollIntervalMs_ = 2000;

    bool fastModificationDetection_ = false;

    bool loadLastSession_ = true;
    bool followFileOnLoad_ = false;
    bool allowMultipleWindows_ = false;
    bool confirmTabClose_ = true;

    // View settings
    bool overviewVisible_ = true;
    bool lineNumbersVisibleInMain_ = false;
    bool lineNumbersVisibleInFiltered_ = true;
    bool minimizeToTray_ = false;
    int contextLinesCount_ = 5;
    QString style_;

    // Default settings for new views
    bool searchAutoRefresh_ = false;
    bool searchIgnoreCase_ = false;
    bool searchLogicalCombining_ = false;
    QList<int> splitterSizes_;

    // Performance settings
    bool useSearchResultsCache_ = true;
    unsigned searchResultsCacheLines_ = 1000000;
    bool useParallelSearch_ = true;
    int indexReadBufferSizeMb_ = 16;
    int searchReadBufferSizeLines_ = 10000;
    int searchThreadPoolSize_ = 0;
    bool keepFileClosed_ = false;
    bool useCompressedIndex_ = true;
    bool useIndexCache_ = false;
    int indexCacheMaxSizeMb_ = 500;

    bool enableLogging_ = false;
    int loggingLevel_ = 4;

    bool enableVersionChecking_ = true;
    bool enableBetaVersionChecking_ = false;

    bool extractArchives_ = true;
    bool extractArchivesAlways_ = false;

    bool verifySslPeers_ = true;

    bool forceFontAntialiasing_ = false;
    bool enableQtHighDpi_ = true;
    bool useBoldFont_ = false;

    int scaleFactorRounding_ = 1;

    RegexpEngine regexpEngine_ = RegexpEngine::Vectorscan;

    QColor qfBackColor_ = Qt::yellow;
    QColor mainSearchBackColor_ = Qt::lightGray;
    bool enableMainSearchHighlight_ = false;
    bool enableMainSearchHighlightVariance_ = false;

    bool allowFollowOnScroll_ = true;
    bool autoRunSearchOnPatternChange_ = false;

    bool fastScrollEnabled_ = true;
    int fastScrollMultiplier_ = 5;

    bool optimizeForNotLatinEncodings_ = false;

    bool hideAnsiColorSequences_ = false;

    int defaultEncodingMib_ = -1;

    bool showSplashScreen_ = false;

    bool showDashboard_ = true;

    int toolbarIconSize_ = 24;

    bool autoDetectLogFormats_ = false;
    bool autoShowTableView_ = false;

    bool pluginsAutoLoad_ = true;
    QStringList enabledPlugins_;

    bool qfIgnoreCase_ = false;

    bool useTextWrap_ = false;

    std::map<std::string, QStringList> shortcuts_;

    QMap<QString, QString> chartPresets_;

    // based on https://gist.github.com/QuantumCD/6245215
    std::map<QString, QString> darkPalette_ = {
        {"Window", "#121212"},
        {"WindowText", "#E0E0E0"},
        {"Base", "#1E1E1E"},
        {"AlternateBase", "#252526"},
        {"ToolTipBase", "#2D2D30"},
        {"ToolTipText", "#E0E0E0"},
        {"Text", "#E0E0E0"},
        {"Button", "#2D2D30"},
        {"ButtonText", "#E0E0E0"},
        {"Link", "#4D90FE"},
        {"Highlight", "#4D90FE"},
        {"HighlightedText", "#FFFFFF"},
        {"ActiveButton", "#252526"},
        {"DisabledButtonText", "#666666"},
        {"DisabledWindowText", "#666666"},
        {"DisabledText", "#666666"},
        {"DisabledLight", "#252526"},
    };
};

#endif
