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

#include "fileholder.h"
#include "indexoperation.h"
#include "linepositionarray.h"
#include "loglinereader.h"
#include "searchblocksource.h"
#include "settingspolicies.h"

#include "textencoding.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

    // Every Log Line reads anew: the Log File was cut short and written again,
    // or reads differently now (another Encoding, another Decoding Policy).
    void replaceLines( const QStringList& lines )
    {
        {
            std::lock_guard lock( mutex_ );
            lines_.clear();
        }
        appendLines( lines );
    }

    // The last Log Line was incomplete -- it was still being written when it
    // was last read -- and the rest of it has arrived now.
    void growLastLine( const QString& suffix )
    {
        std::lock_guard lock( mutex_ );
        const auto utf8 = suffix.toUtf8();
        lines_.back().append( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
    }

    // A block holds no more Log Lines than fit in maxBytes, the first always,
    // as the log data bounds a Search's block by its bytes (#825).
    void limitBlockBytes( qint64 maxBytes )
    {
        std::lock_guard lock( mutex_ );
        maxBlockBytes_ = maxBytes;
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
            if ( index > first.get()
                 && static_cast<qint64>( rawLines.buffer.size() + line.size() + 1 )
                        > maxBlockBytes_ ) {
                break;
            }
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
    qint64 maxBlockBytes_ = std::numeric_limits<qint64>::max();
    static constexpr std::size_t NoBlock = static_cast<std::size_t>( -1 );

    bool held_ = false;
    std::size_t holdFromBlock_ = NoBlock;
    mutable bool readerHeld_ = false;
    mutable std::vector<Block> readBlocks_;

    mutable std::atomic<int> attachedReaders_{ 0 };
};

// A Log File held in memory, its bytes and the Index of its Log Lines, for a
// Log Line Reader to read (loglinereader.h): no file on disk and no indexing.
// A Log Line ends after each line feed in the Log File's Encoding.
//
// A Log Line too long to read (#825) is held without its bytes: the Index
// says where it lies, and reading any of its bytes is recorded
// (readTooLongBytes), as is every read of the bytes (reads). Held through a
// shared handle, as the Log File of log data is.
class InMemoryLogFile final : public LogFileBytes,
                              public std::enable_shared_from_this<InMemoryLogFile> {
public:
    struct Read {
        qint64 offset;
        qint64 size;
    };

    explicit InMemoryLogFile( const char* encoding = "UTF-8" )
        : encoding_( TextEncoding::forName( encoding ) )
        , encodingParams_( encoding_ )
    {
        if ( encoding_ == nullptr ) {
            throw std::invalid_argument( "unknown Encoding" );
        }
        IndexingData::MutateAccessor{ index_.get() }.clear( IndexingPolicy{} );
    }

    // Bytes as they are in the Log File, whole code units of its Encoding.
    void appendBytes( std::string_view bytes )
    {
        const auto start = size_;
        pieces_.push_back( Piece{ .offset = start, .bytes = std::string( bytes ), .holeSize = 0 } );
        size_ += static_cast<qint64>( bytes.size() );

        const auto lineFeed = encoding_->fromUnicode( QStringLiteral( "\n" ) );
        const auto width = static_cast<std::size_t>( lineFeed.size() );
        FastLinePositionArray lineEnds;
        for ( std::size_t at = 0; at + width <= bytes.size(); at += width ) {
            if ( bytes.substr( at, width ) == std::string_view( lineFeed.constData(), width ) ) {
                lineEnds.append( OffsetInFile( start + static_cast<qint64>( at + width ) ) );
            }
        }
        index( lineEnds );
    }

    // A Log Line's text in the Log File's Encoding, and its line feed.
    void appendLine( const QString& text )
    {
        const auto bytes = encoding_->fromUnicode( text + QChar::LineFeed );
        appendBytes(
            std::string_view( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ) );
    }

    // A Log Line of length bytes without its line feed, whose bytes are not
    // held, and its line feed.
    void appendTooLongLine( qint64 length )
    {
        pieces_.push_back( Piece{ .offset = size_, .bytes = {}, .holeSize = length } );
        size_ += length;
        const auto lineFeed = encoding_->fromUnicode( QStringLiteral( "\n" ) );
        pieces_.push_back(
            Piece{ .offset = size_, .bytes = lineFeed.toStdString(), .holeSize = 0 } );
        size_ += lineFeed.size();
        FastLinePositionArray lineEnds;
        lineEnds.append( OffsetInFile( size_ ) );
        index( lineEnds );
    }

    void setDecodingPolicy( const DecodingPolicy& decodingPolicy )
    {
        decodingPolicy_ = decodingPolicy;
    }

    // What a read of its Log Lines holds of it, as log data hands it out.
    ReadHandles readHandles()
    {
        return ReadHandles{ .index = index_,
                            .file = shared_from_this(),
                            .decoder = TextDecoder{ encoding_->makeDecoder(), encodingParams_ },
                            .decodingPolicy = decodingPolicy_ };
    }

    LogLineReader reader()
    {
        return LogLineReader( readHandles() );
    }

    std::vector<Read> reads() const
    {
        std::lock_guard lock( mutex_ );
        return reads_;
    }

    bool readTooLongBytes() const
    {
        std::lock_guard lock( mutex_ );
        return readTooLongBytes_;
    }

    int openReads() const
    {
        std::lock_guard lock( mutex_ );
        return openReads_;
    }

    void beginRead() override
    {
        std::lock_guard lock( mutex_ );
        ++openReads_;
    }

    void endRead() override
    {
        std::lock_guard lock( mutex_ );
        --openReads_;
    }

    qint64 readAt( qint64 offset, char* out, qint64 size ) override
    {
        std::lock_guard lock( mutex_ );
        if ( openReads_ <= 0 ) {
            throw std::logic_error( "bytes read outside of a read" );
        }
        reads_.push_back( Read{ offset, size } );
        if ( offset < 0 || offset > size_ ) {
            return -1;
        }
        const auto end = std::min( offset + size, size_ );
        for ( const auto& piece : pieces_ ) {
            const auto pieceSize
                = piece.holeSize > 0 ? piece.holeSize : static_cast<qint64>( piece.bytes.size() );
            const auto from = std::max( offset, piece.offset );
            const auto to = std::min( end, piece.offset + pieceSize );
            if ( from >= to ) {
                continue;
            }
            if ( piece.holeSize > 0 ) {
                readTooLongBytes_ = true;
                std::fill( out + ( from - offset ), out + ( to - offset ), 'x' );
            }
            else {
                std::copy_n( piece.bytes.data() + ( from - piece.offset ), to - from,
                             out + ( from - offset ) );
            }
        }
        return end - offset;
    }

private:
    struct Piece {
        qint64 offset = 0;
        std::string bytes;
        qint64 holeSize = 0;
    };

    void index( const FastLinePositionArray& lineEnds )
    {
        IndexingData::MutateAccessor{ index_.get() }.addAll( 0, 0_length, lineEnds, encoding_,
                                                             std::nullopt );
    }

    const TextEncoding* encoding_;
    EncodingParameters encodingParams_;
    DecodingPolicy decodingPolicy_;
    const std::shared_ptr<IndexingData> index_ = std::make_shared<IndexingData>();

    std::vector<Piece> pieces_;
    qint64 size_ = 0;

    mutable std::mutex mutex_;
    std::vector<Read> reads_;
    bool readTooLongBytes_ = false;
    int openReads_ = 0;
};
