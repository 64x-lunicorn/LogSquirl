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

#ifndef FAKE_RUN_CONTROL_H
#define FAKE_RUN_CONTROL_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "backgroundrun.h"

// The run a job is part of, for a test that runs the job itself, on its own
// thread, without a Background Run: which run is the active one is whatever
// the test stores in activeRun, so a test can supersede the run at a moment of
// its own choosing. Built without one, it is a run nothing supersedes. The
// progress reported is kept, in the order reported.
class FakeRunControl final : public RunControl {
public:
    FakeRunControl( RunId id, const std::atomic<uint64_t>& activeRun )
        : id_( id )
        , activeRun_( activeRun )
    {
    }

    FakeRunControl()
        : FakeRunControl( RunId( 1 ), ownActiveRun_ )
    {
    }

    RunId id() const override
    {
        return id_;
    }

    bool isSuperseded() const override
    {
        return activeRun_.load() != id_.get();
    }

    void reportProgress( int percent ) const override
    {
        std::lock_guard lock( progressMutex_ );
        progress_.push_back( percent );
    }

    int progressReports() const
    {
        std::lock_guard lock( progressMutex_ );
        return static_cast<int>( progress_.size() );
    }

    std::vector<int> progress() const
    {
        std::lock_guard lock( progressMutex_ );
        return progress_;
    }

private:
    // Initialized before the delegating constructor binds activeRun_ to it.
    std::atomic<uint64_t> ownActiveRun_{ 1 };
    RunId id_;
    const std::atomic<uint64_t>& activeRun_;
    mutable std::mutex progressMutex_;
    mutable std::vector<int> progress_;
};

#endif
