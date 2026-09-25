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

#include "backgroundrun.h"

// The run a job is part of, for a test that runs the job itself, on its own
// thread, without a Background Run: which run is the active one is whatever
// the test stores in activeRun, so a test can supersede the run at a moment of
// its own choosing. Progress is only counted.
class FakeRunControl final : public RunControl {
public:
    FakeRunControl( RunId id, const std::atomic<uint64_t>& activeRun )
        : id_( id )
        , activeRun_( activeRun )
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

    void reportProgress( int ) const override
    {
        ++progressReports_;
    }

    int progressReports() const
    {
        return progressReports_.load();
    }

private:
    RunId id_;
    const std::atomic<uint64_t>& activeRun_;
    mutable std::atomic<int> progressReports_{ 0 };
};

#endif
