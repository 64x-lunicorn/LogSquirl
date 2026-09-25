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

// A data race on purpose, built in a ThreadSanitizer build only (#439).
// tests/tsan_canary.cmake runs this through the ctest runner and checks that
// the race fails the case: the proof, on every TSan run, that a race in
// LogSquirl's own code turns the Sanitizers / tsan job red. Do not fix it.

#include <cstdio>
#include <thread>

namespace {

int unguardedCounter = 0;

[[gnu::noinline]] void bumpUnguardedCounter()
{
    for ( int i = 0; i < 1000; ++i ) {
        ++unguardedCounter;
    }
}

} // namespace

int main()
{
    std::thread first( bumpUnguardedCounter );
    std::thread second( bumpUnguardedCounter );
    first.join();
    second.join();

    // Exits 0 whatever the count: the race is what fails the case, not this.
    std::printf( "tsan canary: the two threads are done (%d)\n", unguardedCounter );
    return 0;
}
