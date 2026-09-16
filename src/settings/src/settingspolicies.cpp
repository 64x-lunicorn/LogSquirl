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

        .fileAccess = { .keepFileClosed = config.keepFileClosed(),
                        .defaultEncodingMib = config.defaultEncodingMib(),
                        .extractArchives = config.extractArchives(),
                        .extractArchivesAlways = config.extractArchivesAlways() },

        .recognition = { .enabled = config.autoDetectLogFormats() },

        .decoding = { .hideAnsiColorSequences = config.hideAnsiColorSequences() },

        .decoration = deriveDecorationPolicy( config ),

        .presentation = { .useTextWrap = config.useTextWrap(),
                          .fastScrollEnabled = config.fastScrollEnabled(),
                          .fastScrollMultiplier = config.fastScrollMultiplier(),
                          .allowFollowOnScroll = config.allowFollowOnScroll(),
                          .autoShowTableView = config.autoShowTableView() },

        .quickFind = { .quickFindRegexpType = config.quickfindRegexpType(),
                       .mainRegexpType = config.mainRegexpType(),
                       .ignoreCase = config.qfIgnoreCase(),
                       .incremental = config.isQuickfindIncremental(),
                       .autoRunSearchOnPatternChange = config.autoRunSearchOnPatternChange() },
    };
}

DecorationPolicy deriveDecorationPolicy( const Configuration& config )
{
    return DecorationPolicy{
        .mainSearchHighlight = config.mainSearchHighlight(),
        .variateMainSearchHighlight = config.variateMainSearchHighlight(),
        .mainSearchBackColor = config.mainSearchBackColor(),
        .quickFindBackColor = config.qfBackColor(),
    };
}
