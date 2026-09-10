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

#include "configuration.h"

SettingsPolicies deriveSettingsPolicies( const Configuration& config )
{
    return SettingsPolicies{
        .indexing = { .readBufferSizeMb = config.indexReadBufferSizeMb(),
                      .useCompressedIndex = config.useCompressedIndex(),
                      .useIndexCache = config.useIndexCache(),
                      .cacheMaxSizeMb = config.indexCacheMaxSizeMb(),
                      .fastModificationDetection = config.fastModificationDetection() },

        .search = { .useParallelSearch = config.useParallelSearch(),
                    .threadPoolSize = config.searchThreadPoolSize(),
                    .readBufferSizeLines = config.searchReadBufferSizeLines(),
                    .useResultsCache = config.useSearchResultsCache(),
                    .resultsCacheLines = config.searchResultsCacheLines(),
                    .regexpEngine = config.regexpEngine() },

        .watch = { .nativeWatchEnabled = config.nativeFileWatchEnabled(),
                   .pollingEnabled = config.pollingEnabled(),
                   .pollIntervalMs = config.pollIntervalMs() },

        .fileAccess = { .keepFileClosed = config.keepFileClosed(),
                        .defaultEncodingMib = config.defaultEncodingMib(),
                        .extractArchives = config.extractArchives(),
                        .extractArchivesAlways = config.extractArchivesAlways() },
    };
}
