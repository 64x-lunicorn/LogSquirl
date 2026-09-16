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

#include <QColor>
#include <QString>

#include "regexpengine.h"
#include "searchregexptype.h"

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
    // Where the Index cache keeps its files. Empty in an underived Policy,
    // and an Index cache given no directory stores nothing.
    QString indexCacheDirectory{};
    // The Index cache keeps nothing for a Log File under this directory.
    // Empty in an underived Policy, which excludes nothing.
    QString indexCacheExcludedDirectory{};

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

    // Whether the Log File is watched at all, by either route. Most
    // consumers do not care which of the two is on, only whether following
    // is possible, so the question is answered here instead of being
    // spelled out as the same OR at every call site. An underived Policy
    // answers false.
    bool anyWatchEnabled() const
    {
        return nativeWatchEnabled || pollingEnabled;
    }

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

// What Format Recognition needs, and nothing else. Whether a recognized
// Log Format is shown as a Table View straight away is a Presentation
// choice, not part of it.
struct RecognitionPolicy {
    bool enabled{};

    bool operator==( const RecognitionPolicy& ) const = default;
};

// What turning the bytes of a Log File into the text of its Log Lines
// needs, beyond the Encoding, and nothing else. It applies to every read:
// the Log Lines a view paints and the ones a Search matches.
struct DecodingPolicy {
    // Whether ANSI color sequences are removed from every Log Line.
    bool hideAnsiColorSequences{};

    bool operator==( const DecodingPolicy& ) const = default;
};

// What coloring a Log Line needs from the settings, and nothing else. The
// one module that builds the Line Decorator's context takes it; neither
// Presentation reads these settings for itself, which is what lets that
// module be exercised without a settings store.
//
// The Highlighter Set, the Color Labels and the QuickFind pattern are not
// here: they are not settings this axis carries but the user's current
// coloring, which reaches the Presentations by their own routes.
struct DecorationPolicy {
    // Whether what the main Search matched is colored in the Log Lines at
    // all. When false, no main-search Highlighter is built.
    bool mainSearchHighlight{};
    // Whether each distinct matched text gets a shade of its own, so that
    // two different matches are told apart.
    bool variateMainSearchHighlight{};
    // The background the main Search's matches are painted in. Invalid in an
    // underived Policy, as is the QuickFind one below.
    QColor mainSearchBackColor{};
    // The background a QuickFind match is painted in.
    QColor quickFindBackColor{};

    bool operator==( const DecorationPolicy& ) const = default;
};

// What a Presentation needs to show and scroll a Log File, and nothing
// else: the settings of the Text View, of the Table View, and of the
// Filtered View drawn like the Text View -- including what is drawn around
// the Log Lines, the line numbers and the overview.
//
// What a Log Line is colored in is not here -- that is the Decoration
// Policy's axis.
struct PresentationPolicy {
    // Whether a Log Line too long for the Viewport is drawn as several
    // Visual Lines instead of being cut off.
    bool useTextWrap{};
    // Whether holding the modifier key multiplies how far a scroll moves.
    bool fastScrollEnabled{};
    // How far it multiplies it. 0 in an underived Policy, which is why
    // fastScrollEnabled is the flag a consumer tests first.
    int fastScrollMultiplier{};
    // Whether scrolling to the end of a Log File may engage follow.
    bool allowFollowOnScroll{};
    // Whether a Log File whose Log Format was recognized opens as a Table
    // View straight away. It rides here rather than with the Recognition
    // Policy because it says what to show, not what to recognize.
    bool autoShowTableView{};
    // Whether the main view draws a line number beside each Log Line.
    bool mainLineNumbersVisible{};
    // Whether a Filtered View does. Its own field, as the View menu toggles
    // the two apart.
    bool filteredLineNumbersVisible{};
    // Whether the overview of matches and marks is shown beside the main
    // view, of the Text View and the Table View alike.
    bool overviewVisible{};

    bool operator==( const PresentationPolicy& ) const = default;
};

// What searching interactively needs, and nothing else: the settings of
// the QuickFind bar, of the mux that dispatches a QuickFind to the widget
// the user is in, and of the Presentations that answer one.
//
// What a match is painted in is not here; that is the Decoration Policy's
// axis. This one is about how the text the user types is read.
struct QuickFindPolicy {
    // How a QuickFind pattern is read. ExtendedRegexp in an underived
    // Policy, that being the first enumerator -- as for the main type below.
    SearchRegexpType quickFindRegexpType{};
    // How a pattern typed into the Search line is read. It rides this axis
    // rather than the Search one because it is a question about the text a
    // widget takes from the user, not about how a Search then runs.
    SearchRegexpType mainRegexpType{};
    // Whether a QuickFind pattern matches regardless of case.
    bool ignoreCase{};
    // Whether QuickFind moves to a match while the pattern is still being
    // typed, rather than only once it is confirmed.
    bool incremental{};
    // Whether changing the Search pattern starts the Search at once. Here
    // for the same reason as the main regexp type: it is about the typing,
    // not about the run.
    bool autoRunSearchOnPatternChange{};

    // The state the search button row of a Log File starts in: whether its
    // Search ignores case, refreshes as the Log File grows, and reads the
    // pattern as a logical combination. Starting state, not live state: they
    // seed the buttons when the Log File's widget is built, and a Policy
    // arriving afterwards does not set a button the user may since have
    // changed by hand.
    bool searchIgnoreCaseDefault{};
    bool searchAutoRefreshDefault{};
    bool searchLogicalCombiningDefault{};

    bool operator==( const QuickFindPolicy& ) const = default;
};

// The Policies as one bundle, so the place that builds the application's
// long-lived objects derives and carries them together.
struct SettingsPolicies {
    IndexingPolicy indexing;
    SearchPolicy search;
    WatchPolicy watch;
    FileAccessPolicy fileAccess;
    RecognitionPolicy recognition;
    DecodingPolicy decoding;
    DecorationPolicy decoration;
    PresentationPolicy presentation;
    QuickFindPolicy quickFind;

    bool operator==( const SettingsPolicies& ) const = default;
};

// Derives all the Policies from a Configuration. Called once, where the
// application's long-lived objects are built -- not wherever a setting
// happens to be needed.
//
// There is no derivation of a single Policy beside it, the Decoration
// Policy's included: that one used to be derived on its own because a
// Presentation had to be colored before the bundle reached it. It no longer
// has to be -- the Session hands a Log File's views the Decoration Policy
// before they are built, as it does every other Policy (#190) -- so a single
// derivation has no caller of its own, and this is the one derivation.
SettingsPolicies deriveSettingsPolicies( const Configuration& config );

#endif
