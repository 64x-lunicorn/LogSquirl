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

#include "tablesorter.h"

#include <algorithm>
#include <deque>

#include <QFutureWatcher>
#include <QtConcurrent>

#include "abstractlogdata.h"
#include "logformatdefinition.h"
#include "tablerowcells.h"

namespace {

// The Log File's reader, attached for as long as the sort reads.
class AttachedReader {
public:
    explicit AttachedReader( const AbstractLogData& logData )
        : logData_( logData )
    {
        logData_.attachReader();
    }
    ~AttachedReader()
    {
        logData_.detachReader();
    }
    AttachedReader( const AttachedReader& ) = delete;
    AttachedReader& operator=( const AttachedReader& ) = delete;

private:
    const AbstractLogData& logData_;
};

// The Timestamps of the Log Lines just before the one read, as many as the
// elapsed time looks back: what the model remembers for the same lookup.
class RecentTimestamps {
public:
    void push( const std::optional<QDateTime>& timestamp )
    {
        recent_.push_back( timestamp );
        if ( recent_.size() > TableRowCells::ElapsedLookBack ) {
            recent_.pop_front();
        }
    }

    // The Timestamp of the Log Line back before the one read, 1 being the
    // one just before it.
    std::optional<QDateTime> of( uint64_t back ) const
    {
        if ( back == 0 || back > recent_.size() ) {
            return std::nullopt;
        }
        return recent_[ recent_.size() - back ];
    }

private:
    std::deque<std::optional<QDateTime>> recent_;
};

} // namespace

std::optional<TableSortResult> sortTableRows( const TableSortRequest& request,
                                              const std::atomic<bool>& cancel,
                                              std::atomic<uint64_t>& linesDone )
{
    if ( request.logData == nullptr || !request.format || !request.base ) {
        return std::nullopt;
    }
    const TableRowCells cells( *request.format, request.modificationDate );
    const auto total = static_cast<uint32_t>(
        std::min( request.lineCount.get(), static_cast<uint64_t>( UINT32_MAX ) ) );

    // The keys read so far are kept when they are the column's and no Log
    // Line of theirs went; else every key is read anew.
    auto keys = request.keys && request.keys->kind() == cells.sortKeyKindOf( request.column )
                        && request.keys->size() <= total
                    ? std::make_shared<TableSortKeys>( *request.keys )
                    : std::make_shared<TableSortKeys>( cells.sortKeyKindOf( request.column ) );
    const auto from = keys->size();
    keys->reserve( total );

    {
        const AttachedReader reader( *request.logData );
        const auto& logData = *request.logData;

        // For the elapsed column, the Log Lines before the first one read
        // seed the look back, as the model reads them for it; no other key
        // needs a Log Line but its own.
        RecentTimestamps recent;
        const auto seedFrom = from > TableRowCells::ElapsedLookBack
                                  ? from - static_cast<uint32_t>( TableRowCells::ElapsedLookBack )
                                  : 0U;
        if ( seedFrom < from
             && cells.sortKeyKindOf( request.column ) == TableRowCells::SortKeyKind::Elapsed ) {
            const auto seed
                = logData.getLines( LineNumber( seedFrom ), LinesCount( from - seedFrom ) );
            for ( const auto& line : seed ) {
                recent.push( cells.timestampOf( line ) );
            }
        }

        constexpr uint32_t batchSize = 5000;
        for ( uint32_t start = from; start < total; start += batchSize ) {
            if ( cancel.load() ) {
                return std::nullopt;
            }
            const auto batch = std::min( batchSize, total - start );
            const auto lines = logData.getLines( LineNumber( start ), LinesCount( batch ) );
            uint32_t logLine = start;
            for ( const auto& line : lines ) {
                const auto row = cells.rowOf( LineNumber( logLine ), line,
                                              [ &recent, logLine ]( LineNumber earlier ) {
                                                  return recent.of( logLine - earlier.get() );
                                              } );
                keys->append( cells.sortKeyOf( request.column, row ) );
                recent.push( row.timestamp );
                ++logLine;
            }
            // A Log File shorter than announced: what it has is what is sorted.
            if ( lines.size() < batch ) {
                break;
            }
            linesDone.store( start + batch - from );
        }
    }

    // The Rows to order: the base mapping's, over the Log Lines keyed.
    const auto keyed = LinesCount( keys->size() );
    const auto rowCount = request.base->rowCount( keyed );
    const auto logLinesOfRows = [ & ]( int firstRow ) {
        std::vector<uint32_t> lines;
        lines.reserve( static_cast<size_t>( std::max( rowCount - firstRow, 0 ) ) );
        for ( int row = firstRow; row < rowCount; ++row ) {
            const auto logLine = request.base->logLineAt( row ).get();
            if ( logLine < keyed.get() ) {
                lines.push_back( static_cast<uint32_t>( logLine ) );
            }
        }
        return lines;
    };

    TableSortResult result;
    result.column = request.column;
    result.order = request.order;
    const auto sortedCount = request.rows ? static_cast<int>( request.rows->logLines().size() ) : 0;
    const bool merge = request.rows && sortedCount > 0 && sortedCount <= rowCount;
    if ( merge ) {
        auto appended = logLinesOfRows( sortedCount );
        const auto expected = request.rows->logLines().size() + appended.size();
        auto merged = mergeSortedLogLines( *keys, request.order, request.rows->logLines(),
                                           std::move( appended ), cancel );
        if ( merged.logLines.size() != expected ) {
            return std::nullopt;
        }
        result.merged = true;
        result.inserted = std::move( merged.inserted );
        result.rows = std::make_shared<const SortedRows>( std::move( merged.logLines ) );
    }
    else {
        auto lines = logLinesOfRows( 0 );
        const auto expected = lines.size();
        auto sorted = sortedLogLines( *keys, request.order, std::move( lines ), cancel );
        if ( sorted.size() != expected ) {
            return std::nullopt;
        }
        result.rows = std::make_shared<const SortedRows>( std::move( sorted ) );
    }
    result.keys = std::move( keys );
    return result;
}

// ---------------------------------------------------------------------------
// On a worker thread
// ---------------------------------------------------------------------------

struct TableSorter::Job {
    uint64_t linesToRead = 0;
    std::atomic<bool> cancelled{ false };
    std::atomic<uint64_t> linesDone{ 0 };
    QFuture<std::optional<TableSortResult>> future;
};

TableSorter::TableSorter( QObject* parent )
    : QObject( parent )
{
}

TableSorter::~TableSorter()
{
    stopAndWait();
}

void TableSorter::start( TableSortRequest request )
{
    cancel();

    auto job = std::make_shared<Job>();
    const auto keyed = request.keys ? request.keys->size() : 0U;
    job->linesToRead = request.lineCount.get() > keyed ? request.lineCount.get() - keyed : 0;
    running_ = job;
    jobs_.push_back( job );

    auto* watcher = new QFutureWatcher<std::optional<TableSortResult>>( this );
    connect( watcher, &QFutureWatcher<std::optional<TableSortResult>>::finished, this,
             [ this, job, watcher ]() {
                 watcher->deleteLater();
                 onFinished( job );
             } );

    // The worker shares the job, so it outlives whatever the owner does.
    job->future = QtConcurrent::run(
        [ request = std::move( request ), job ]() -> std::optional<TableSortResult> {
            return sortTableRows( request, job->cancelled, job->linesDone );
        } );
    watcher->setFuture( job->future );
}

void TableSorter::cancel()
{
    if ( running_ ) {
        running_->cancelled.store( true );
        running_.reset();
    }
}

void TableSorter::stopAndWait()
{
    cancel();
    for ( const auto& job : jobs_ ) {
        job->cancelled.store( true );
    }
    for ( const auto& job : jobs_ ) {
        job->future.waitForFinished();
    }
}

bool TableSorter::isRunning() const
{
    return running_ != nullptr;
}

int TableSorter::progress() const
{
    if ( !running_ || running_->linesToRead == 0 ) {
        return 0;
    }
    return static_cast<int>(
        std::min<uint64_t>( 100, ( running_->linesDone.load() * 100 ) / running_->linesToRead ) );
}

void TableSorter::onFinished( const std::shared_ptr<Job>& job )
{
    jobs_.erase( std::remove( jobs_.begin(), jobs_.end(), job ), jobs_.end() );
    if ( job != running_ ) {
        return; // cancelled: stale
    }
    running_.reset();

    const auto result = job->future.result();
    if ( result ) {
        Q_EMIT finished( *result );
    }
}
