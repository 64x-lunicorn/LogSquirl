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

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

// Runs one lookup at a time on a worker thread and hands its result back on
// the thread this object lives in, like the Value Count's worker: an atomic
// flag cancels it, and a cancelled lookup leaves no result behind, however far
// it had come. Starting another lookup cancels the running one.
//
// The work gets the flag to look at; it returns the result. The result is
// given to the callback only if the lookup was not cancelled meanwhile.
class LookupRunner : public QObject {
public:
    explicit LookupRunner( QObject* parent = nullptr )
        : QObject( parent )
    {
    }

    ~LookupRunner() override
    {
        for ( const auto& job : jobs_ ) {
            job->cancelled.store( true );
        }
        for ( const auto& job : jobs_ ) {
            job->future.waitForFinished();
        }
    }

    template <typename Result, typename Work, typename Done>
    void start( Work work, Done done )
    {
        cancel();

        auto job = std::make_shared<Job>();
        running_ = job;
        jobs_.push_back( job );

        auto* watcher = new QFutureWatcher<Result>( this );
        connect( watcher, &QFutureWatcherBase::finished, this,
                 [ this, job, watcher, done = std::move( done ) ]() mutable {
                     const auto isCurrent = job == running_;
                     jobs_.erase( std::remove( jobs_.begin(), jobs_.end(), job ), jobs_.end() );
                     if ( isCurrent ) {
                         running_.reset();
                         done( watcher->result() );
                     }
                     watcher->deleteLater();
                 } );

        auto future = QtConcurrent::run(
            [ work = std::move( work ), state = job.get() ]() mutable -> Result {
                // Released before the result is reported, so what the work
                // holds (the log data) never outlives the owner's wait.
                const auto owned = std::move( work );
                return owned( state->cancelled );
            } );
        job->future = future;
        watcher->setFuture( future );
    }

    // Drops the running lookup: it stops, and its result is never reported.
    void cancel()
    {
        if ( running_ ) {
            running_->cancelled.store( true );
            running_.reset();
        }
    }

    bool isRunning() const
    {
        return running_ != nullptr;
    }

private:
    struct Job {
        std::atomic<bool> cancelled{ false };
        QFuture<void> future;
    };

    std::shared_ptr<Job> running_;
    std::vector<std::shared_ptr<Job>> jobs_;
};
