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

#include "settingspolicies.h"

#include <QDir>

#include "configuration.h"
#include "log.h"
#include "textencoding.h"

namespace {

// The default Encoding a File Access Policy carries: none, or one this build
// knows. One it does not -- a MIB another build or version wrote -- is the
// locale's, decided here and nowhere else, so everything that reads a Log
// File under the Policy takes the MIB as it is (#552). The desktop resets
// such a setting to Auto as soon as it is loaded (#488); the grep CLI, which
// never writes the settings, reads in the locale's Encoding.
int knownDefaultEncodingMib( int mib )
{
    if ( mib < 0 || TextEncoding::forMib( mib ) != nullptr ) {
        return mib;
    }

    const auto* locale = TextEncoding::forLocale();
    LOG_WARNING << "Unknown default encoding MIB " << mib << " in the settings, reading in "
                << locale->name().constData();
    return locale->mibEnum();
}

} // namespace

SettingsPolicies deriveSettingsPolicies( const Configuration& config )
{
    return SettingsPolicies{
        .indexing = { .readBufferSizeMb = config.indexReadBufferSizeMb(),
                      .useCompressedIndex = config.useCompressedIndex(),
                      .useIndexCache = config.useIndexCache(),
                      .cacheMaxSizeMb = config.indexCacheMaxSizeMb(),
                      .fastModificationDetection = config.fastModificationDetection(),
                      .indexCacheDirectory = config.indexCacheDirectory(),
                      // A temporary file is not worth an Index kept across
                      // sessions: it is rarely opened again, and its path is
                      // soon reused by an unrelated file.
                      .indexCacheExcludedDirectory = QDir::tempPath() },

        .search = { .useParallelSearch = config.useParallelSearch(),
                    .threadPoolSize = config.searchThreadPoolSize(),
                    .readBufferSizeLines = config.searchReadBufferSizeLines(),
                    .useResultsCache = config.useSearchResultsCache(),
                    .resultsCacheLines = config.searchResultsCacheLines(),
                    .regexpEngine = config.regexpEngine(),
                    .contextLinesCount = config.contextLinesCount() },

        .watch = { .nativeWatchEnabled = config.nativeFileWatchEnabled(),
                   .pollingEnabled = config.pollingEnabled(),
                   .pollIntervalMs = config.pollIntervalMs() },

        .fileAccess
        = { .keepFileClosed = config.keepFileClosed(),
            .defaultEncodingMib = knownDefaultEncodingMib( config.defaultEncodingMib() ),
            .extractArchives = config.extractArchives(),
            .extractArchivesAlways = config.extractArchivesAlways() },

        .recognition = { .enabled = config.autoDetectLogFormats() },

        // Show colors hides the sequences as Hide does: the two differ only
        // in the Decoration Policy, so switching between them re-reads no Log
        // File and re-runs no Search (#573).
        .decoding = { .hideAnsiColorSequences
                      = config.ansiColorSequences() != AnsiColorSequences::ShowAsText },

        .decoration
        = { .mainSearchHighlight = config.mainSearchHighlight(),
            .variateMainSearchHighlight = config.variateMainSearchHighlight(),
            .mainSearchBackColor = config.mainSearchBackColor(),
            .quickFindBackColor = config.qfBackColor(),
            .showAnsiColors = config.ansiColorSequences() == AnsiColorSequences::ShowColors },

        .presentation = { .useTextWrap = config.useTextWrap(),
                          .fastScrollEnabled = config.fastScrollEnabled(),
                          .fastScrollMultiplier = config.fastScrollMultiplier(),
                          .allowFollowOnScroll = config.allowFollowOnScroll(),
                          .autoShowTableView = config.autoShowTableView(),
                          .mainLineNumbersVisible = config.mainLineNumbersVisible(),
                          .filteredLineNumbersVisible = config.filteredLineNumbersVisible(),
                          .overviewVisible = config.isOverviewVisible(),
                          .showValueNames = config.showValueNames() },

        .quickFind = { .quickFindRegexpType = config.quickfindRegexpType(),
                       .mainRegexpType = config.mainRegexpType(),
                       .ignoreCase = config.qfIgnoreCase(),
                       .incremental = config.isQuickfindIncremental(),
                       .autoRunSearchOnPatternChange = config.autoRunSearchOnPatternChange(),
                       .searchIgnoreCaseDefault = config.isSearchIgnoreCaseDefault(),
                       .searchAutoRefreshDefault = config.isSearchAutoRefreshDefault(),
                       .searchLogicalCombiningDefault = config.isSearchLogicalCombiningDefault() },

        .teamFolder = { .enabled = config.teamFolderEnabled(),
                        .repositoryUrl = config.teamFolderUrl(),
                        .subfolder = config.teamFolderSubfolder() },
    };
}
