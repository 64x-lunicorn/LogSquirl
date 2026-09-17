/*
 * Copyright (C) 2009, 2010, 2014, 2015 Nicolas Bonnefon and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <qglobal.h>
#include <qthread.h>
#include <string_view>
#include <thread>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <tuple>
#include <utility>

#include <tbb/flow_graph.h>

#include "containers.h"
#include "encodingdetector.h"
#include "indexcache.h"
#include "indexedhash.h"
#include "indexingblocks.h"
#include "linepositionarray.h"
#include "linetypes.h"
#include "log.h"
#include "logdata.h"
#include "memory_info.h"
#include "progress.h"
#include "readablesize.h"
#include "runnable_lambda.h"

#include "logdataworker.h"

constexpr int IndexingBlockSize = IndexOperation::DefaultBlockSize;

IndexingData::IndexingData()
    : headerAndTailDigests_( IndexingBlockSize )
{
}

qint64 IndexingData::getIndexedSize() const
{
    return hash_.size;
}

IndexedHash IndexingData::getHash() const
{
    return hash_;
}

LineLength IndexingData::getMaxLength() const
{
    return maxLength_;
}

LinesCount IndexingData::getNbLines() const
{
    return LinesCount( std::visit( []( const auto& linePosition ) { return linePosition.size(); },
                                   linePosition_ ) );
}

OffsetInFile IndexingData::getEndOfLineOffset( LineNumber line ) const
{
    return std::visit(
        [ line ]( const auto& linePosition ) { return linePosition.at( line.get() ); },
        linePosition_ );
}

logsquirl::vector<OffsetInFile> IndexingData::getEndOfLineOffsets( LineNumber line,
                                                                   LinesCount count ) const
{
    return std::visit(
        [ line, count ]( const auto& linePosition ) { return linePosition.range( line, count ); },
        linePosition_ );
}

QTextCodec* IndexingData::getEncodingGuess() const
{
    return encodingGuess_;
}

void IndexingData::setEncodingGuess( QTextCodec* codec )
{
    encodingGuess_ = codec;
}

void IndexingData::forceEncoding( QTextCodec* codec )
{
    encodingForced_ = codec;
}

QTextCodec* IndexingData::getForcedEncoding() const
{
    return encodingForced_;
}

void IndexingData::addAll( qint64 blockSize, LineLength length,
                           const FastLinePositionArray& newLinePosition, QTextCodec* encoding,
                           std::optional<quint64> fullDigest )
{
    maxLength_ = std::max( maxLength_, length );
    std::visit(
        [ &newLinePosition ]( auto& linePosition ) { linePosition.append_list( newLinePosition ); },
        linePosition_ );

    hash_.size += blockSize;
    if ( fullDigest ) {
        hash_.fullDigest = *fullDigest;
    }

    encodingGuess_ = encoding;
}

IndexedBytesDigests IndexingData::takeDigests()
{
    auto full = useFastModificationDetection_
                    ? std::nullopt
                    : std::optional<FileDigest>( std::exchange( hashBuilder_, FileDigest{} ) );
    return IndexedBytesDigests{ .full = std::move( full ),
                                .headerAndTail
                                = std::exchange( headerAndTailDigests_,
                                                 HeaderAndTailDigests( IndexingBlockSize ) ) };
}

void IndexingData::returnDigests( IndexedBytesDigests&& digests )
{
    if ( digests.full ) {
        hashBuilder_ = std::move( *digests.full );
    }
    headerAndTailDigests_ = std::move( digests.headerAndTail );
}

int IndexingData::getProgress() const
{
    return progress_;
}

void IndexingData::setProgress( int progress )
{
    progress_ = progress;
}

void IndexingData::clear( const IndexingPolicy& policy )
{
    maxLength_ = 0_length;
    hash_ = {};
    hashBuilder_.reset();
    headerAndTailDigests_.reset();
    indexedModificationTime_ = {};
    if ( policy.useCompressedIndex ) {
        linePosition_ = LinePositionArrayType( LinePositionArray{} );
    }
    else {
        linePosition_ = LinePositionArrayType( FastLinePositionArray{} );
    }
    encodingGuess_ = nullptr;
    encodingForced_ = nullptr;

    progress_ = {};
    useFastModificationDetection_ = policy.fastModificationDetection;
}

void IndexingData::loadFromCache( LinePositionArray&& linePosition, LineLength maxLength,
                                  const IndexedHash& hash, QTextCodec* encoding,
                                  bool fastModificationDetection )
{
    useFastModificationDetection_ = fastModificationDetection;

    linePosition_ = std::move( linePosition );
    maxLength_ = maxLength;
    hash_ = hash;
    hashBuilder_.reset();
    // The cache checked the header and tail only, and took no digests to go
    // on from.
    headerAndTailDigests_.reset();
    indexedModificationTime_ = {};
    encodingGuess_ = encoding;
    encodingForced_ = nullptr;
    progress_ = 100;
}

void IndexingData::resumeFromCache( ResumedIndex&& resumed )
{
    useFastModificationDetection_ = resumed.fastModificationDetection;

    linePosition_ = std::move( resumed.linePosition );
    // The dropped last Log Line cannot have been longer than it is once it
    // is indexed again, so the longest line so far stays a lower bound.
    maxLength_ = resumed.maxLength;

    // The header and tail digests are taken again once indexing is done.
    hash_ = {};
    hash_.size = resumed.offset.get();
    headerAndTailDigests_.reset();
    indexedModificationTime_ = {};
    hashBuilder_ = std::move( resumed.digestBeforeOffset );
    if ( !useFastModificationDetection_ ) {
        hash_.fullDigest = hashBuilder_.digest();
    }

    encodingGuess_ = resumed.encoding;
    encodingForced_ = nullptr;
}

const LinePositionArray* IndexingData::getCompressedLinePosition() const
{
    if ( auto* p = std::get_if<LinePositionArray>( &linePosition_ ) ) {
        return p;
    }
    return nullptr;
}

size_t IndexingData::allocatedSize() const
{
    return std::visit( []( const auto& linePosition ) { return linePosition.allocatedSize(); },
                       linePosition_ );
}

LogDataWorker::LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data,
                              const IndexingPolicy& indexingPolicy )
    : indexingPolicy_( indexingPolicy )
    , indexing_data_( indexing_data )
{
    operationsPool_.setMaxThreadCount( 1 );
}

LogDataWorker::~LogDataWorker() noexcept
{
    try {
        // Signal all running operations to stop early
        interruptRequest_.set();

        // Remove pending runnables from the pool (thread-safe, no mutex needed)
        operationsPool_.clear();

        // Wait for the active runnable to finish WITHOUT holding operationsMutex_.
        // The pool thread needs to acquire operationsMutex_ before it can observe
        // the interrupt flag and exit. Holding the mutex here would deadlock.
        operationsPool_.waitForDone();

        LOG_INFO << "LogDataWorker shutdown";
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy LogDataWorker: " << e.what();
    }
}

void LogDataWorker::setIndexingPolicy( const IndexingPolicy& indexingPolicy )
{
    ScopedLock locker( operationsMutex_ );
    indexingPolicy_ = indexingPolicy;
}

void LogDataWorker::attachFile( const QString& fileName )
{
    ScopedLock locker( operationsMutex_ );
    interruptRequest_.clear();
    fileName_ = fileName;
}

void LogDataWorker::indexAll( QTextCodec* forcedEncoding )
{
    ScopedLock locker( operationsMutex_ );
    operationsPool_.waitForDone();
    interruptRequest_.clear();

    LOG_INFO << "FullIndex requested, forced encoding: "
             << ( forcedEncoding != nullptr ? forcedEncoding->name().toStdString()
                                            : std::string{ "none" } );
    QSemaphore operationStarted;
    operationsPool_.start(
        createRunnable( [ this, &operationStarted, forcedEncoding, fileName = fileName_,
                          indexingPolicy = indexingPolicy_ ] {
            LOG_INFO << "FullIndex thread started";
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<FullIndexOperation>(
                fileName, indexing_data_, interruptRequest_, indexingPolicy, forcedEncoding );
            return connectSignalsAndRun( operationRequested.get() );
        } ) );
    operationStarted.acquire();
}

void LogDataWorker::indexAdditionalLines()
{
    ScopedLock locker( operationsMutex_ );
    operationsPool_.waitForDone();
    interruptRequest_.clear();

    LOG_INFO << "PartialIndex requested";

    QSemaphore operationStarted;
    operationsPool_.start( createRunnable(
        [ this, &operationStarted, fileName = fileName_, indexingPolicy = indexingPolicy_ ] {
            QThread::currentThread()->setObjectName( "PartialIndex" );
            LOG_INFO << "PartialIndex thread started";
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<PartialIndexOperation>(
                fileName, indexing_data_, interruptRequest_, indexingPolicy );
            return connectSignalsAndRun( operationRequested.get() );
        } ) );
    operationStarted.acquire();
}

void LogDataWorker::checkFileChanges()
{
    ScopedLock locker( operationsMutex_ );
    operationsPool_.waitForDone();
    interruptRequest_.clear();

    LOG_INFO << "Check file changes requested";

    QSemaphore operationStarted;
    operationsPool_.start( createRunnable(
        [ this, &operationStarted, fileName = fileName_, indexingPolicy = indexingPolicy_ ] {
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<CheckFileChangesOperation>(
                fileName, indexing_data_, interruptRequest_, indexingPolicy );

            return connectSignalsAndRun( operationRequested.get() );
        } ) );
    operationStarted.acquire();
}

OperationResult LogDataWorker::connectSignalsAndRun( IndexOperation* operationRequested )
{
    connect( operationRequested, &IndexOperation::indexingProgressed, this,
             &LogDataWorker::indexingProgressed );

    connect( operationRequested, &IndexOperation::indexingFinished, this,
             &LogDataWorker::onIndexingFinished );

    connect( operationRequested, &IndexOperation::fileCheckFinished, this,
             &LogDataWorker::onCheckFileFinished );

    auto result = operationRequested->run();

    operationRequested->disconnect( this );

    return result;
}

void LogDataWorker::interrupt()
{
    LOG_INFO << "Load interrupt requested";
    interruptRequest_.set();
}

void LogDataWorker::onIndexingFinished( LoadingStatus status, const QString& failure )
{
    LOG_INFO << "indexing finished in worker thread, status " << static_cast<int>( status );
    Q_EMIT indexingFinished( status, failure );
}

void LogDataWorker::onCheckFileFinished( const MonitoredFileStatus result, const QString& failure )
{
    LOG_INFO << "checking file finished in worker thread";
    Q_EMIT checkFileChangesFinished( result, failure );
}

//
// Operations implementation
//
void IndexOperation::guessEncoding( const char* bytes, std::size_t size,
                                    IndexingState& state ) const
{
    if ( !state.encodingGuess ) {
        state.encodingGuess = EncodingDetector::getInstance().detectEncoding( bytes, size );
        LOG_INFO << "Encoding guess " << state.encodingGuess->name().toStdString();
    }

    if ( !state.fileTextCodec ) {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
        state.fileTextCodec = scopedAccessor.getForcedEncoding();

        if ( !state.fileTextCodec ) {
            state.fileTextCodec = scopedAccessor.getEncodingGuess();
        }

        if ( !state.fileTextCodec ) {
            state.fileTextCodec = state.encodingGuess;
        }
    }

    state.encodingParams = EncodingParameters( state.fileTextCodec );

    LOG_DEBUG << "Encoding " << state.fileTextCodec->name().toStdString() << ", Char width "
              << state.encodingParams.lineFeedWidth;
}

indexing_blocks::IndexingBlock*
IndexOperation::readNextBlock( QFile& file, indexing_blocks::BlockReading& reading,
                               indexing_blocks::IndexingBlockPool& pool, IndexingState& state,
                               std::chrono::microseconds& ioDuration )
{
    using namespace std::chrono;
    using namespace indexing_blocks;
    using clock = high_resolution_clock;

    if ( interruptRequest_ || ( reading.bytesAhead == 0 && file.atEnd() ) ) {
        return nullptr;
    }

    auto* block = pool.acquire();
    auto* bytes = block->bytes();

    // The bytes read past the block before, and those right before it, are
    // kept in the reading: the block's buffer has room around its bytes.
    std::copy_n( reading.behind.data(), reading.bytesBehind, bytes - reading.bytesBehind );
    std::copy_n( reading.ahead.data(), reading.bytesAhead, bytes );

    const auto ioStartTime = clock::now();
    const auto readBytes
        = file.read( bytes + reading.bytesAhead,
                     pool.blockSize() + MaxDelimiterNeighbours - reading.bytesAhead );

    if ( readBytes < 0 ) {
        LOG_ERROR << "Reading past the end of file";
        pool.release( block );
        return nullptr;
    }
    bytesIndexed_ += readBytes;
    ioDuration += duration_cast<microseconds>( clock::now() - ioStartTime );

    const auto available = std::int64_t{ reading.bytesAhead } + readBytes;
    block->size = std::min( available, pool.blockSize() );
    if ( block->size == 0 ) {
        pool.release( block );
        return nullptr;
    }
    block->bytesBefore = reading.bytesBehind;
    block->bytesAfter = static_cast<int>( available - block->size );
    block->sequence = reading.blocksRead++;
    block->beginning = reading.end;

    reading.end += block->size;
    reading.bytesAhead = block->bytesAfter;
    std::copy_n( bytes + block->size, reading.bytesAhead, reading.ahead.data() );
    reading.bytesBehind = static_cast<int>(
        std::min( std::int64_t{ MaxDelimiterNeighbours }, block->bytesBefore + block->size ) );
    std::copy_n( bytes + block->size - reading.bytesBehind, reading.bytesBehind,
                 reading.behind.data() );

    // Detected from the first block alone, before any block is parsed.
    if ( block->sequence == 0 ) {
        guessEncoding( bytes, static_cast<std::size_t>( block->size ), state );
    }
    block->encoding = state.encodingParams;

    LOG_DEBUG << "Read block " << block->beginning << " size " << block->size;
    return block;
}

void IndexOperation::indexNextBlock( IndexingState& state,
                                     const indexing_blocks::IndexingBlock& block )
{
    using namespace indexing_blocks;

    LOG_DEBUG << "Indexing block " << block.beginning << " start";

    // Stitching the block to those before it and hashing it take no index
    // lock: only publishing the parsed block below takes the exclusive one.
    OpenLogLine line{ .start = state.pos, .widening = state.additional_spaces };
    if ( const auto crossingLineLength = stitchBlock( block, line ) ) {
        state.max_length = std::max( state.max_length, *crossingLineLength );
    }
    state.max_length
        = std::max( { state.max_length, block.maxLength, openLineLength( block, line ) } );
    state.pos = line.start;
    state.additional_spaces = line.widening;

    std::optional<quint64> fullDigest;
    if ( state.digests ) {
        state.digests->headerAndTail.add( block.beginning, block.bytes(), block.size );
        if ( state.digests->full ) {
            fullDigest = state.digests->full
                             ->addData( block.bytes(), static_cast<std::size_t>( block.size ) )
                             .digest();
        }
    }

    // Update the caller for progress indication
    const auto progress
        = ( state.file_size > 0 ) ? calculateProgress( state.pos, state.file_size ) : 100;

    bool progressed = false;
    {
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        // Measured as a qsizetype, 64 bits wide in every build shipped: no
        // Log Line is too long to measure, so none is reported as such.
        scopedAccessor.addAll(
            block.size,
            LineLength( type_safe::narrow_cast<LineLength::UnderlyingType>( state.max_length ) ),
            block.endOfLines, state.encodingGuess, fullDigest );

        if ( progress != scopedAccessor.getProgress() ) {
            scopedAccessor.setProgress( progress );
            progressed = true;
        }
    }

    if ( progressed ) {
        LOG_DEBUG << "Indexing progress " << progress << ", indexed size " << state.pos;
        Q_EMIT indexingProgressed( progress );
    }

    LOG_DEBUG << "Indexing block " << block.beginning << " done";
}

namespace {

// A digest of the bytes of file in [offset, offset + size), or of those
// there are.
RangeDigest digestReadFrom( QFile& file, qint64 offset, qint64 size )
{
    QByteArray bytes( static_cast<qsizetype>( size ), Qt::Uninitialized );
    const auto readBytes = file.seek( offset ) ? file.read( bytes.data(), size ) : qint64{ 0 };
    FileDigest digest;
    digest.addData( bytes.constData(), static_cast<size_t>( std::max( readBytes, qint64{ 0 } ) ) );
    return { offset, std::max( readBytes, qint64{ 0 } ), digest.digest() };
}

} // namespace

// The header and tail digests of the bytes indexed up to end. Indexing
// appended bytes reads neither again (#277): the tail digest goes on from
// the bytes just indexed, and a header of a whole block cannot change by
// appending. Only what the digests taken while indexing do not cover, as
// after a cached Index was loaded, is read from the Log File again. Taken
// from the digests the run built, outside the index lock; the caller
// publishes them.
IndexOperation::HeaderAndTail IndexOperation::recordHeaderAndTail( QFile& file, qint64 end,
                                                                   HeaderAndTailDigests& digests,
                                                                   bool hasWholeBlockHeader ) const
{
    HeaderAndTail recorded;

    auto tail = digests.tail( end );
    if ( !tail ) {
        const auto range = HeaderAndTailDigests::tailRange( IndexingBlockSize, end );
        QByteArray bytes( static_cast<qsizetype>( range.size ), Qt::Uninitialized );
        const auto readBytes
            = file.seek( range.offset ) ? file.read( bytes.data(), range.size ) : qint64{ -1 };
        if ( readBytes == range.size ) {
            digests.add( range.offset, bytes.constData(), readBytes );
            tail = digests.tail( end );
        }
        if ( !tail ) {
            // The Log File is shorter than what was indexed: the digest of
            // what there is will not match when it is checked.
            tail = digestReadFrom( file, range.offset, range.size );
        }
    }
    recorded.tail = *tail;

    if ( hasWholeBlockHeader ) {
        return recorded;
    }
    recorded.header = digests.header( end );
    if ( !recorded.header ) {
        recorded.header = digestReadFrom( file, 0, std::min( end, qint64{ IndexingBlockSize } ) );
    }
    return recorded;
}

void IndexOperation::doIndex( OffsetInFile initialPosition )
{
    LOG_INFO << "Indexing file " << fileName_;
    QFile file( fileName_ );

    if ( !( file.isOpen() || file.open( QIODevice::ReadOnly ) ) ) {
        // TODO: Check that the file is seekable?
        // If the file cannot be open, we do as if it was empty
        LOG_WARNING << "Cannot open file " << fileName_.toStdString();

        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

        scopedAccessor.clear( indexingPolicy_ );
        scopedAccessor.setEncodingGuess( QTextCodec::codecForLocale() );

        scopedAccessor.setProgress( 100 );
        Q_EMIT indexingProgressed( 100 );
        return;
    }

    LOG_INFO << "File size " << file.size();

    IndexingState state;
    state.pos = initialPosition.get();
    state.file_size = file.size();
    bool hasWholeBlockHeader = false;

    {
        // Exclusive, as the digests are taken out: this run builds them on,
        // outside the lock, and hands them back once done.
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        state.digests = scopedAccessor.takeDigests();
        state.digests->headerAndTail.expectLogFileSize( state.file_size );
        hasWholeBlockHeader = scopedAccessor.getHash().headerSize == IndexingBlockSize;

        state.fileTextCodec = scopedAccessor.getForcedEncoding();
        if ( !state.fileTextCodec ) {
            state.fileTextCodec = scopedAccessor.getEncodingGuess();
        }

        state.encodingGuess = scopedAccessor.getEncodingGuess();
        LOG_INFO << "Initial encoding "
                 << ( state.fileTextCodec != nullptr ? state.fileTextCodec->name().toStdString()
                                                     : std::string{ "auto" } );
    }

    // From this run's own Indexing Policy: no settings object is read here
    // at all, so the options dialog writing from the UI thread while this
    // pass over the Log File is in flight cannot be observed by it (#94).
    // The read buffer is in MiB: it bounds the blocks read and not yet
    // stitched, and so the block buffers allocated (#290).
    const auto blocksInFlight
        = indexing_blocks::blocksInReadBuffer( indexingPolicy_.readBufferSizeMb, blockSize_ );

    LOG_INFO << "Prefetch buffer "
             << readableSize( static_cast<uint64_t>( blocksInFlight * blockSize_ ) ) << ", "
             << blocksInFlight << " blocks";

    using namespace std::chrono;
    using clock = high_resolution_clock;
    microseconds ioDuration{};

    const auto indexingStartTime = clock::now();

    // Declared before the graph, so that the blocks outlive every node.
    indexing_blocks::IndexingBlockPool blockPool( blockSize_ );
    indexing_blocks::BlockReading reading{ .end = state.pos };
    std::atomic<bool> readingDone{ false };

    using indexing_blocks::IndexingBlock;
    using Token = tbb::flow::continue_msg;
    // Integral, rather than a continue_msg: the decrementer of a continue_msg
    // only counts once every edge into it has sent one, and both reading and
    // stitching give tokens back.
    using TokensBack = std::int64_t;

    // Blocks are read one after the other, parsed each on its own in
    // parallel, and stitched to the Log Lines before them in file order
    // (#290). A token is let through the read buffer for every block read, so
    // that no more blocks than it holds are ever read and not yet stitched.
    //
    // The graph pulls its tokens from an input_node while this thread waits
    // in wait_for_all(), which makes this thread one of those running the
    // graph. Pushing blocks in from here instead, sleeping whenever the
    // limiter was full, only worked while TBB had a worker free for this
    // graph (#146; the same stall hit Search in #142). Once reading stops, at
    // the end of the file, on a read error or on an interrupt, the pass ends
    // as soon as the blocks already read have gone through the graph.
    tbb::flow::graph indexingGraph;

    const auto nextToken = [ &readingDone ]( tbb::flow_control& control ) {
        if ( readingDone ) {
            control.stop();
        }
        return Token{};
    };
    auto tokens = tbb::flow::input_node<Token>( indexingGraph, nextToken );

    auto readBuffer = tbb::flow::limiter_node<Token, TokensBack>(
        indexingGraph, static_cast<size_t>( blocksInFlight ) );

    file.seek( state.pos );

    using BlockReader
        = tbb::flow::multifunction_node<Token, std::tuple<IndexingBlock*, TokensBack>>;
    const auto readBlock = [ & ]( const Token&, BlockReader::output_ports_type& ports ) {
        auto* block
            = readingDone ? nullptr : readNextBlock( file, reading, blockPool, state, ioDuration );
        if ( block ) {
            std::get<0>( ports ).try_put( block );
        }
        else {
            // The token of a block not read goes back to the read buffer.
            readingDone = true;
            std::get<1>( ports ).try_put( TokensBack{ 1 } );
        }
    };
    auto blockReader = BlockReader( indexingGraph, tbb::flow::serial, readBlock );

    auto blockParser = tbb::flow::function_node<IndexingBlock*, IndexingBlock*>(
        indexingGraph, tbb::flow::unlimited, []( IndexingBlock* block ) {
            indexing_blocks::parseBlock( *block );
            return block;
        } );

    auto inFileOrder = tbb::flow::sequencer_node<IndexingBlock*>(
        indexingGraph, []( IndexingBlock* const& block ) { return block->sequence; } );

    auto blockStitcher = tbb::flow::function_node<IndexingBlock*, TokensBack>(
        indexingGraph, tbb::flow::serial, [ this, &state, &blockPool ]( IndexingBlock* block ) {
            indexNextBlock( state, *block );
            blockPool.release( block );
            return TokensBack{ 1 };
        } );

    tbb::flow::make_edge( tokens, readBuffer );
    tbb::flow::make_edge( readBuffer, blockReader );
    tbb::flow::make_edge( tbb::flow::output_port<0>( blockReader ), blockParser );
    tbb::flow::make_edge( tbb::flow::output_port<1>( blockReader ), readBuffer.decrementer() );
    tbb::flow::make_edge( blockParser, inFileOrder );
    tbb::flow::make_edge( inFileOrder, blockStitcher );
    tbb::flow::make_edge( blockStitcher, readBuffer.decrementer() );

    LOG_INFO << "Reading blocks";
    tokens.activate();
    indexingGraph.wait_for_all();
    LOG_INFO << "Reading blocks done";

    blockBuffersAllocated_ = blockPool.allocated();

    LOG_DEBUG << "Indexed up to " << state.pos;

    // The header and tail digests, and the modification time, are taken
    // before the index lock is taken to publish them: they may read the Log
    // File. What was indexed ends where the last block read ends: reading
    // looks a few bytes past it.
    const auto endFilePos = reading.end;
    const auto headerAndTail = recordHeaderAndTail( file, endFilePos, state.digests->headerAndTail,
                                                    hasWholeBlockHeader );

    // Only while nothing was appended since the last read: a check of a Log
    // File of the indexed size that still has this modification time need
    // not read all of it again.
    const QFileInfo indexedFile( fileName_ );
    const auto indexedModificationTime
        = indexedFile.size() == endFilePos ? indexedFile.lastModified() : QDateTime{};

    IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

    // Check if there is a non LF terminated line at the end of the file
    if ( !interruptRequest_ && state.file_size > state.pos ) {
        LOG_WARNING << "Non LF terminated file, adding a fake end of line";

        FastLinePositionArray line_position;
        line_position.append( OffsetInFile( state.file_size + 1 ) );
        line_position.setFakeFinalLF();

        scopedAccessor.addAll( 0, 0_length, line_position, state.encodingGuess, std::nullopt );
    }

    scopedAccessor.setTailHash( headerAndTail.tail.digest, headerAndTail.tail.offset,
                                headerAndTail.tail.size );
    if ( headerAndTail.header ) {
        scopedAccessor.setHeaderHash( headerAndTail.header->digest, headerAndTail.header->size );
    }
    scopedAccessor.setIndexedModificationTime( indexedModificationTime );
    scopedAccessor.returnDigests( std::move( *state.digests ) );

    const auto indexingEndTime = high_resolution_clock::now();
    const auto duration = duration_cast<microseconds>( indexingEndTime - indexingStartTime );

    LOG_INFO << "Indexing done, took " << duration << ", io " << ioDuration;
    LOG_INFO << "Index size "
             << readableSize( static_cast<uint64_t>( scopedAccessor.allocatedSize() ) );
    LOG_INFO << "Indexed lines " << scopedAccessor.getNbLines();
    LOG_INFO << "Max line " << scopedAccessor.getMaxLength();
    LOG_INFO << "Indexing perf "
             << ( 1000.f * 1000.f * static_cast<float>( state.file_size )
                  / static_cast<float>( duration.count() ) )
                    / ( 1024 * 1024 )
             << " MiB/s";
    LOG_INFO << "Memory usage " << readableSize( usedMemory() );

    if ( interruptRequest_ ) {
        scopedAccessor.clear( indexingPolicy_ );
    }

    if ( !scopedAccessor.getEncodingGuess() ) {
        scopedAccessor.setEncodingGuess( QTextCodec::codecForLocale() );
    }
}

namespace {

// The Index Cache as an Indexing Policy describes it. A Policy with the
// cache turned off yields a cache with no directory, which keeps nothing and
// finds nothing: that is the only place "off" is expressed.
IndexCache indexCacheFor( const IndexingPolicy& policy )
{
    return IndexCache{ policy.useIndexCache ? policy.indexCacheDirectory : QString{},
                       policy.indexCacheExcludedDirectory,
                       static_cast<qint64>( policy.cacheMaxSizeMb ) * 1024 * 1024 };
}

// The encoding a full re-index of the Log File detects: the one its first
// indexing block is taken for. Nothing when the file cannot be read.
QTextCodec* detectedEncodingOf( const QString& fileName, qint64 fileSize )
{
    QFile file( fileName );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return nullptr;
    }
    logsquirl::vector<char> block(
        static_cast<size_t>( std::min( fileSize, qint64{ IndexingBlockSize } ) ) );
    const auto readBytes = file.read( block.data(), logsquirl::ssize( block ) );
    if ( readBytes != logsquirl::ssize( block ) ) {
        return nullptr;
    }
    return EncodingDetector::getInstance().detectEncoding( block );
}

// The digest of the Log File's bytes before `end`, as indexing them would
// have built it. Nothing when reading fails or the run is interrupted.
std::optional<FileDigest> digestOfPrefix( const QString& fileName, OffsetInFile end,
                                          const AtomicFlag& interruptRequest )
{
    QFile file( fileName );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return std::nullopt;
    }
    FileDigest digest;
    QByteArray buffer( IndexingBlockSize, Qt::Uninitialized );
    for ( qint64 remaining = end.get(); remaining > 0; ) {
        if ( interruptRequest ) {
            return std::nullopt;
        }
        const auto readBytes
            = file.read( buffer.data(), std::min( remaining, qint64{ buffer.size() } ) );
        if ( readBytes <= 0 ) {
            return std::nullopt;
        }
        digest.addData( buffer.data(), static_cast<size_t>( readBytes ) );
        remaining -= readBytes;
    }
    return digest;
}

} // namespace

bool FullIndexOperation::resumeFrom( CachedIndex& cached, qint64 fileSize )
{
    auto& linePosition = cached.linePosition;
    const auto lines = linePosition.size().get();
    if ( lines == 0 || cached.hash.size >= fileSize ) {
        return false;
    }

    // Going on from a cached Index only keeps its line positions right if
    // the appended bytes are read with the encoding they were read with.
    auto* codec = QTextCodec::codecForName( cached.encodingName );
    if ( !codec || ( forcedEncoding_ && forcedEncoding_->name() != codec->name() ) ) {
        return false;
    }

    // A full re-index detects the encoding from the first indexing block.
    // Unless the Index was built from at least that block, what was appended
    // since may lead to another encoding.
    if ( cached.hash.size < IndexingBlockSize ) {
        const auto* detected = detectedEncodingOf( fileName_, fileSize );
        if ( !detected || detected->name() != codec->name() ) {
            LOG_INFO << "Encoding of " << fileName_ << " changed as it grew, indexing it again";
            return false;
        }
    }

    // The last cached Log Line may have had no newline yet, and continued
    // since: indexing goes on from where it starts.
    const auto resumeOffset = lines > 1 ? linePosition.at( lines - 2 ) : 0_offset;
    if ( resumeOffset.get() > cached.hash.size ) {
        return false;
    }

    FileDigest digestBeforeResumeOffset;
    if ( !indexingPolicy_.fastModificationDetection ) {
        auto digest = digestOfPrefix( fileName_, resumeOffset, interruptRequest_ );
        if ( !digest ) {
            return false;
        }
        digestBeforeResumeOffset = std::move( *digest );
    }

    LOG_INFO << "Resuming cached index for " << fileName_ << " at " << resumeOffset.get() << " of "
             << fileSize << " bytes";

    linePosition.pop_back();
    const auto progress = calculateProgress( resumeOffset.get(), fileSize );
    {
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        scopedAccessor.resumeFromCache( ResumedIndex{
            .linePosition = std::move( linePosition ),
            .maxLength = cached.maxLength,
            .offset = resumeOffset,
            .digestBeforeOffset = std::move( digestBeforeResumeOffset ),
            .encoding = codec,
            .fastModificationDetection = indexingPolicy_.fastModificationDetection } );
        scopedAccessor.forceEncoding( forcedEncoding_ );
        scopedAccessor.setProgress( progress );
    }
    Q_EMIT indexingProgressed( progress );

    return true;
}

OperationResult IndexOperation::run()
{
    try {
        return doRun();
    } catch ( const std::exception& err ) {
        const auto failure
            = QString( "%1 failed: %2" ).arg( metaObject()->className(), err.what() );
        LOG_ERROR << failure;
        return reportFailure( failure );
    }
}

OperationResult IndexOperation::reportFailure( const QString& failure )
{
    {
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        scopedAccessor.clear( indexingPolicy_ );
    }

    Q_EMIT indexingFinished( LoadingStatus::Failed, failure );
    return false;
}

// Called in the worker thread's context
OperationResult FullIndexOperation::doRun()
{
    LOG_INFO << "FullIndexOperation::run(), file " << fileName_.toStdString();

    // From this run's own Indexing Policy, so the cache asked for an
    // Index here and the one handed the new Index afterwards are
    // necessarily the same run's.
    const auto indexCache = indexCacheFor( indexingPolicy_ );

    // The cache hands out an Index only while the bytes it was built from
    // are unchanged, complete for the size it was built at. Whether that
    // is all of the Log File, or it has grown since, is decided here.
    auto cached = indexCache.tryLoad( fileName_ );
    const auto fileSize = cached ? QFileInfo( fileName_ ).size() : qint64{ 0 };

    if ( cached && cached->hash.size == fileSize ) {
        LOG_INFO << "Using cached index for " << fileName_;

        auto* codec = QTextCodec::codecForName( cached->encodingName );
        if ( !codec ) {
            codec = QTextCodec::codecForLocale();
        }

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.loadFromCache( std::move( cached->linePosition ), cached->maxLength,
                                          cached->hash, codec,
                                          indexingPolicy_.fastModificationDetection );
            if ( forcedEncoding_ ) {
                scopedAccessor.forceEncoding( forcedEncoding_ );
            }
        }

        Q_EMIT indexingProgressed( 100 );
        Q_EMIT indexingFinished( LoadingStatus::Successful, {} );
        return true;
    }

    if ( cached && resumeFrom( *cached, fileSize ) ) {
        // Read into a local first: an accessor held for the duration of
        // doIndex() would keep indexing from taking its own.
        const auto resumeOffset
            = IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize();
        doIndex( OffsetInFile( resumeOffset ) );
    }
    else if ( cached && interruptRequest_ ) {
        // Checking the cached Index was interrupted, which is not a sign
        // it cannot be gone on from: stop, rather than throw away what
        // there is and start indexing the whole Log File over.
        LOG_INFO << "FullIndexOperation: interrupted while checking the cached index of "
                 << fileName_;
        Q_EMIT indexingFinished( LoadingStatus::Interrupted, {} );
        return false;
    }
    else {
        Q_EMIT indexingProgressed( 0 );
        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear( indexingPolicy_ );
            scopedAccessor.forceEncoding( forcedEncoding_ );
        }

        doIndex( 0_offset );
    }

    LOG_INFO << "FullIndexOperation: ... finished, interrupt = "
             << static_cast<bool>( interruptRequest_ );

    const auto result = interruptRequest_ ? false : true;

    // The cache decides for itself whether it keeps this Index.
    if ( result ) {
        IndexingData::ConstAccessor accessor{ indexing_data_.get() };
        if ( const auto* linePos = accessor.getCompressedLinePosition() ) {
            const auto* codec = accessor.getEncodingGuess();
            const auto encodingName = codec ? codec->name() : QByteArray( "UTF-8" );

            indexCache.trySave( fileName_, *linePos, accessor.getMaxLength(), accessor.getHash(),
                                encodingName, linePos->hasFakeFinalLF() );
        }
    }

    Q_EMIT indexingFinished( result ? LoadingStatus::Successful : LoadingStatus::Interrupted, {} );
    return result;
}

OperationResult PartialIndexOperation::doRun()
{
    LOG_INFO << "PartialIndexOperation::run(), file " << fileName_.toStdString();

    const auto initialPosition
        = OffsetInFile( IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize() );

    LOG_INFO << "PartialIndexOperation: Starting the count at " << initialPosition << " ...";

    Q_EMIT indexingProgressed( 0 );

    doIndex( initialPosition );

    LOG_INFO << "PartialIndexOperation: ... finished counting.";

    const auto result = interruptRequest_ ? false : true;
    Q_EMIT indexingFinished( result ? LoadingStatus::Successful : LoadingStatus::Interrupted, {} );
    return result;
}

OperationResult CheckFileChangesOperation::doRun()
{
    LOG_INFO << "CheckFileChangesOperation::run(), file " << fileName_.toStdString();
    const auto result = doCheckFileChanges();
    Q_EMIT fileCheckFinished( result, {} );
    return result;
}

// What changed cannot be told when checking failed, so the Log File is
// taken as truncated: it is indexed again from the start.
OperationResult CheckFileChangesOperation::reportFailure( const QString& failure )
{
    Q_EMIT fileCheckFinished( MonitoredFileStatus::Truncated, failure );
    return MonitoredFileStatus::Truncated;
}

MonitoredFileStatus CheckFileChangesOperation::doCheckFileChanges()
{
    IndexedHash indexedHash;
    QDateTime indexedModificationTime;
    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
        indexedHash = scopedAccessor.getHash();
        indexedModificationTime = scopedAccessor.getIndexedModificationTime();
    }

    // Taken before the Log File is read, so a change made while it is read
    // is not taken for checked.
    const auto modificationTime = QFileInfo( fileName_ ).lastModified();

    // Without fast modification detection, a Log File that grew is still
    // told from its header and tail, and one that did not is read end to end
    // only when it was modified since it was last indexed or checked:
    // following a Log File that grows by small appends does not read all of
    // it for every change (#277).
    auto coverage = DigestCoverage::FullUnlessGrown;
    if ( indexingPolicy_.fastModificationDetection
         || ( indexedModificationTime.isValid() && modificationTime == indexedModificationTime ) ) {
        coverage = DigestCoverage::HeaderAndTail;
    }

    switch ( indexFit( indexedHash, fileName_, coverage ) ) {
    case IndexFit::Unchanged:
        LOG_INFO << "No change in file";
        if ( coverage == DigestCoverage::FullUnlessGrown ) {
            IndexingData::MutateAccessor{ indexing_data_.get() }.setIndexedModificationTime(
                modificationTime );
        }
        return MonitoredFileStatus::Unchanged;
    case IndexFit::Grown:
        LOG_INFO << "New data on disk";
        return MonitoredFileStatus::DataAdded;
    case IndexFit::Changed:
        LOG_INFO << "File truncated or changed in indexed range";
        return MonitoredFileStatus::Truncated;
    case IndexFit::LogFileUnreadable:
        LOG_INFO << "File failed to open";
        return MonitoredFileStatus::Truncated;
    }
    return MonitoredFileStatus::Truncated;
}
