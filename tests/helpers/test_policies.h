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

#ifndef TEST_POLICIES_H
#define TEST_POLICIES_H

#include "settingspolicies.h"

// Settings Policies for tests, written as literals.
//
// A Policy carries no defaults of its own -- the shipped ones live in
// Configuration -- so a test that builds one has to say what it wants. The
// values below mirror the shipped defaults so that a test which does not
// care about settings behaves as the application does, and a test which
// does care overrides the one field it is about:
//
//     auto policies = testSettingsPolicies();
//     policies.indexing.useCompressedIndex = false;
//
// This is a test fixture, not a second source of truth: nothing in the
// application reads it, and a test that depends on an exact value states
// that value itself.
inline SettingsPolicies testSettingsPolicies()
{
    return SettingsPolicies{
        .indexing = { .readBufferSizeMb = 16,
                      .useCompressedIndex = true,
                      .useIndexCache = false,
                      .cacheMaxSizeMb = 500,
                      .fastModificationDetection = false },

        .search = { .useParallelSearch = true,
                    .threadPoolSize = 0,
                    .readBufferSizeLines = 10000,
                    .useResultsCache = true,
                    .resultsCacheLines = 1000000u,
                    .regexpEngine = RegexpEngine::Vectorscan,
                    .contextLinesCount = 5 },

        // Polling as well as native watching, at a far shorter interval
        // than the shipped one: a test that appends to a file should not
        // have to wait two seconds, nor depend on the platform having
        // working native notifications.
        .watch = { .nativeWatchEnabled = true, .pollingEnabled = true, .pollIntervalMs = 100 },

        .fileAccess = { .keepFileClosed = false,
                        .defaultEncodingMib = -1,
                        .extractArchives = true,
                        .extractArchivesAlways = false },
    };
}

#endif // TEST_POLICIES_H
