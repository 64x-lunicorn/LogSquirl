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
#include <QMessageBox>
#include <QSemaphore>
#include <tuple>

#include <tbb/flow_graph.h>

#include "containers.h"
#include "dispatch_to.h"
#include "encodingdetector.h"
#include "indexcache.h"
#include "indexedhash.h"
#include "issuereporter.h"
#include "linepositionarray.h"
#include "linetypes.h"
#include "log.h"
#include "logdata.h"
#include "memory_info.h"
#include "progress.h"
#include "readablesize.h"
#include "runnable_lambda.h"

#include "logdataworker.h"

constexpr int IndexingBlockSize = 5 * 1024 * 1024;

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

void IndexingData::addAll( const logsquirl::vector<char>& block, LineLength length,
                           const FastLinePositionArray& newLinePosition, QTextCodec* encoding )

{
    maxLength_ = std::max( maxLength_, length );
    std::visit(
        [ &newLinePosition ]( auto& linePosition ) { linePosition.append_list( newLinePosition ); },
        linePosition_ );

    if ( !block.empty() ) {
        hash_.size += logsquirl::ssize( block );

        if ( !useFastModificationDetection_ ) {
            hashBuilder_.addData( block.data(), block.size() );
            hash_.fullDigest = hashBuilder_.digest();
        }
    }

    encodingGuess_ = encoding;
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

void LogDataWorker::onIndexingFinished( bool result )
{
    if ( result ) {
        LOG_INFO << "finished indexing in worker thread";
        Q_EMIT indexingFinished( LoadingStatus::Successful );
    }
    else {
        LOG_INFO << "indexing interrupted in worker thread";
        Q_EMIT indexingFinished( LoadingStatus::Interrupted );
    }
}

void LogDataWorker::onCheckFileFinished( const MonitoredFileStatus result )
{
    LOG_INFO << "checking file finished in worker thread";
    Q_EMIT checkFileChangesFinished( result );
}

//
// Operations implementation
//
namespace parse_data_block {

std::string_view::size_type findNextMultiByteDelimeter( EncodingParameters encodingParams,
                                                        std::string_view data, char delimeter )
{
    auto nextDelimeter = data.find( delimeter );

    if ( nextDelimeter == std::string_view::npos ) {
        return nextDelimeter;
    }

    const auto isNotDelimeter = [ &encodingParams, data ]( std::string_view::size_type checkPos ) {
        const auto lineFeedWidth
            = static_cast<std::string_view::size_type>( encodingParams.lineFeedWidth );

        const auto isCheckForward = encodingParams.lineFeedIndex == 0;

        if ( isCheckForward && checkPos + lineFeedWidth > data.size() ) {
            return true;
        }
        else if ( !isCheckForward && checkPos < lineFeedWidth - 1 ) {
            return true;
        }

        for ( auto i = 1u; i < lineFeedWidth; ++i ) {
            const auto nextByte = isCheckForward ? data[ checkPos + i ] : data[ checkPos - i ];
            if ( nextByte != '\0' ) {
                return true;
            }
        }

        return false;
    };

    while ( nextDelimeter != std::string_view::npos && isNotDelimeter( nextDelimeter ) ) {
        nextDelimeter = data.find( delimeter, nextDelimeter + 1 );
    }

    return nextDelimeter;
}

std::string_view::size_type findNextSingleByteDelimeter( EncodingParameters, std::string_view data,
                                                         char delimeter )
{
    return data.find( delimeter );
}

int charOffsetWithinBlock( const char* blockStart, const char* pointer,
                           const EncodingParameters& encodingParams )
{
    return type_safe::narrow_cast<int>( std::distance( blockStart, pointer ) )
           - encodingParams.getBeforeCrOffset();
}

using FindDelimeter = std::string_view::size_type ( * )( EncodingParameters encodingParams,
                                                         std::string_view, char );

LineLength::UnderlyingType
expandTabsInLine( const logsquirl::vector<char>& block, std::string_view blockToExpand,
                  int posWithinBlock, EncodingParameters encodingParams,
                  FindDelimeter findNextDelimeter,
                  LineLength::UnderlyingType initialAdditionalSpaces = 0 )
{
    auto additionalSpaces = initialAdditionalSpaces;
    while ( !blockToExpand.empty() ) {
        const auto nextTab = findNextDelimeter( encodingParams, blockToExpand, '\t' );
        if ( nextTab == std::string_view::npos ) {
            break;
        }

        const auto tabPosWithinBlock
            = charOffsetWithinBlock( block.data(), blockToExpand.data() + nextTab, encodingParams );

        LOG_DEBUG << "Tab at " << tabPosWithinBlock;

        const auto currentExpandedSize = tabPosWithinBlock - posWithinBlock + additionalSpaces;

        additionalSpaces += TabStop - ( currentExpandedSize % TabStop ) - 1;
        if ( nextTab >= blockToExpand.size() ) {
            break;
        }

        blockToExpand.remove_prefix( nextTab + 1 );
    }

    return additionalSpaces;
}

std::tuple<bool, int, LineLength::UnderlyingType>
findNextLineFeed( const logsquirl::vector<char>& block, int posWithinBlock,
                  const IndexingState& state, FindDelimeter findNextDelimeter )
{
    const auto searchStart = block.data() + posWithinBlock;
    const auto searchLineSize = static_cast<size_t>( logsquirl::ssize( block ) - posWithinBlock );

    const auto blockView = std::string_view( searchStart, searchLineSize );
    const auto nextLineFeed = findNextDelimeter( state.encodingParams, blockView, '\n' );

    const auto isEndOfBlock = nextLineFeed == std::string_view::npos;
    const auto nextLineSize = !isEndOfBlock ? nextLineFeed : searchLineSize;

    posWithinBlock
        = charOffsetWithinBlock( block.data(), searchStart + nextLineSize, state.encodingParams );

    const auto additionalSpaces
        = expandTabsInLine( block, blockView.substr( 0, nextLineSize ), posWithinBlock,
                            state.encodingParams, findNextDelimeter, state.additional_spaces );

    return std::make_tuple( isEndOfBlock, posWithinBlock, additionalSpaces );
}
} // namespace parse_data_block

FastLinePositionArray IndexOperation::parseDataBlock( OffsetInFile::UnderlyingType blockBeginning,
                                                      const logsquirl::vector<char>& block,
                                                      IndexingState& state ) const
{
    using namespace parse_data_block;

    FindDelimeter findNextDelimeter;
    if ( state.encodingParams.lineFeedWidth == 1 ) {
        findNextDelimeter = findNextSingleByteDelimeter;
    }
    else {
        findNextDelimeter = findNextMultiByteDelimeter;
    }

    bool isEndOfBlock = false;
    FastLinePositionArray linePositions;

    while ( !isEndOfBlock ) {
        if ( state.pos > blockBeginning + logsquirl::ssize( block ) ) {
            LOG_ERROR << "Trying to parse out of block: " << state.pos << " " << blockBeginning
                      << " " << block.size();
            break;
        }

        auto posWithinBlock = type_safe::narrow_cast<int>(
            state.pos >= blockBeginning ? ( state.pos - blockBeginning ) : 0 );

        isEndOfBlock = posWithinBlock == logsquirl::ssize( block );

        if ( !isEndOfBlock ) {
            std::tie( isEndOfBlock, posWithinBlock, state.additional_spaces )
                = findNextLineFeed( block, posWithinBlock, state, findNextDelimeter );
        }

        const auto currentDataEnd = posWithinBlock + blockBeginning;

        const auto length
            = type_safe::narrow_cast<LineLength::UnderlyingType>( currentDataEnd - state.pos )
                  / state.encodingParams.lineFeedWidth
              + state.additional_spaces;

        state.max_length = std::max( state.max_length, length );

        if ( !isEndOfBlock ) {
            state.end = currentDataEnd;
            state.pos = state.end + state.encodingParams.lineFeedWidth;
            state.additional_spaces = 0;
            linePositions.append( OffsetInFile( state.pos ) );
        }
    }

    return linePositions;
}

void IndexOperation::guessEncoding( const logsquirl::vector<char>& block,
                                    IndexingData::MutateAccessor& scopedAccessor,
                                    IndexingState& state ) const
{
    if ( !state.encodingGuess ) {
        state.encodingGuess = EncodingDetector::getInstance().detectEncoding( block );
        LOG_INFO << "Encoding guess " << state.encodingGuess->name().toStdString();
    }

    if ( !state.fileTextCodec ) {
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

std::optional<IndexOperation::BlockData>
IndexOperation::readNextBlock( QFile& file, std::chrono::microseconds& ioDuration )
{
    using namespace std::chrono;
    using clock = high_resolution_clock;

    if ( interruptRequest_ || file.atEnd() ) {
        return std::nullopt;
    }

    BlockData blockData{ file.pos(), new BlockBuffer( IndexingBlockSize ) };

    const auto ioStartTime = clock::now();
    const auto readBytes
        = file.read( blockData.second->data(), logsquirl::ssize( *blockData.second ) );

    if ( readBytes < 0 ) {
        LOG_ERROR << "Reading past the end of file";
        // The buffer never reaches the graph, whose consumer would otherwise
        // free it; release it here so it does not leak.
        delete blockData.second;
        return std::nullopt;
    }

    if ( readBytes < logsquirl::ssize( *blockData.second ) ) {
        blockData.second->resize( static_cast<size_t>( readBytes ) );
    }
    bytesIndexed_ += readBytes;

    ioDuration += duration_cast<microseconds>( clock::now() - ioStartTime );

    LOG_DEBUG << "Read block " << blockData.first << " size " << blockData.second->size();
    return blockData;
}

void IndexOperation::indexNextBlock( IndexingState& state, const BlockData& blockData )
{
    const auto& blockBeginning = blockData.first;
    const auto& block = *blockData.second;

    LOG_DEBUG << "Indexing block " << blockBeginning << " start";

    IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

    guessEncoding( block, scopedAccessor, state );

    if ( !block.empty() ) {
        const auto linePositions = parseDataBlock( blockBeginning, block, state );
        auto maxLength = state.max_length;
        if ( maxLength > std::numeric_limits<LineLength::UnderlyingType>::max() ) {
            LOG_ERROR << "Too long lines " << maxLength;
            maxLength = std::numeric_limits<LineLength::UnderlyingType>::max();
        }

        scopedAccessor.addAll(
            block, LineLength( type_safe::narrow_cast<LineLength::UnderlyingType>( maxLength ) ),
            linePositions, state.encodingGuess );

        // Update the caller for progress indication
        const auto progress
            = ( state.file_size > 0 ) ? calculateProgress( state.pos, state.file_size ) : 100;

        if ( progress != scopedAccessor.getProgress() ) {
            scopedAccessor.setProgress( progress );
            LOG_DEBUG << "Indexing progress " << progress << ", indexed size " << state.pos;
            Q_EMIT indexingProgressed( progress );
        }
    }
    else {
        scopedAccessor.setEncodingGuess( state.encodingGuess );
    }

    LOG_DEBUG << "Indexing block " << blockBeginning << " done";
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

    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };

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
    const auto prefetchBufferSize = static_cast<size_t>( indexingPolicy_.readBufferSizeMb );

    LOG_INFO << "Prefetch buffer " << readableSize( prefetchBufferSize * IndexingBlockSize );

    using namespace std::chrono;
    using clock = high_resolution_clock;
    microseconds ioDuration{};

    const auto indexingStartTime = clock::now();

    tbb::flow::graph indexingGraph;
    auto blockPrefetcher = tbb::flow::limiter_node<BlockData>( indexingGraph, prefetchBufferSize );
    auto blockQueue = tbb::flow::queue_node<BlockData>( indexingGraph );

    auto blockParser = tbb::flow::function_node<BlockData, tbb::flow::continue_msg>(
        indexingGraph, tbb::flow::serial, [ this, &state ]( const BlockData& blockData ) {
            indexNextBlock( state, blockData );
            delete blockData.second;
            return tbb::flow::continue_msg{};
        } );

    tbb::flow::make_edge( blockPrefetcher, blockQueue );
    tbb::flow::make_edge( blockQueue, blockParser );
    tbb::flow::make_edge( blockParser, blockPrefetcher.decrementer() );

    // The graph pulls its blocks from an input_node while this thread waits in
    // wait_for_all(), which makes this thread one of those running the graph.
    // Pushing blocks in from here instead, sleeping whenever the limiter was
    // full, only worked while TBB had a worker free for this graph (#146; the
    // same stall hit Search in #142). Once blockReader stops, reading at the
    // end of the file, on a read error or on an interrupt, the pass ends as
    // soon as the blocks already read have gone through the graph.
    file.seek( state.pos );

    auto blockReader = tbb::flow::input_node<BlockData>(
        indexingGraph, [ this, &file, &ioDuration ]( tbb::flow_control& control ) -> BlockData {
            auto blockData = readNextBlock( file, ioDuration );
            if ( !blockData ) {
                control.stop();
                return {};
            }
            return *blockData;
        } );

    tbb::flow::make_edge( blockReader, blockPrefetcher );

    LOG_INFO << "Reading blocks";
    blockReader.activate();
    indexingGraph.wait_for_all();
    LOG_INFO << "Reading blocks done";

    IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

    LOG_DEBUG << "Indexed up to " << state.pos;

    // Check if there is a non LF terminated line at the end of the file
    if ( !interruptRequest_ && state.file_size > state.pos ) {
        LOG_WARNING << "Non LF terminated file, adding a fake end of line";

        FastLinePositionArray line_position;
        line_position.append( OffsetInFile( state.file_size + 1 ) );
        line_position.setFakeFinalLF();

        scopedAccessor.addAll( {}, 0_length, line_position, state.encodingGuess );
    }

    const auto endFilePos = file.pos();
    file.reset();
    QByteArray hashBuffer( IndexingBlockSize, Qt::Uninitialized );
    const auto headerHashSize = file.read( hashBuffer.data(), hashBuffer.size() );
    FileDigest fastHashDigest;
    fastHashDigest.addData( hashBuffer.data(), static_cast<size_t>( headerHashSize ) );

    scopedAccessor.setHeaderHash( fastHashDigest.digest(), headerHashSize );

    if ( endFilePos <= hashBuffer.size() ) {
        scopedAccessor.setTailHash( fastHashDigest.digest(), 0, headerHashSize );
    }
    else {
        const auto tailHashOffset = endFilePos - hashBuffer.size();
        file.seek( tailHashOffset );
        const auto tailHashSize = file.read( hashBuffer.data(), hashBuffer.size() );
        fastHashDigest.reset();
        fastHashDigest.addData( hashBuffer.data(), static_cast<size_t>( tailHashSize ) );
        scopedAccessor.setTailHash( fastHashDigest.digest(), tailHashOffset, tailHashSize );
    }

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

    if ( scopedAccessor.getMaxLength().get()
         == std::numeric_limits<LineLength::UnderlyingType>::max() ) {
        dispatchToMainThread( [] {
            QMessageBox::critical( nullptr, "LogSquirl",
                                   "Can't index file: some lines are too long",
                                   QMessageBox::Close );
        } );

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

// Called in the worker thread's context
OperationResult FullIndexOperation::run()
{
    try {
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
            Q_EMIT indexingFinished( true );
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
            Q_EMIT indexingFinished( false );
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

                indexCache.trySave( fileName_, *linePos, accessor.getMaxLength(),
                                    accessor.getHash(), encodingName, linePos->hasFakeFinalLF() );
            }
        }

        Q_EMIT indexingFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "FullIndexOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear( indexingPolicy_ );
        }

        Q_EMIT indexingFinished( false );
        return false;
    }
}

OperationResult PartialIndexOperation::run()
{
    try {
        LOG_INFO << "PartialIndexOperation::run(), file " << fileName_.toStdString();

        const auto initialPosition
            = OffsetInFile( IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize() );

        LOG_INFO << "PartialIndexOperation: Starting the count at " << initialPosition << " ...";

        Q_EMIT indexingProgressed( 0 );

        doIndex( initialPosition );

        LOG_INFO << "PartialIndexOperation: ... finished counting.";

        const auto result = interruptRequest_ ? false : true;
        Q_EMIT indexingFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "PartialIndexOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear( indexingPolicy_ );
        }

        Q_EMIT indexingFinished( false );
        return false;
    }
}

OperationResult CheckFileChangesOperation::run()
{
    try {
        LOG_INFO << "CheckFileChangesOperation::run(), file " << fileName_.toStdString();
        const auto result = doCheckFileChanges();
        Q_EMIT fileCheckFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString
            = QString( "CheckFileChangesOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        Q_EMIT fileCheckFinished( MonitoredFileStatus::Truncated );
        return MonitoredFileStatus::Truncated;
    }
}

MonitoredFileStatus CheckFileChangesOperation::doCheckFileChanges()
{
    const auto indexedHash = IndexingData::ConstAccessor{ indexing_data_.get() }.getHash();
    const auto coverage = indexingPolicy_.fastModificationDetection ? DigestCoverage::HeaderAndTail
                                                                    : DigestCoverage::Full;

    switch ( indexFit( indexedHash, fileName_, coverage ) ) {
    case IndexFit::Unchanged:
        LOG_INFO << "No change in file";
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
