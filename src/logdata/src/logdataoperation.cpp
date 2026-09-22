/*
 * Copyright (C) 2009, 2010, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include "logdataoperation.h"

#include "log.h"
#include "overload_visitor.h"
#include "synchronization.h"

namespace {

// How strong an index job is under the job rule; nothing is the weakest.
int strength( const IndexJob& job )
{
    return std::visit(
        makeOverloadVisitor( []( std::monostate ) { return 0; },
                             []( const PartialReindexJob& ) { return 1; },
                             []( const CheckForChangesJob& ) { return 2; },
                             []( const FullReindexJob& full ) {
                                 return full.request == FullIndexRequest::ExplicitReload ? 4 : 3;
                             },
                             []( const AttachJob& ) { return 5; } ),
        job );
}

// The index job's name under the job rule, for the log.
const char* nameOf( const IndexJob& job )
{
    return std::visit( makeOverloadVisitor( []( std::monostate ) { return "none"; },
                                            []( const PartialReindexJob& ) { return "Partial"; },
                                            []( const CheckForChangesJob& ) { return "Check"; },
                                            []( const FullReindexJob& full ) {
                                                return full.request
                                                               == FullIndexRequest::ExplicitReload
                                                           ? "Full (explicit reload)"
                                                           : "Full (automatic)";
                                            },
                                            []( const AttachJob& ) { return "Attach"; } ),
                       job );
}

// The Encoding a Full forces, if the job is one that forces one.
QTextCodec* forcedEncodingOfFull( const IndexJob& job )
{
    const auto* full = std::get_if<FullReindexJob>( &job );
    return full ? full->forcedEncoding : nullptr;
}

} // namespace

IndexJob waitingIndexJob( IndexJob waiting, IndexJob arriving )
{
    auto& winner = strength( arriving ) >= strength( waiting ) ? arriving : waiting;
    const auto& loser = &winner == &arriving ? waiting : arriving;

    if ( const auto* attach = std::get_if<AttachJob>( &winner ) ) {
        if ( auto* forcedEncoding = forcedEncodingOfFull( loser ) ) {
            return AttachJob{ attach->fileName, attach->defaultEncodingMib, forcedEncoding };
        }
    }

    return std::move( winner );
}

OperationQueue::OperationQueue( std::function<void()> beforeJobStart )
    : beforeJobStart_( std::move( beforeJobStart ) )
{
}

void OperationQueue::setWorker( std::unique_ptr<LogDataWorker>&& worker )
{
    worker_ = std::move( worker );
}

void OperationQueue::setIndexingPolicy( const IndexingPolicy& indexingPolicy )
{
    ScopedLock guard( mutex_ );
    if ( worker_ ) {
        worker_->setIndexingPolicy( indexingPolicy );
    }
}

void OperationQueue::interrupt()
{
    ScopedLock guard( mutex_ );
    if ( worker_ ) {
        worker_->interrupt();
    }
}

void OperationQueue::shutdown()
{
    std::unique_ptr<LogDataWorker> worker;
    {
        ScopedLock guard( mutex_ );
        worker = std::move( worker_ );
    }

    // Interrupt and destroy the worker outside the queue mutex so that the
    // worker destructor can wait for its thread pool without contention.
    if ( worker ) {
        worker->interrupt();
    }
    worker.reset();

    LOG_INFO << "Operation queue shutdown";
}

void OperationQueue::tryStartWaitingJob()
{
    runningJob_ = std::exchange( waitingJob_, {} );
    if ( !worker_ ) {
        LOG_WARNING << "No worker for index job";
        runningJob_ = {};
        return;
    }

    if ( std::holds_alternative<std::monostate>( runningJob_ ) ) {
        LOG_INFO << "No index job to start";
        return;
    }

    beforeJobStart_();
    worker_->run( runningJob_ );
    LOG_INFO << "Started index job " << nameOf( runningJob_ );
}

void OperationQueue::enqueueJob( IndexJob&& job )
{
    ScopedLock guard( mutex_ );

    LOG_INFO << "Enqueue index job " << nameOf( job ) << ", now running " << nameOf( runningJob_ )
             << ", waiting " << nameOf( waitingJob_ );

    waitingJob_ = waitingIndexJob( std::move( waitingJob_ ), std::move( job ) );

    if ( std::holds_alternative<std::monostate>( runningJob_ ) ) {
        tryStartWaitingJob();
    }
}

void OperationQueue::finishJobAndStartNext()
{
    ScopedLock guard( mutex_ );
    LOG_INFO << "Finished index job " << nameOf( runningJob_ ) << ", next index job "
             << nameOf( waitingJob_ );

    tryStartWaitingJob();
}

bool OperationQueue::isPartialReindexRunning() const
{
    ScopedLock guard( mutex_ );
    return std::holds_alternative<PartialReindexJob>( runningJob_ );
}
