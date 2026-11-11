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

#pragma once

#include "linetypes.h"
#include "loadrule.h"
#include "regularexpressionpattern.h"
#include "searchautorefresh.h"
#include "searchsessionstate.h"
#include "settingspolicies.h"

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// What the Search Set asks of a Search (#941): to run a pattern over Log
// Lines, to drop its results and go idle, to stop the run in flight, to take
// another Search Policy, and to tell its state. The Open Log File's Searches
// are LogFilteredData; the Search Set's tests hand it a fake.
template <typename Search>
concept SearchPort
    = requires( Search& search, const Search& constSearch, const RegularExpressionPattern& pattern,
                LineNumber line, const SearchPolicy& policy ) {
          search.request( pattern, line, line );
          search.dropResults();
          search.stop();
          search.setSearchPolicy( policy );
          { constSearch.searchState() } -> std::convertible_to<SearchSessionState>;
      };

// The Search Set of an Open Log File (#941): every Search of its Log File,
// the current one and the kept ones, each with the pattern it was last
// requested with, its own auto-refresh, and whether it waits for the first
// load, is held by the Session before or after that load, or runs.
//
// A Search requested before the first load has finished waits for it and
// then runs over the Search Limits as that load leaves them; a load that does
// not succeed drops what waits. A restored Session holds the waiting Searches
// beyond that load, until it releases them (#777, #779); what is asked for
// after that is the user's and never held.
//
// Whether the user wishes for auto-refresh -- the Search Line's one button --
// is the Log File's, and every Search hears of it. Whether a Search follows
// the Log File, is suspended, or had the Log File truncated under it is the
// Search's own (#954): it is followed from the moment it is handed a request
// until a reload, and switching to another Search leaves it as it was.
//
// Every Search is made here, from the factory it is handed, so a Search's
// whole life is in one place; the Search Policy reaches each one still alive.
// The Searches are owned by whoever holds them -- the Kept Searches -- and
// this holds the current one only; one dropped elsewhere drops out of it.
//
// Like the Load Rule it is used on the Open Log File's thread only. It hears
// nothing by itself: the Open Log File tells it of every request and load.
template <SearchPort Search>
class SearchSet {
public:
    // Where a Search stands with regard to the first load.
    enum class State {
        // Not requested, cleared, dropped by a reload or by a first load that
        // did not succeed.
        Idle,
        // Requested before the first load, it runs once that has finished.
        Waiting,
        // Waiting, and held beyond the first load until released.
        HeldBeforeLoad,
        // Held since the first load finished, it runs once released.
        HeldAfterLoad,
        // Handed to its Search: running, finished or stopped.
        Running,
    };

    using SearchLimits = LoadRule::SearchLimits;

    // What the Search Set is handed by the Open Log File.
    struct Hooks {
        // Makes a new Search, with no pattern yet.
        std::function<std::shared_ptr<Search>()> newSearch;
        // The pattern a Search is requested with, as requested: with the
        // reader of the Log Format's fields, say (#798).
        std::function<RegularExpressionPattern( RegularExpressionPattern )> preparePattern
            = []( RegularExpressionPattern pattern ) { return pattern; };
        // Fewer Searches are held than before.
        std::function<void()> heldNoLonger = [] {};
    };

    explicit SearchSet( Hooks hooks )
        : hooks_( std::move( hooks ) )
        , current_( hooks_.newSearch() )
    {
        entries_.push_back( newEntry( current_ ) );
    }

    // The current Search: the one requestCurrent() runs and the Open Log
    // File follows.
    const std::shared_ptr<Search>& current() const
    {
        return current_;
    }

    // The pattern the current Search was last requested with.
    const RegularExpressionPattern& currentPattern() const
    {
        return entryOf( current_ )->pattern;
    }

    // Where search stands, Idle for one this does not hold.
    State state( const std::shared_ptr<Search>& search ) const
    {
        const auto* entry = entryOf( search );
        return entry ? entry->state : State::Idle;
    }

    // The current Search's own auto-refresh: what growth and truncation of
    // the Log File decide on for it, and what the Search Line shows.
    const SearchAutoRefresh& currentAutoRefresh() const
    {
        return entryOf( current_ )->autoRefresh;
    }

    // The auto-refresh of search, current or kept; for one this does not
    // hold, that of a Search never requested.
    SearchAutoRefresh autoRefresh( const std::shared_ptr<Search>& search ) const
    {
        const auto* entry = entryOf( search );
        return entry ? entry->autoRefresh : newEntry( search ).autoRefresh;
    }

    // The user wishes for auto-refresh, or no longer does. The current
    // Search follows the wish at once. A kept one follows it at once when it
    // is no longer wished for, so no Search is followed against the user's
    // wish; wished for again, a kept one starts from it when next requested,
    // so a Search the user stopped is not followed again behind their back.
    void setAutoRefresh( bool refresh )
    {
        autoRefreshRequested_ = refresh;
        for ( auto& entry : entries_ ) {
            if ( !refresh || entry.search.lock() == current_ ) {
                entry.autoRefresh.setAutoRefresh( refresh );
            }
            else {
                entry.autoRefresh.takeRequest( refresh );
            }
        }
    }

    // The current Search's pattern was changed and not yet requested: its
    // auto-refresh is suspended.
    void changeCurrentExpression()
    {
        entryOf( current_ )->autoRefresh.changeExpression();
    }

    // Whether the first load has finished, whatever its outcome: until it
    // has, a Search requested waits for it.
    bool hasFirstLoadFinished() const
    {
        return firstLoadFinished_;
    }

    // Keeps the current Search, with its results or still waiting, and makes
    // a new one current. Returns it.
    std::shared_ptr<Search> startAnother()
    {
        forgetDropped();
        current_->stop();
        current_ = hooks_.newSearch();
        entries_.push_back( newEntry( current_ ) );
        return current_;
    }

    // Makes search current, stopping the one current before, which still
    // waits for the first load if it did, kept. Made current again, or with
    // no Search given, the current one is stopped and waits no longer.
    void makeCurrent( std::shared_ptr<Search> search )
    {
        if ( !search || search == current_ ) {
            auto& entry = *entryOf( current_ );
            const auto wasHeld = isHeld( entry );
            if ( entry.state == State::Waiting || entry.state == State::HeldBeforeLoad ) {
                entry.state = State::Running;
            }
            current_->stop();
            tellHeldNoLonger( wasHeld );
            return;
        }

        forgetDropped();
        current_->stop();
        current_ = std::move( search );
        entryFor( current_ );
    }

    // Requests the current Search for pattern over limits, as the user does,
    // and returns whether it waits for the first load. A held one is held no
    // longer. Handed the request, it is followed by its auto-refresh.
    bool requestCurrent( const RegularExpressionPattern& pattern, SearchLimits limits )
    {
        auto& entry = *entryOf( current_ );
        if ( !settleRequested( entry, pattern ) ) {
            // Nothing to search yet: it runs over the Log Lines once they
            // have loaded, rather than over none now.
            return true;
        }
        handRequest( entry, *current_, limits );
        return false;
    }

    // Requests search -- current or kept -- for pattern over limits: the
    // current one as requestCurrent() does, a kept one beside it. A held one
    // is held no longer; requested before the first load it waits for it.
    void requestKept( const std::shared_ptr<Search>& search,
                      const RegularExpressionPattern& pattern, SearchLimits limits )
    {
        if ( !search ) {
            return;
        }
        if ( search == current_ ) {
            requestCurrent( pattern, limits );
            return;
        }

        auto& entry = entryFor( search );
        if ( !settleRequested( entry, pattern ) ) {
            return;
        }
        handRequest( entry, *search, limits );
    }

    // No Search is active any longer: the current one drops its results and
    // goes idle, and neither waits nor is held nor is followed. Its cached
    // results stay.
    void clearCurrent()
    {
        auto& entry = *entryOf( current_ );
        const auto wasHeld = isHeld( entry );
        entry.state = State::Idle;
        entry.autoRefresh.resetState();
        tellHeldNoLonger( wasHeld );
        current_->dropResults();
    }

    // The Log File is loaded anew: no Search follows it any longer, current or
    // kept, until it is requested again, so a kept one made current does not
    // continue a run over the Log File as it was. What they found stays.
    void followNoLonger()
    {
        for ( auto& entry : entries_ ) {
            entry.autoRefresh.resetState();
        }
    }

    // Stops the current Search, keeping what it found: it neither waits nor
    // is held any longer, and its auto-refresh is suspended.
    void stopCurrent()
    {
        auto& entry = *entryOf( current_ );
        const auto wasHeld = isHeld( entry );
        if ( entry.state != State::Idle ) {
            entry.state = State::Running;
        }
        entry.autoRefresh.stopSearch();
        tellHeldNoLonger( wasHeld );
        current_->stop();
    }

    // The current Search starts again with its pattern over limits, its
    // results dropped first.
    void restartCurrent( SearchLimits limits )
    {
        current_->dropResults();
        const auto pattern = entryOf( current_ )->pattern;
        requestCurrent( pattern, limits );
    }

    // The current Search is requested again with its pattern over limits,
    // unless it is held or idle.
    void requestCurrentAgain( SearchLimits limits )
    {
        if ( isHeld( *entryOf( current_ ) )
             || current_->searchState().phase == SearchSessionPhase::Idle ) {
            return;
        }
        const auto pattern = entryOf( current_ )->pattern;
        requestCurrent( pattern, limits );
    }

    // The Log File was truncated: every Search requested -- waiting, held or
    // run, current or kept -- has it truncated under it, so one auto-refreshed
    // starts again once it is current and the Log File has loaded. The
    // current one's results no longer describe the Log File: they are
    // dropped. Returns whether they were. A kept Search drops what it found
    // beyond the Log File's end as every Search is told the Log Lines changed.
    bool truncateFile()
    {
        for ( auto& entry : entries_ ) {
            if ( entry.state != State::Idle ) {
                entry.autoRefresh.truncateFile();
            }
        }
        if ( entryOf( current_ )->state == State::Idle ) {
            return false;
        }
        current_->dropResults();
        return true;
    }

    // Holds the Searches that wait for the first load now beyond it. Once it
    // has finished nothing waits, and this holds nothing.
    void holdWaiting()
    {
        if ( firstLoadFinished_ ) {
            return;
        }
        for ( auto& entry : entries_ ) {
            if ( entry.state == State::Waiting ) {
                entry.state = State::HeldBeforeLoad;
            }
        }
    }

    // Holds nothing any longer. Held beyond the first load, a Search still
    // alive runs over limits: the current one as requestCurrent() does, the
    // others beside it. Held before it, they wait for it as though never
    // held.
    void releaseHeld( SearchLimits limits )
    {
        auto released = false;
        std::vector<Entry> toRun;
        for ( auto& entry : entries_ ) {
            if ( entry.state == State::HeldBeforeLoad ) {
                entry.state = State::Waiting;
                released = true;
            }
            else if ( entry.state == State::HeldAfterLoad ) {
                entry.state = State::Running;
                released = true;
                toRun.push_back( entry );
            }
        }
        run( toRun, limits );
        tellHeldNoLonger( released );
    }

    // Whether search is held, waiting to be released.
    bool isHeld( const std::shared_ptr<Search>& search ) const
    {
        const auto* entry = search ? entryOf( search ) : nullptr;
        return entry && isHeld( *entry );
    }

    // Whether any Search still alive is held.
    bool holdsAny() const
    {
        return std::ranges::any_of( entries_, []( const Entry& entry ) {
            return isHeld( entry ) && !entry.search.expired();
        } );
    }

    // A load of the Log File finished; only the first one matters here. When
    // it succeeded, the Searches waiting for it run over limits, the current
    // one as requestCurrent() does; the held ones wait on, held after it.
    // When it did not, there is nothing to search: what waits and what is
    // held is dropped.
    void loadFinished( bool succeeded, SearchLimits limits )
    {
        if ( firstLoadFinished_ ) {
            return;
        }
        firstLoadFinished_ = true;

        auto heldNoLonger = false;
        std::vector<Entry> toRun;
        for ( auto& entry : entries_ ) {
            switch ( entry.state ) {
            case State::Waiting:
                if ( succeeded ) {
                    entry.state = State::Running;
                    toRun.push_back( entry );
                }
                else {
                    entry.state = State::Idle;
                }
                break;
            case State::HeldBeforeLoad:
                if ( succeeded ) {
                    entry.state = State::HeldAfterLoad;
                }
                else {
                    entry.state = State::Idle;
                    heldNoLonger = true;
                }
                break;
            case State::Idle:
            case State::HeldAfterLoad:
            case State::Running:
                break;
            }
        }
        run( toRun, limits );
        tellHeldNoLonger( heldNoLonger );
    }

    // Replaces the Search Policy of every Search still alive, the kept ones
    // included.
    void setSearchPolicy( const SearchPolicy& policy )
    {
        for ( const auto& entry : entries_ ) {
            if ( const auto search = entry.search.lock() ) {
                search->setSearchPolicy( policy );
            }
        }
    }

private:
    struct Entry {
        std::weak_ptr<Search> search;
        RegularExpressionPattern pattern;
        State state = State::Idle;
        // The Search's own: whether it follows the Log File (#954).
        SearchAutoRefresh autoRefresh;
    };

    static bool isHeld( const Entry& entry )
    {
        return entry.state == State::HeldBeforeLoad || entry.state == State::HeldAfterLoad;
    }

    // The entry of search in entries, null when there is none.
    template <typename Entries>
    static auto* find( Entries& entries, const std::shared_ptr<Search>& search )
    {
        const auto found = std::ranges::find_if(
            entries, [ &search ]( const Entry& entry ) { return entry.search.lock() == search; } );
        return found != entries.end() ? &*found : nullptr;
    }

    const Entry* entryOf( const std::shared_ptr<Search>& search ) const
    {
        return find( entries_, search );
    }

    Entry* entryOf( const std::shared_ptr<Search>& search )
    {
        return find( entries_, search );
    }

    // The entry of search, added when it was made elsewhere.
    Entry& entryFor( const std::shared_ptr<Search>& search )
    {
        if ( auto* entry = entryOf( search ) ) {
            return *entry;
        }
        return entries_.emplace_back( newEntry( search ) );
    }

    // The Search of entry was requested for pattern: it waits for the first
    // load, or runs now, and a held one is held no longer, which is told.
    // Returns whether it runs now.
    bool settleRequested( Entry& entry, const RegularExpressionPattern& pattern )
    {
        const auto wasHeld = isHeld( entry );
        entry.pattern = pattern;
        entry.state = firstLoadFinished_ ? State::Running : State::Waiting;
        tellHeldNoLonger( wasHeld );
        return firstLoadFinished_;
    }

    void tellHeldNoLonger( bool heldNoLonger )
    {
        if ( heldNoLonger ) {
            hooks_.heldNoLonger();
        }
    }

    // Requests the Searches still alive among entries, each with its pattern:
    // the current one first, as the user requests it, then the others beside
    // it.
    void run( const std::vector<Entry>& entries, SearchLimits limits )
    {
        if ( const auto* current = find( entries, current_ ) ) {
            requestCurrent( current->pattern, limits );
        }
        for ( const auto& entry : entries ) {
            const auto search = entry.search.lock();
            auto* kept = search && search != current_ ? entryOf( search ) : nullptr;
            if ( kept ) {
                handRequest( *kept, *search, limits );
            }
        }
    }

    // Hands the Search of entry the request settled for it, over limits: from
    // now on its auto-refresh follows it, as the user wishes; an invalid
    // pattern leaves no Search active.
    void handRequest( Entry& entry, Search& search, SearchLimits limits )
    {
        search.request( hooks_.preparePattern( entry.pattern ), limits.start, limits.end );
        if ( search.searchState().phase != SearchSessionPhase::InvalidPattern ) {
            entry.autoRefresh.startSearch();
        }
        else {
            entry.autoRefresh.resetState();
        }
    }

    // The entry of a Search new to this, never requested; its auto-refresh
    // starts from the user's wish.
    Entry newEntry( const std::shared_ptr<Search>& search ) const
    {
        Entry entry;
        entry.search = search;
        entry.autoRefresh.takeRequest( autoRefreshRequested_ );
        return entry;
    }

    // Forgets the Searches dropped since, so the list does not grow without
    // bound over a long session.
    void forgetDropped()
    {
        std::erase_if( entries_, []( const Entry& entry ) { return entry.search.expired(); } );
    }

    Hooks hooks_;
    std::shared_ptr<Search> current_;
    // Every Search made, the current one among them, in the order made.
    std::vector<Entry> entries_;
    bool firstLoadFinished_ = false;
    // Whether the user wishes for auto-refresh: one for the Log File, heard by
    // every Search.
    bool autoRefreshRequested_ = false;
};
