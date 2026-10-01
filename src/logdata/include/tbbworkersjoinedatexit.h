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

#include <memory>

// Joins oneTBB's worker threads when main() ends (#665).
//
// Left to itself, oneTBB lets its workers end on their own once main() has
// returned, without waiting for them. They can still be ending while exit()
// runs mimalloc's process teardown, and then a worker's thread teardown and
// mimalloc's both handle the same abandoned segments: a mimalloc built with
// its internal checks (a Debug build) stops the process on an assertion.
//
// Made first in main(), it is destroyed last, after every flow graph and
// tbb::global_control of main() is gone, which joining needs. A pool thread
// that ran oneTBB's work and is still ending is waited for a little. Should
// something of oneTBB still be in use after that, the workers are left to
// end as oneTBB would have ended them.
class TbbWorkersJoinedAtExit {
public:
    TbbWorkersJoinedAtExit();
    ~TbbWorkersJoinedAtExit();

    TbbWorkersJoinedAtExit( const TbbWorkersJoinedAtExit& ) = delete;
    TbbWorkersJoinedAtExit& operator=( const TbbWorkersJoinedAtExit& ) = delete;
    TbbWorkersJoinedAtExit( TbbWorkersJoinedAtExit&& ) = delete;
    TbbWorkersJoinedAtExit& operator=( TbbWorkersJoinedAtExit&& ) = delete;

private:
    // Joins the workers: whether they were joined.
    bool join();

    struct Handle;
    std::unique_ptr<Handle> handle_;
};
