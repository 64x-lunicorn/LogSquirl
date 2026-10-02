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

#include "tbbworkersjoinedatexit.h"

#include <chrono>
#include <new>
#include <thread>

#include <tbb/global_control.h>

struct TbbWorkersJoinedAtExit::Handle {
    tbb::task_scheduler_handle scheduler{ tbb::attach{} };
};

TbbWorkersJoinedAtExit::TbbWorkersJoinedAtExit()
    : handle_( std::make_unique<Handle>() )
{
}

TbbWorkersJoinedAtExit::~TbbWorkersJoinedAtExit()
{
    join();
}

bool TbbWorkersJoinedAtExit::join()
{
    if ( tbb::finalize( handle_->scheduler, std::nothrow ) ) {
        return true;
    }
    // A thread of a pool that ran oneTBB's work may still be ending, and
    // holds oneTBB until it has: wait a little for it and try again.
    for ( int attempt = 0; attempt < 50; ++attempt ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        tbb::task_scheduler_handle again{ tbb::attach{} };
        if ( tbb::finalize( again, std::nothrow ) ) {
            return true;
        }
    }
    return false;
}
