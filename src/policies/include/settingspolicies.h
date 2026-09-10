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

#ifndef LOGSQUIRL_SETTINGS_POLICIES_H
#define LOGSQUIRL_SETTINGS_POLICIES_H

#include "regexpengine.h"

class Configuration;

// The Settings Policies: the small set of settings one part of the
// application actually needs, taken as a snapshot and handed to it when it
// is built. A part that holds a Policy cannot reach for a setting it did
// not declare -- which is the whole point, and why each type below names
// its consumer's settings and nothing else.
//
// They are plain aggregates a test can build from literals, with no
// settings store, no persistable bootstrap and no ambient accessor.
//
// Every member is value-initialised and nothing more. The shipped defaults
// live in Configuration and nowhere else, so there is no second copy here
// to drift out of step with the settings store -- and some of those
// defaults are platform-dependent, which a copy would get wrong. A Policy
// that says false/0 throughout is therefore visibly not a configured one;
// the values that matter always arrive via deriveSettingsPolicies().
//
// The types live in their own header-only library, apart from the settings
// store: a library that consumes a Policy links logsquirl_policies and not
// logsquirl_settings, so reaching for an ambient setting from inside it is
// a link error rather than something a reviewer has to catch. Only the
// place that derives the Policies -- and deriveSettingsPolicies() below,
// which is defined in logsquirl_settings -- needs the store itself.

// What indexing a Log File needs, and nothing else.
struct IndexingPolicy {
    int readBufferSizeMb{};
    bool useCompressedIndex{};
    bool useIndexCache{};
    int cacheMaxSizeMb{};
    bool fastModificationDetection{};

    // Compared so that a settings change can be applied per axis: only the
    // consumers of an axis that actually changed are disturbed.
    bool operator==( const IndexingPolicy& ) const = default;
};

// What running a Search needs, and nothing else.
struct SearchPolicy {
    bool useParallelSearch{};
    // 0 means "as many threads as the hardware reports".
    int threadPoolSize{};
    int readBufferSizeLines{};
    bool useResultsCache{};
    unsigned resultsCacheLines{};
    RegexpEngine regexpEngine{};
    // How far around a Match or Mark the Context Lines reach. Part of the
    // Search axis because the Search Session owns Context Lines: they are
    // rebuilt from the matches a run produced.
    int contextLinesCount{};

    bool operator==( const SearchPolicy& ) const = default;
};

// What following a Log File on disk needs, and nothing else.
struct WatchPolicy {
    bool nativeWatchEnabled{};
    bool pollingEnabled{};
    int pollIntervalMs{};

    bool operator==( const WatchPolicy& ) const = default;
};

// What opening and reading a Log File needs, and nothing else.
struct FileAccessPolicy {
    bool keepFileClosed{};
    // Negative means "detect the Encoding rather than force one".
    int defaultEncodingMib{};
    bool extractArchives{};
    bool extractArchivesAlways{};

    bool operator==( const FileAccessPolicy& ) const = default;
};

// The four Policies as one bundle, so the place that builds the
// application's long-lived objects derives and carries them together.
struct SettingsPolicies {
    IndexingPolicy indexing;
    SearchPolicy search;
    WatchPolicy watch;
    FileAccessPolicy fileAccess;

    bool operator==( const SettingsPolicies& ) const = default;
};

// Derives all four Policies from a Configuration. Called once, where the
// application's long-lived objects are built -- not wherever a setting
// happens to be needed.
SettingsPolicies deriveSettingsPolicies( const Configuration& config );

#endif
