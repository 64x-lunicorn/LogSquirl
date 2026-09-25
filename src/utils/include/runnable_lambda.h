/*
 * Copyright (C) 2022 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_RUNNABLE_LAMBDA_H
#define LOGSQUIRL_RUNNABLE_LAMBDA_H

#include <QRunnable>

#include <atomic>
#include <type_traits>
#include <utility>

// Built on the thread that queues it, run on a pool thread. QThreadPool hands
// it over under a lock inside QtCore; that lock orders the two threads, but
// ThreadSanitizer cannot see it, as QtCore is not built with it. So the wrapper
// publishes what it captured itself: a release store once it is built, an
// acquire load before it runs. The pair costs next to nothing and gives TSan
// the edge it cannot see inside Qt (#482).
template <typename TRunnable>
class RunnableWrapper : public QRunnable {
public:
    RunnableWrapper( TRunnable&& runnable )
        : runnable_( std::move( runnable ) )
    {
        setAutoDelete( true );
        published_.store( true, std::memory_order_release );
    }

    void run() override
    {
        [[maybe_unused]] const auto published = published_.load( std::memory_order_acquire );
        runnable_();
    }

private:
    TRunnable runnable_;
    std::atomic<bool> published_{ false };
};

template <typename TRunnable>
auto* createRunnable( TRunnable&& runnable )
{
    // Forwarded: an lvalue no longer binds, so a caller cannot have its
    // runnable moved from silently; it passes std::move() explicitly.
    return new RunnableWrapper<std::remove_cvref_t<TRunnable>>(
        std::forward<TRunnable>( runnable ) );
}

#endif