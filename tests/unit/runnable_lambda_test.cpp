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

#include <catch2/catch_test_macros.hpp>

#include "runnable_lambda.h"

#include <QSemaphore>
#include <QThreadPool>

#include <future>
#include <string>

// A runnable is built on the thread that queues it and runs on a pool thread.
// QThreadPool hands it over under a lock inside QtCore, which ThreadSanitizer
// cannot see, so under TSan the captures read on the pool thread race with
// their construction unless the runnable publishes them itself (#482). Outside
// TSan the case only checks that the captures arrive.
TEST_CASE( "A runnable's captures reach the pool thread that runs it", "[runnable]" )
{
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );

    // The first runnable starts the pool thread, a hand-over TSan does see, and
    // keeps it busy until the second one is queued. The pool thread then takes
    // the second one from its queue without waiting for it, so nothing but
    // QtCore's own lock lies between building it here and running it there. A
    // QSemaphore lives in QtCore as well, so the gate adds no edge TSan sees.
    QSemaphore gate;
    pool.start( createRunnable( [ &gate ] { gate.acquire(); } ) );

    std::promise<std::string> seen;
    auto seenFuture = seen.get_future();
    const std::string text( "captured by value, on the queueing thread" );
    pool.start( createRunnable( [ text, &seen ] { seen.set_value( text ); } ) );
    gate.release();

    CHECK( seenFuture.get() == "captured by value, on the queueing thread" );
    pool.waitForDone();
}
