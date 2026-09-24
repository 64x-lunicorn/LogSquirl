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

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <QStringList>

#include "abstractlogdata.h"

namespace testing_support {

using namespace std::chrono_literals;

// A Log File held in memory that the extraction's worker thread may read while
// the test changes it, and whose next read can be held until the test lets it
// go on.
class GrowingLogData : public AbstractLogData {
public:
    explicit GrowingLogData( QStringList lines )
        : lines_( std::move( lines ) )
    {
    }

    void setLines( const QStringList& lines )
    {
        const std::scoped_lock lock{ mutex_ };
        lines_ = lines;
    }

    // The next read waits until release() is called; it gives up after a
    // while, so that a test that would block forever fails instead.
    void holdNextRead()
    {
        const std::scoped_lock lock{ mutex_ };
        holdNextRead_ = true;
        released_ = false;
    }

    void release()
    {
        // Notified under the lock: the waiting worker may hold the last
        // reference to this object, and must not be able to destroy the
        // condition variable while release() is still inside notify_all().
        const std::scoped_lock lock{ mutex_ };
        released_ = true;
        condition_.notify_all();
    }

    bool waitUntilHeld()
    {
        std::unique_lock lock{ mutex_ };
        return condition_.wait_for( lock, 10s, [ this ] { return held_; } );
    }

    // The first Log Line of every block read, in the order read.
    std::vector<uint64_t> firstLinesRead() const
    {
        const std::scoped_lock lock{ mutex_ };
        return firstLinesRead_;
    }

    void forgetReads()
    {
        const std::scoped_lock lock{ mutex_ };
        firstLinesRead_.clear();
    }

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        const std::scoped_lock lock{ mutex_ };
        return lineUnlocked( line );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        std::unique_lock lock{ mutex_ };
        if ( holdNextRead_ ) {
            holdNextRead_ = false;
            held_ = true;
            condition_.notify_all();
            condition_.wait_for( lock, 10s, [ this ] { return released_; } );
            held_ = false;
        }
        firstLinesRead_.push_back( first.get() );
        logsquirl::vector<QString> result;
        for ( uint64_t i = 0;
              i < count.get() && first.get() + i < static_cast<uint64_t>( lines_.size() ); ++i ) {
            result.push_back( lineUnlocked( LineNumber( first.get() + i ) ) );
        }
        return result;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        return doGetLines( first, count );
    }
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        const std::scoped_lock lock{ mutex_ };
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( 0 );
    }
    LineLength doGetLineLength( LineNumber ) const override
    {
        return LineLength( 0 );
    }
    void doSetDisplayEncoding( const char* ) override {}
    const TextEncoding* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}

private:
    QString lineUnlocked( LineNumber line ) const
    {
        return line.get() < static_cast<uint64_t>( lines_.size() )
                   ? lines_[ static_cast<qsizetype>( line.get() ) ]
                   : QString{};
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    QStringList lines_;
    mutable bool holdNextRead_ = false;
    mutable bool held_ = false;
    bool released_ = true;
    mutable std::vector<uint64_t> firstLinesRead_;
};

} // namespace testing_support
