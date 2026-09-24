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

#include "valuecount.h"

#include <algorithm>
#include <memory>

#include <QFutureWatcher>
#include <QHash>
#include <QtConcurrent>

#include "abstractlogdata.h"
#include "logfieldextractor.h"
#include "logformatdefinition.h"

double ValueCountResult::sharePercent( uint64_t count ) const
{
    if ( linesCounted == 0 ) {
        return 0.0;
    }
    return 100.0 * static_cast<double>( count ) / static_cast<double>( linesCounted );
}

namespace {

// A copy of a Log Format with the extractor that refers to it.
struct FieldReader {
    explicit FieldReader( const LogFormatDefinition& f )
        : format( f )
        , extractor( format )
    {
    }
    FieldReader( const FieldReader& ) = delete;
    FieldReader& operator=( const FieldReader& ) = delete;

    LogFormatDefinition format;
    LogFieldExtractor extractor;
};

} // namespace

ValueOfLine fieldValueOf( const LogFormatDefinition& format, const QString& fieldName )
{
    auto reader = std::make_shared<FieldReader>( format );

    return [ reader, fieldName ]( const QString& line ) -> std::optional<QString> {
        return reader->extractor.extractField( line, fieldName );
    };
}

ValueOfLine captureGroupValueOf( const QRegularExpression& regexp, int group )
{
    return [ regexp, group ]( const QString& line ) -> std::optional<QString> {
        const auto match = regexp.match( line );
        if ( !match.hasMatch() || match.capturedStart( group ) < 0 ) {
            return std::nullopt;
        }
        return match.captured( group );
    };
}

int captureGroupCount( const QRegularExpression& regexp )
{
    return regexp.isValid() ? static_cast<int>( regexp.captureCount() ) : 0;
}

std::optional<ValueCountResult> countValues( const AbstractLogData& logData, LineNumber first,
                                             LinesCount count, const ValueOfLine& valueOf,
                                             size_t maxDistinctValues,
                                             const std::atomic<bool>& cancel,
                                             std::atomic<uint64_t>& linesDone )
{
    ValueCountResult result;
    QHash<QString, uint64_t> counts;

    constexpr uint64_t batchSize = 5000;
    const auto end = first.get() + count.get();
    for ( uint64_t start = first.get(); start < end; start += batchSize ) {
        if ( cancel.load() ) {
            return std::nullopt;
        }

        const auto batch = std::min( batchSize, end - start );
        const auto lines = logData.getExpandedLines( LineNumber( start ), LinesCount( batch ) );
        for ( const auto& line : lines ) {
            const auto value = valueOf( line );
            if ( !value ) {
                continue;
            }
            ++result.linesCounted;
            ++counts[ *value ];
            if ( static_cast<size_t>( counts.size() ) > maxDistinctValues ) {
                return ValueCountResult{ {}, 0, true };
            }
        }

        linesDone.store( start + batch - first.get() );
    }

    if ( cancel.load() ) {
        return std::nullopt;
    }

    result.entries.reserve( counts.size() );
    for ( auto it = counts.cbegin(); it != counts.cend(); ++it ) {
        result.entries.append( { it.key(), it.value() } );
    }
    std::sort( result.entries.begin(), result.entries.end(),
               []( const ValueCountEntry& a, const ValueCountEntry& b ) {
                   return a.count != b.count ? a.count > b.count : a.value < b.value;
               } );
    return result;
}

// ---------------------------------------------------------------------------
// On a worker thread
// ---------------------------------------------------------------------------

struct ValueCounter::Job {
    LinesCount count;
    std::atomic<bool> cancelled{ false };
    std::atomic<uint64_t> linesDone{ 0 };
    QFuture<std::optional<ValueCountResult>> future;
};

ValueCounter::ValueCounter( QObject* parent )
    : QObject( parent )
{
}

ValueCounter::~ValueCounter()
{
    for ( const auto& job : jobs_ ) {
        job->cancelled.store( true );
    }
    for ( const auto& job : jobs_ ) {
        job->future.waitForFinished();
    }
}

void ValueCounter::start( std::shared_ptr<const AbstractLogData> logData, ValueOfLine valueOf,
                          size_t maxDistinctValues )
{
    cancel();

    auto job = std::make_shared<Job>();
    job->count = logData->getNbLine();
    running_ = job;
    jobs_.push_back( job );

    auto* watcher = new QFutureWatcher<std::optional<ValueCountResult>>( this );
    connect( watcher, &QFutureWatcher<std::optional<ValueCountResult>>::finished, this,
             [ this, job, watcher ]() {
                 watcher->deleteLater();
                 onFinished( job );
             } );

    job->future = QtConcurrent::run(
        [ data = std::move( logData ), valueOf = std::move( valueOf ), maxDistinctValues,
          state = job.get() ]() mutable -> std::optional<ValueCountResult> {
            // Released here, before the result is reported: the owner, which
            // waits for that on destruction, keeps the last reference.
            const auto owned = std::move( data );
            return countValues( *owned, 0_lnum, state->count, valueOf, maxDistinctValues,
                                state->cancelled, state->linesDone );
        } );
    watcher->setFuture( job->future );
}

void ValueCounter::cancel()
{
    if ( running_ ) {
        running_->cancelled.store( true );
        running_.reset();
    }
}

bool ValueCounter::isRunning() const
{
    return running_ != nullptr;
}

int ValueCounter::progress() const
{
    if ( !running_ || running_->count.get() == 0 ) {
        return 0;
    }
    return static_cast<int>( ( running_->linesDone.load() * 100 ) / running_->count.get() );
}

void ValueCounter::onFinished( const std::shared_ptr<Job>& job )
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
