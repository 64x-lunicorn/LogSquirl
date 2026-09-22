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

void AttachOperation::doStart( LogDataWorker& workerThread ) const
{
    LOG_INFO << "Attaching " << filename_ << ", encoding " << defaultEncodingMib_;
    workerThread.attachFile( filename_ );
    if ( forcedEncoding_ ) {
        workerThread.indexAll( forcedEncoding_ );
    }
    else {
        workerThread.indexAll(
            defaultEncodingMib_ >= 0 ? QTextCodec::codecForMib( defaultEncodingMib_ ) : nullptr );
    }
}

void FullReindexOperation::doStart( LogDataWorker& workerThread ) const
{
    LOG_INFO << "Reindexing (full)";
    workerThread.indexAll( forcedEncoding_, request_ );
}

void PartialReindexOperation::doStart( LogDataWorker& workerThread ) const
{
    LOG_INFO << "Reindexing (partial)";
    workerThread.indexAdditionalLines();
}

void CheckDataChangesOperation::doStart( LogDataWorker& workerThread ) const
{
    LOG_INFO << "Checking file changes";
    workerThread.checkFileChanges();
}

namespace {

// How strong an index job is under the job rule; nothing is the weakest.
int strength( const IndexJob& job )
{
    return std::visit(
        makeOverloadVisitor( []( std::monostate ) { return 0; },
                             []( const PartialReindexOperation& ) { return 1; },
                             []( const CheckDataChangesOperation& ) { return 2; },
                             []( const FullReindexOperation& full ) {
                                 return full.request() == FullIndexRequest::ExplicitReload ? 4 : 3;
                             },
                             []( const AttachOperation& ) { return 5; } ),
        job );
}

// The Encoding a Full forces, if the job is one that forces one.
QTextCodec* forcedEncodingOfFull( const IndexJob& job )
{
    const auto* full = std::get_if<FullReindexOperation>( &job );
    return full ? full->forcedEncoding() : nullptr;
}

} // namespace

IndexJob waitingIndexJob( IndexJob waiting, IndexJob arriving )
{
    auto& winner = strength( arriving ) >= strength( waiting ) ? arriving : waiting;
    const auto& loser = &winner == &arriving ? waiting : arriving;

    if ( const auto* attach = std::get_if<AttachOperation>( &winner ) ) {
        if ( auto* forcedEncoding = forcedEncodingOfFull( loser ) ) {
            return AttachOperation{ attach->getFilename(), attach->defaultEncodingMib(),
                                    forcedEncoding };
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

    std::visit( makeOverloadVisitor(
                    [ this ]( const LogDataOperation& logDataOperation ) {
                        beforeOperationStart_();
                        logDataOperation.start( worker_.get() );
                        LOG_INFO << "Started operation " << executingOperation_.index();
                    },
                    []( std::monostate ) { LOG_INFO << "no operation to start"; } ),
                executingOperation_ );
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
    return std::holds_alternative<PartialReindexOperation>( executingOperation_ );
}
