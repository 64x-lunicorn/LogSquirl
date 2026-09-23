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

#include "searchblocksource.h"

#include "textencoding.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Log Lines held in memory, handed to a Search as UTF-8 blocks: no file on
// disk and no indexing. Safe to use from the Search's threads while the test
// appends Log Lines between runs.
//
// Beyond serving Log Lines, a test can make it fail every block read from
// then on (failReading), or every one from the n-th read on
// (failReadingFromBlock), hold the Search's next block read, or its n-th one,
// until released (holdReading / holdReadingFromBlock / waitUntilReadingHeld /
// releaseReading), and see which blocks were read (readBlocks) and whether
// every run's reader was detached again (attachedReaders).
class InMemoryBlockSource final : public SearchBlockSource {
public:
    struct Block {
        LineNumber first;
        LinesCount number;
    };

    InMemoryBlockSource() = default;

    explicit InMemoryBlockSource( const QStringList& lines )
    {
        appendLines( lines );
    }

    void appendLines( const QStringList& lines )
    {
        std::lock_guard lock( mutex_ );
        for ( const auto& line : lines ) {
            const auto utf8 = line.toUtf8();
            lines_.emplace_back( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
        }
    }

    // The last Log Line was incomplete -- it was still being written when it
    // was last read -- and the rest of it has arrived now.
    void growLastLine( const QString& suffix )
    {
        std::lock_guard lock( mutex_ );
        const auto utf8 = suffix.toUtf8();
        lines_.back().append( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
    }

    void failReading( std::string message )
    {
        std::lock_guard lock( mutex_ );
        failure_ = std::move( message );
    }

    // The reads before, counted from 0 across every run, still succeed.
    void failReadingFromBlock( std::size_t blockIndex, std::string message )
    {
        std::lock_guard lock( mutex_ );
        failFromBlock_ = blockIndex;
        failure_ = std::move( message );
    }

    void holdReading()
    {
        std::lock_guard lock( mutex_ );
        held_ = true;
    }

    // The reads before, counted from 0 across every run, are not held.
    void holdReadingFromBlock( std::size_t blockIndex )
    {
        std::lock_guard lock( mutex_ );
        holdFromBlock_ = blockIndex;
    }

    // Blocks until a Search is waiting in a held block read.
    void waitUntilReadingHeld()
    {
        std::unique_lock lock( mutex_ );
        changed_.wait( lock, [ this ] { return readerHeld_; } );
    }

    void releaseReading()
    {
        {
            std::lock_guard lock( mutex_ );
            held_ = false;
            holdFromBlock_ = NoBlock;
        }
        changed_.notify_all();
    }

    std::vector<Block> readBlocks() const
    {
        std::lock_guard lock( mutex_ );
        return readBlocks_;
    }

    int attachedReaders() const
    {
        return attachedReaders_.load();
    }

    LinesCount getNbLines() const override
    {
        std::lock_guard lock( mutex_ );
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }

    RawLines getLinesRaw( LineNumber first, LinesCount number ) const override
    {
        std::unique_lock lock( mutex_ );

        if ( held_ || readBlocks_.size() >= holdFromBlock_ ) {
            readerHeld_ = true;
            changed_.notify_all();
            changed_.wait( lock, [ this ] { return !held_ && holdFromBlock_ == NoBlock; } );
            readerHeld_ = false;
        }

        readBlocks_.push_back( { first, number } );

        if ( !failure_.empty() && readBlocks_.size() > failFromBlock_ ) {
            throw std::runtime_error( failure_ );
        }

        RawLines rawLines;
        rawLines.startLine = first;
        if ( ( first + number ).get() > lines_.size() ) {
            return rawLines;
        }

        for ( auto index = first.get(); index < ( first + number ).get(); ++index ) {
            const auto& line = lines_[ static_cast<std::size_t>( index ) ];
            rawLines.buffer.insert( rawLines.buffer.end(), line.begin(), line.end() );
            rawLines.buffer.push_back( '\n' );
            rawLines.endOfLines.push_back( static_cast<qint64>( rawLines.buffer.size() ) );
        }

        const auto* codec = TextEncoding::forName( "UTF-8" );
        rawLines.textDecoder.decoder = codec->makeDecoder();
        rawLines.textDecoder.encodingParams = EncodingParameters( codec );
        return rawLines;
    }

    void attachReader() const override
    {
        ++attachedReaders_;
    }

    void detachReader() const override
    {
        --attachedReaders_;
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;

    std::vector<std::string> lines_;
    std::string failure_;
    std::size_t failFromBlock_ = 0;
    static constexpr std::size_t NoBlock = static_cast<std::size_t>( -1 );

    bool held_ = false;
    std::size_t holdFromBlock_ = NoBlock;
    mutable bool readerHeld_ = false;
    mutable std::vector<Block> readBlocks_;

    mutable std::atomic<int> attachedReaders_{ 0 };
};
