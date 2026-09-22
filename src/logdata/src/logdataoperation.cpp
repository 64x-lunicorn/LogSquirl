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

OperationQueue::OperationQueue( std::function<void()> beforeOperationStart )
    : beforeOperationStart_( std::move( beforeOperationStart ) )
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

void OperationQueue::tryStartPendingOperation()
{
    executingOperation_ = std::exchange( pendingOperation_, {} );
    if ( !worker_ ) {
        LOG_WARNING << "No worker for operation";
        executingOperation_ = {};
        return;
    }

    if ( std::holds_alternative<std::monostate>( executingOperation_ ) ) {
        LOG_INFO << "no operation to start";
        return;
    }

    beforeOperationStart_();
    worker_->run( executingOperation_ );
    LOG_INFO << "Started operation " << executingOperation_.index();
}

void OperationQueue::enqueueOperation( IndexJob&& operation )
{
    ScopedLock guard( mutex_ );

    LOG_INFO << "Enqueue operation " << operation.index() << ", now executing "
             << executingOperation_.index() << ", waiting " << pendingOperation_.index();

    pendingOperation_ = waitingIndexJob( std::move( pendingOperation_ ), std::move( operation ) );

    if ( executingOperation_.index() == 0 ) {
        tryStartPendingOperation();
    }
}

void OperationQueue::finishOperationAndStartNext()
{
    ScopedLock guard( mutex_ );
    LOG_INFO << "Finished operation " << executingOperation_.index() << ", next operation "
             << pendingOperation_.index();

    tryStartPendingOperation();
}

bool OperationQueue::isPartialReindexRunning() const
{
    ScopedLock guard( mutex_ );
    return std::holds_alternative<PartialReindexJob>( executingOperation_ );
}
