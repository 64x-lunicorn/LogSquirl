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

#ifndef LOGSQUIRL_BACKGROUND_RUN_H
#define LOGSQUIRL_BACKGROUND_RUN_H

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>

#include <QMetaType>
#include <QSemaphore>
#include <QString>
#include <QThreadPool>

#include <type_safe/strong_typedef.hpp>

#include "runnable_lambda.h"

// Identifies one run of a Background Run. Every run started gets a fresh id,
// counting from 1; 0 is never one.
struct RunId : type_safe::strong_typedef<RunId, uint64_t>,
               type_safe::strong_typedef_op::equality_comparison<RunId> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    UnderlyingType get() const
    {
        return type_safe::get( *this );
    }
};
Q_DECLARE_METATYPE( RunId )

// What a job sees of the run it is part of. Safe to use from any thread for
// as long as the job runs, the threads a job fans out to included.
class RunControl {
public:
    virtual ~RunControl() = default;

    RunControl( const RunControl& ) = delete;
    RunControl& operator=( const RunControl& ) = delete;
    RunControl( RunControl&& ) = delete;
    RunControl& operator=( RunControl&& ) = delete;

    virtual RunId id() const = 0;

    // True once another run has started, or the run was interrupted or the
    // Background Run shut down: whatever the job still does is no longer
    // wanted, and it should stop as soon as it can.
    virtual bool isSuperseded() const = 0;

    // Reports how far the run is, in percent. Reaches whoever started it on
    // the thread the Background Run lives on, in the order reported, and
    // always before the run's finish report.
    virtual void reportProgress( int percent ) const = 0;

protected:
    RunControl() = default;
};

// How a run ended, as its finish report carries it.
template <typename Outcome>
struct RunEnd {
    RunId id{ 0 };
    // The run was superseded (by a newer run, an interrupt, or shutdown)
    // before its job returned. Never set together with a failure.
    bool superseded = false;
    // What went wrong when the job threw; empty when it returned. The outcome
    // is then the default one.
    QString failure;
    Outcome outcome{};
};

// Hands reports from the threads of a Background Run to the thread it lives
// on, in the order they were posted. Not for use outside BackgroundRun.
class BackgroundRunMailbox {
public:
    // Built on, and delivering to, the calling thread.
    BackgroundRunMailbox();
    ~BackgroundRunMailbox();

    BackgroundRunMailbox( const BackgroundRunMailbox& ) = delete;
    BackgroundRunMailbox& operator=( const BackgroundRunMailbox& ) = delete;
    BackgroundRunMailbox( BackgroundRunMailbox&& ) = delete;
    BackgroundRunMailbox& operator=( BackgroundRunMailbox&& ) = delete;

    // Any thread. Nothing is delivered once the mailbox is closed.
    void post( std::function<void()> delivery );

    // On the thread it delivers to: drops what was not delivered yet, and
    // everything posted from now on.
    void close();

    struct State;

private:
    std::shared_ptr<State> state_;
};

namespace background_run_detail {
// Logged, not thrown: shutdown runs in a destructor.
void reportShutdownTimeout( const QString& name );
QString describeFailure( const QString& name, const std::exception& failure );
QString describeUnknownFailure( const QString& name );
} // namespace background_run_detail

// A Background Run: runs jobs on a thread of its own, one at a time, and owns
// the thread mechanics around them, so the job itself is plain code.
//
// - Starting a run supersedes the run before it (one scheme: the run id), and
//   returns only once the new run has started on that thread, that is once
//   the one before has ended.
// - Each run is handed a copy of the Policy taken when it started: a Policy
//   replaced mid-run takes effect on the next run.
// - The reader is attached when a run starts and detached when its finish
//   report is delivered -- or, for a run whose report never is, when the
//   Background Run shuts down. Attached and detached are always in balance.
// - Every run started is reported finished exactly once, on the thread the
//   Background Run lives on: after its last progress report, whether its job
//   returned, was superseded or threw. Superseded runs are reported too; who
//   started them decides whether a report still matters, by its id. The
//   report is a plain call, so it may start the next run itself.
// - Shutting down (the destructor) supersedes the run in flight, waits up to
//   10 s for it, and delivers nothing more. A job still running after that is
//   left to itself, with its thread, its reader still attached and whatever
//   it references: better than a process that cannot end (TBB hanging on
//   Windows).
//
// start(), interrupt() and the destructor are called on the thread the
// Background Run lives on, which is the one reports are delivered on;
// setPolicy() from any thread.
template <typename Policy, typename Outcome = std::monostate>
class BackgroundRun {
public:
    using Job = std::function<Outcome( const RunControl&, const Policy& )>;
    using Progressed = std::function<void( RunId, int percent )>;
    using Finished = std::function<void( const RunEnd<Outcome>& )>;

    // Whatever must stay open while a run reads (the Log File, when it is
    // kept closed otherwise). Either may be empty.
    struct Reader {
        std::function<void()> attach;
        std::function<void()> detach;
    };

    // name describes the runs in their failures, and names the thread.
    BackgroundRun( QString name, Policy policy, Reader reader, Progressed progressed,
                   Finished finished )
        : name_( std::move( name ) )
        , policy_( std::move( policy ) )
        , reader_( std::move( reader ) )
        , progressed_( std::move( progressed ) )
        , finished_( std::move( finished ) )
    {
        pool_->setMaxThreadCount( 1 );
        pool_->setObjectName( name_ );
    }

    ~BackgroundRun()
    {
        try {
            shared_->activeRun.store( 0, std::memory_order_release );
            pool_->clear();
            // Not holding policyMutex_ (or anything a job could wait on) here.
            const bool done = pool_->waitForDone( ShutdownTimeoutMs );
            shared_->mailbox.close();
            if ( !done ) {
                // Destroying the pool would wait for the job without a limit,
                // and detaching the reader would close the Log File under it.
                background_run_detail::reportShutdownTimeout( name_ );
                static_cast<void>( pool_.release() );
                return;
            }
            for ( ; undelivered_ > 0; --undelivered_ ) {
                detachReader();
            }
        } catch ( ... ) {
            // A destructor: nothing to report to, and nothing left to undo.
        }
    }

    BackgroundRun( const BackgroundRun& ) = delete;
    BackgroundRun& operator=( const BackgroundRun& ) = delete;
    BackgroundRun( BackgroundRun&& ) = delete;
    BackgroundRun& operator=( BackgroundRun&& ) = delete;

    // Starts job as the new active run, superseding the one in flight, and
    // returns its id once it has started.
    RunId start( Job job )
    {
        const auto id = RunId( ++lastRun_ );
        // Becoming the active run at once, without waiting for the one before
        // to acknowledge, is what lets that one be superseded rather than
        // waited on.
        shared_->activeRun.store( id.get(), std::memory_order_release );

        auto policy = [ this ] {
            std::lock_guard lock( policyMutex_ );
            return policy_;
        }();

        if ( reader_.attach ) {
            reader_.attach();
        }
        ++undelivered_;

        // Shared with the runnable, not captured by reference: start() may
        // return from acquire() and drop its handle while the pool thread is
        // still inside release(); the last owner destroys the semaphore (#482).
        auto started = std::make_shared<QSemaphore>();
        pool_->start( createRunnable( [ this, shared = shared_, started, id, name = name_,
                                        job = std::move( job ), policy = std::move( policy ) ] {
            started->release();
            const Control control{ this, shared, id };
            RunEnd<Outcome> end;
            end.id = id;
            try {
                end.outcome = job( control, policy );
            } catch ( const std::exception& failure ) {
                end.failure = background_run_detail::describeFailure( name, failure );
            } catch ( ... ) {
                end.failure = background_run_detail::describeUnknownFailure( name );
            }
            // A failure is how the run ended, superseded or not.
            end.superseded = end.failure.isEmpty() && control.isSuperseded();
            // Posted last, after every progress report of the job.
            shared->mailbox.post( [ this, end = std::move( end ) ] { deliverFinish( end ); } );
        } ) );
        started->acquire();

        return id;
    }

    // Supersedes the run in flight, if any, without starting another. Does
    // not wait for it.
    void interrupt()
    {
        shared_->activeRun.store( 0, std::memory_order_release );
    }

    // Replaces the Policy handed to the runs started from now on.
    void setPolicy( const Policy& policy )
    {
        std::lock_guard lock( policyMutex_ );
        policy_ = policy;
    }

private:
    static constexpr int ShutdownTimeoutMs = 10'000;

    // Outlives the Background Run if a job is still running when shutdown
    // gives up waiting for it.
    struct Shared {
        std::atomic<uint64_t> activeRun{ 0 };
        BackgroundRunMailbox mailbox;
    };

    class Control final : public RunControl {
    public:
        Control( BackgroundRun* run, std::shared_ptr<Shared> shared, RunId id )
            : run_( run )
            , shared_( std::move( shared ) )
            , id_( id )
        {
        }

        RunId id() const override
        {
            return id_;
        }

        bool isSuperseded() const override
        {
            return shared_->activeRun.load( std::memory_order_acquire ) != id_.get();
        }

        void reportProgress( int percent ) const override
        {
            // run_ is only used on delivery, which the mailbox stops before
            // the Background Run is gone.
            shared_->mailbox.post( [ run = run_, id = id_, percent ] {
                if ( run->progressed_ ) {
                    run->progressed_( id, percent );
                }
            } );
        }

    private:
        BackgroundRun* run_;
        std::shared_ptr<Shared> shared_;
        RunId id_;
    };

    void detachReader() const
    {
        if ( reader_.detach ) {
            reader_.detach();
        }
    }

    // The one place a run is reported finished, on the thread this lives on.
    // Nothing touches this object after finished_: it may destroy it.
    void deliverFinish( const RunEnd<Outcome>& end )
    {
        --undelivered_;
        detachReader();
        if ( finished_ ) {
            finished_( end );
        }
    }

    const QString name_;

    mutable std::mutex policyMutex_;
    Policy policy_;

    const Reader reader_;
    const Progressed progressed_;
    const Finished finished_;

    // Only on the thread this lives on.
    uint64_t lastRun_ = 0;
    // Runs started whose finish report has not been delivered yet: each holds
    // one attached reader.
    uint64_t undelivered_ = 0;

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();

    // Declared last, so it is destroyed first, once the destructor has waited
    // for it.
    std::unique_ptr<QThreadPool> pool_ = std::make_unique<QThreadPool>();
};

#endif
