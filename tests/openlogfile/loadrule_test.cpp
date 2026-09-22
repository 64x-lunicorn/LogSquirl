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

#include "loadrule.h"
#include "searchautorefresh.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch.hpp>

// The Load Rule (#396): what a load, a change on disk and a reload mean for an
// Open Log File. Each row is a sequence of events and what the rule decides at
// its end; no Log File, thread or event loop is involved.

namespace {

using SearchRefresh = LoadRule::SearchRefresh;
using Change = LoadRule::Change;
using AutoRefreshState = SearchAutoRefresh::State;

// The events an Open Log File hands the Load Rule.
struct SavedMarks {
    std::vector<uint64_t> marks;
};
struct Reload {};
struct ChangedOnDisk {
    MonitoredFileStatus status;
};
struct SearchRequested {};
struct SearchCleared {};
struct WaitingSearchDropped {};
struct LoadFinished {
    LoadingStatus status = LoadingStatus::Successful;
    uint64_t lines = 30;
    // Where the Search's auto-refresh stands when the load finishes.
    AutoRefreshState search = AutoRefreshState::NoSearch;
};

using Event = std::variant<SavedMarks, Reload, ChangedOnDisk, SearchRequested, SearchCleared,
                           WaitingSearchDropped, LoadFinished>;

const auto grew = ChangedOnDisk{ MonitoredFileStatus::DataAdded };
const auto truncated = ChangedOnDisk{ MonitoredFileStatus::Truncated };
const auto unchanged = ChangedOnDisk{ MonitoredFileStatus::Unchanged };
const auto loaded = LoadFinished{};
const auto failedToLoad = LoadFinished{ LoadingStatus::Failed, 0 };

// An auto-refresh in the given state, reached the way an Open Log File
// reaches it.
SearchAutoRefresh autoRefreshIn( AutoRefreshState state )
{
    SearchAutoRefresh autoRefresh;
    switch ( state ) {
    case AutoRefreshState::NoSearch:
        break;
    case AutoRefreshState::Static:
        autoRefresh.startSearch();
        break;
    case AutoRefreshState::Autorefreshing:
        autoRefresh.setAutoRefresh( true );
        autoRefresh.startSearch();
        break;
    case AutoRefreshState::FileTruncated:
        autoRefresh.startSearch();
        autoRefresh.truncateFile();
        break;
    case AutoRefreshState::TruncatedAutorefreshing:
        autoRefresh.setAutoRefresh( true );
        autoRefresh.startSearch();
        autoRefresh.truncateFile();
        break;
    }
    REQUIRE( autoRefresh.state() == state );
    return autoRefresh;
}

std::vector<uint64_t> numbers( const logsquirl::vector<LineNumber>& lines )
{
    std::vector<uint64_t> result;
    for ( const auto& line : lines ) {
        result.push_back( line.get() );
    }
    return result;
}

// What the rule decided along the way: the last decision of each kind.
struct Decisions {
    LoadRule::LoadDecision load;
    LoadRule::ChangeDecision change;
    LoadRule::ReloadDecision reload;
    bool searchWaits = false;
};

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

Decisions run( LoadRule& rule, const std::vector<Event>& events )
{
    Decisions decisions;
    for ( const auto& event : events ) {
        std::visit(
            Overloaded{
                [ & ]( const SavedMarks& saved ) {
                    logsquirl::vector<LineNumber> marks;
                    for ( const auto mark : saved.marks ) {
                        marks.emplace_back( mark );
                    }
                    rule.restoreMarks( marks );
                },
                [ & ]( const Reload& ) { decisions.reload = rule.reload(); },
                [ & ]( const ChangedOnDisk& change ) {
                    decisions.change = rule.changedOnDisk( change.status );
                },
                [ & ]( const SearchRequested& ) { decisions.searchWaits = rule.searchRequested(); },
                [ & ]( const SearchCleared& ) { rule.searchCleared(); },
                [ & ]( const WaitingSearchDropped& ) { rule.waitingSearchDropped(); },
                [ & ]( const LoadFinished& load ) {
                    decisions.load = rule.loadFinished( load.status, LinesCount( load.lines ),
                                                        autoRefreshIn( load.search ) );
                },
            },
            event );
    }
    return decisions;
}

struct LoadRow {
    std::string sequence;
    std::vector<Event> events; // the last one is the load whose decision is checked
    bool fromStart;
    bool onlyAppended;
    SearchRefresh searchRefresh;
    std::vector<uint64_t> savedMarksToApply;
    bool runWaitingSearch;
    bool recognizeFormat;
};

struct ChangeRow {
    std::string sequence;
    std::vector<Event> events; // the last one is the change whose decision is checked
    Change report;
    bool clearMarks;
    bool dropSearch;
    bool forgetLogFormat;
};

struct SearchRow {
    std::string sequence;
    std::vector<Event> events; // the Search is requested after them
    bool waits;
};

} // namespace

SCENARIO( "The Load Rule decides what a finished load brings", "[openlogfile][loadrule]" )
{
    const auto autoRefreshing
        = LoadFinished{ LoadingStatus::Successful, 40, AutoRefreshState::Autorefreshing };
    const auto staticSearch
        = LoadFinished{ LoadingStatus::Successful, 40, AutoRefreshState::Static };
    const auto truncatedUnderAutoRefresh
        = LoadFinished{ LoadingStatus::Successful, 10, AutoRefreshState::TruncatedAutorefreshing };
    const auto truncatedUnderStaticSearch
        = LoadFinished{ LoadingStatus::Successful, 10, AutoRefreshState::FileTruncated };
    const auto noLines = LoadFinished{ LoadingStatus::Successful, 0 };

    // clang-format off
    const std::vector<LoadRow> rows = {
        // sequence, events,
        //   fromStart, onlyAppended, searchRefresh, savedMarksToApply, runWaitingSearch, recognizeFormat
        { "first load", { loaded },
          true, false, SearchRefresh::None, {}, false, true },
        { "first load with Marks saved with the Session", { SavedMarks{ { 3, 7 } }, loaded },
          true, false, SearchRefresh::None, { 3, 7 }, false, true },
        { "grew, then loaded, under an auto-refreshing Search", { loaded, grew, autoRefreshing },
          false, true, SearchRefresh::Continue, {}, false, false },
        { "grew, then loaded, under a static Search", { loaded, grew, staticSearch },
          false, true, SearchRefresh::None, {}, false, false },
        { "grew twice before the load finished", { loaded, grew, grew, loaded },
          false, true, SearchRefresh::None, {}, false, false },
        { "loaded again with nothing changed on disk", { loaded, loaded },
          false, false, SearchRefresh::None, {}, false, false },
        { "truncated, then loaded, under an auto-refreshing Search",
          { loaded, truncated, truncatedUnderAutoRefresh },
          false, false, SearchRefresh::Restart, {}, false, true },
        { "truncated, then loaded, under a static Search",
          { loaded, truncated, truncatedUnderStaticSearch },
          false, false, SearchRefresh::None, {}, false, true },
        { "truncated, then grew before the load finished",
          { loaded, truncated, grew, truncatedUnderAutoRefresh },
          false, false, SearchRefresh::Restart, {}, false, true },
        { "grew, then truncated before the load finished",
          { loaded, grew, truncated, loaded },
          false, false, SearchRefresh::None, {}, false, true },
        { "truncated, loaded, then grew and loaded", { loaded, truncated, loaded, grew, loaded },
          false, true, SearchRefresh::None, {}, false, false },
        { "reloaded after the first load", { loaded, Reload{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        { "reloaded while a growth was loading", { loaded, grew, Reload{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        { "saved Marks are applied once, not after a growth",
          { SavedMarks{ { 3 } }, loaded, grew, loaded },
          false, true, SearchRefresh::None, {}, false, false },
        { "saved Marks are applied once, not after a reload",
          { SavedMarks{ { 3 } }, loaded, Reload{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        // A reload asked for while the first load is still running leaves the
        // saved Marks to the load that finishes, which is still the first.
        { "reloaded while the first load was running", { SavedMarks{ { 3 } }, Reload{}, loaded },
          true, false, SearchRefresh::None, { 3 }, false, true },
        // A first load that fails is the first load all the same: the saved
        // Marks go with it, and the next load is not from the start.
        { "a first load that failed takes the saved Marks", { SavedMarks{ { 3 } }, failedToLoad },
          true, false, SearchRefresh::None, { 3 }, false, false },
        { "loaded after a first load that failed", { failedToLoad, loaded },
          false, false, SearchRefresh::None, {}, false, true },
        { "a Search requested before the first load runs once it has loaded",
          { SearchRequested{}, loaded },
          true, false, SearchRefresh::None, {}, true, true },
        { "a Search requested before a first load that failed is dropped",
          { SearchRequested{}, failedToLoad },
          true, false, SearchRefresh::None, {}, false, false },
        { "a Search dropped by a failed first load does not run with the next load",
          { SearchRequested{}, failedToLoad, loaded },
          false, false, SearchRefresh::None, {}, false, true },
        { "a Search waiting for the first load, then stopped",
          { SearchRequested{}, WaitingSearchDropped{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        { "a Search waiting for the first load, then cleared",
          { SearchRequested{}, SearchCleared{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        { "a Search waiting for the first load, then reloaded",
          { SearchRequested{}, Reload{}, loaded },
          true, false, SearchRefresh::None, {}, false, true },
        // Kept oddity: a load with no Log Lines leaves Format Recognition for
        // the next load, even one that only brings Log Lines added.
        { "a first load with no Log Lines recognizes no Log Format", { noLines },
          true, false, SearchRefresh::None, {}, false, false },
        { "the Log Format is recognized once Log Lines were added", { noLines, grew, loaded },
          false, true, SearchRefresh::None, {}, false, true },
        // Kept oddity: a check that finds the Log File unchanged still reports
        // it grew (see the change table), but the load after it does not take
        // it for Log Lines added.
        { "a check found the Log File unchanged", { loaded, unchanged, loaded },
          false, false, SearchRefresh::None, {}, false, false },
    };
    // clang-format on

    for ( const auto& row : rows ) {
        INFO( row.sequence );
        LoadRule rule;
        const auto decision = run( rule, row.events ).load;

        CHECK( decision.fromStart == row.fromStart );
        CHECK( decision.onlyAppended == row.onlyAppended );
        CHECK( decision.searchRefresh == row.searchRefresh );
        CHECK( numbers( decision.savedMarksToApply ) == row.savedMarksToApply );
        CHECK( decision.runWaitingSearch == row.runWaitingSearch );
        CHECK( decision.recognizeFormat == row.recognizeFormat );
    }
}

SCENARIO( "The Load Rule decides what a change on disk means", "[openlogfile][loadrule]" )
{
    // clang-format off
    const std::vector<ChangeRow> rows = {
        // sequence, events, report, clearMarks, dropSearch, forgetLogFormat
        { "grew", { loaded, grew },
          Change::Grew, false, false, false },
        { "truncated with no Search requested", { loaded, truncated },
          Change::Truncated, true, false, true },
        { "truncated under a Search", { loaded, SearchRequested{}, truncated },
          Change::Truncated, true, true, true },
        { "truncated under a Search waiting for the first load", { SearchRequested{}, truncated },
          Change::Truncated, true, true, true },
        { "truncated after the Search was cleared",
          { loaded, SearchRequested{}, SearchCleared{}, truncated },
          Change::Truncated, true, false, true },
        { "truncated after the Search was stopped",
          { loaded, SearchRequested{}, WaitingSearchDropped{}, truncated },
          Change::Truncated, true, true, true },
        { "truncated after a reload dropped the Search",
          { loaded, SearchRequested{}, Reload{}, truncated },
          Change::Truncated, true, false, true },
        { "truncated after a failed first load dropped the waiting Search",
          { SearchRequested{}, failedToLoad, truncated },
          Change::Truncated, true, false, true },
        // Kept oddity: a check that finds the Log File unchanged still reports
        // it grew. The log data tells no such check (it tells only a change,
        // on master as since #395), so the Open Log File never sees it today.
        { "a check found the Log File unchanged", { loaded, unchanged },
          Change::Grew, false, false, false },
    };
    // clang-format on

    for ( const auto& row : rows ) {
        INFO( row.sequence );
        LoadRule rule;
        const auto decision = run( rule, row.events ).change;

        CHECK( decision.report == row.report );
        CHECK( decision.clearMarks == row.clearMarks );
        CHECK( decision.dropSearch == row.dropSearch );
        CHECK( decision.forgetLogFormat == row.forgetLogFormat );
    }
}

SCENARIO( "The Load Rule decides whether a Search waits for the first load",
          "[openlogfile][loadrule]" )
{
    // clang-format off
    const std::vector<SearchRow> rows = {
        { "before any load", {}, true },
        { "while the first load is reloaded", { Reload{} }, true },
        { "after the first load", { loaded }, false },
        { "after a first load that failed", { failedToLoad }, false },
        { "while a reload after the first load is running", { loaded, Reload{} }, false },
    };
    // clang-format on

    for ( const auto& row : rows ) {
        INFO( row.sequence );
        LoadRule rule;
        auto events = row.events;
        events.emplace_back( SearchRequested{} );
        CHECK( run( rule, events ).searchWaits == row.waits );
    }
}

SCENARIO( "The Load Rule decides what a reload drops", "[openlogfile][loadrule]" )
{
    LoadRule rule;
    const auto decision = run( rule, { loaded, SearchRequested{}, Reload{} } ).reload;

    THEN( "the Search is dropped with its cache and the Marks are cleared" )
    {
        REQUIRE( decision.dropSearch );
        REQUIRE( decision.clearMarks );
    }
}

SCENARIO( "The Load Rule keeps the Marks saved with the Session until the first load",
          "[openlogfile][loadrule]" )
{
    LoadRule rule;
    run( rule, { SavedMarks{ { 3 } }, SavedMarks{ { 7 } } } );

    THEN( "they are kept while the Log File loads from its start" )
    {
        REQUIRE( rule.isLoadingFromStart() );
        REQUIRE( numbers( rule.savedMarks() ) == std::vector<uint64_t>{ 3, 7 } );
    }

    WHEN( "the first load has finished" )
    {
        run( rule, { loaded } );

        THEN( "none are kept any longer" )
        {
            REQUIRE_FALSE( rule.isLoadingFromStart() );
            REQUIRE( rule.savedMarks().empty() );
        }

        AND_WHEN( "the Log File is reloaded" )
        {
            run( rule, { Reload{} } );

            THEN( "it loads from its start again, with no saved Marks" )
            {
                REQUIRE( rule.isLoadingFromStart() );
                REQUIRE( rule.savedMarks().empty() );
            }
        }
    }
}
