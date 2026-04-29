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

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QSemaphore>
#include <tuple>

#include "configuration.h"
#include "containers.h"
#include "dispatch_to.h"
#include "encodingdetector.h"
#include "indexcache.h"
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

void IndexingData::clear()
{
    const auto& config = Configuration::get();

    maxLength_ = 0_length;
    hash_ = {};
    hashBuilder_.reset();
    if ( config.useCompressedIndex() ) {
        linePosition_ = LinePositionArrayType( LinePositionArray{} );
    }
    else {
        linePosition_ = LinePositionArrayType( FastLinePositionArray{} );
    }
    encodingGuess_ = nullptr;
    encodingForced_ = nullptr;

    progress_ = {};
    useFastModificationDetection_ = config.fastModificationDetection();
}

void IndexingData::loadFromCache( LinePositionArray&& linePosition, LineLength maxLength,
                                  const IndexedHash& hash, QTextCodec* encoding )
{
    const auto& config = Configuration::get();
    useFastModificationDetection_ = config.fastModificationDetection();

    linePosition_ = std::move( linePosition );
    maxLength_ = maxLength;
    hash_ = hash;
    hashBuilder_.reset();
    encodingGuess_ = encoding;
    encodingForced_ = nullptr;
    progress_ = 100;
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

LogDataWorker::LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data )
    : indexing_data_( indexing_data )
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
        createRunnable( [ this, &operationStarted, forcedEncoding, fileName = fileName_ ] {
            LOG_INFO << "FullIndex thread started";
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<FullIndexOperation>(
                fileName, indexing_data_, interruptRequest_, forcedEncoding );
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
    operationsPool_.start( createRunnable( [ this, &operationStarted, fileName = fileName_ ] {
        QThread::currentThread()->setObjectName( "PartialIndex" );
        LOG_INFO << "PartialIndex thread started";
        operationStarted.release();
        ScopedLock operationLock( operationsMutex_ );
        auto operationRequested = std::make_unique<PartialIndexOperation>( fileName, indexing_data_,
                                                                           interruptRequest_ );
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
    operationsPool_.start( createRunnable( [ this, &operationStarted, fileName = fileName_ ] {
        operationStarted.release();
        ScopedLock operationLock( operationsMutex_ );
        auto operationRequested = std::make_unique<CheckFileChangesOperation>(
            fileName, indexing_data_, interruptRequest_ );

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
findNextLineFeed( const logsquirl::vector<char>& block, int posWithinBlock, const IndexingState& state,
                  FindDelimeter findNextDelimeter )
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

std::chrono::microseconds IndexOperation::readFileInBlocks( QFile& file,
                                                            BlockPrefetcher& blockPrefetcher )
{
    using namespace std::chrono;
    using clock = high_resolution_clock;

    LOG_INFO << "Starting IO thread";

    int sentBlocksCount = 0;

    microseconds ioDuration{};
    while ( !file.atEnd() ) {

        if ( interruptRequest_ ) {
            break;
        }

        BlockData blockData{ file.pos(), new logsquirl::vector<char>( IndexingBlockSize ) };

        clock::time_point ioT1 = clock::now();
        const auto readBytes
            = file.read( blockData.second->data(), logsquirl::ssize( *blockData.second ) );

        if ( readBytes < 0 ) {
            LOG_ERROR << "Reading past the end of file";
            // The buffer was never published to the prefetcher; release it here so
            // the consumer (which would otherwise free it) does not leak it.
            delete blockData.second;
            blockData.second = nullptr;
            break;
        }

        if ( readBytes < logsquirl::ssize( *blockData.second ) ) {
            blockData.second->resize( static_cast<size_t>( readBytes ) );
        }

        clock::time_point ioT2 = clock::now();

        ioDuration += duration_cast<microseconds>( ioT2 - ioT1 );

        if ( sentBlocksCount % 10 == 0 ) {
            LOG_INFO << "Sending block " << blockData.first << " size " << blockData.second->size();
        }

        // try_put may legitimately fail forever if the indexing was interrupted while we
        // were waiting for capacity. In that case the buffer is still owned by us; free it
        // before exiting the loop to avoid a leak.
        bool blockAccepted = false;
        while ( !interruptRequest_ ) {
            if ( blockPrefetcher.try_put( std::move( blockData ) ) ) {
                blockAccepted = true;
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        if ( !blockAccepted ) {
            delete blockData.second;
            blockData.second = nullptr;
            break;
        }
        sentBlocksCount++;
    }

    auto lastBlock = std::make_pair( -1, new logsquirl::vector<char>{} );
    bool lastBlockAccepted = false;
    while ( !interruptRequest_ ) {
        if ( blockPrefetcher.try_put( lastBlock ) ) {
            lastBlockAccepted = true;
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    if ( !lastBlockAccepted ) {
        delete lastBlock.second;
    }

    LOG_INFO << "IO thread done";
    return ioDuration;
}

void IndexOperation::indexNextBlock( IndexingState& state, const BlockData& blockData )
{
    const auto& blockBeginning = blockData.first;
    const auto& block = *blockData.second;

    LOG_DEBUG << "Indexing block " << blockBeginning << " start";

    if ( blockBeginning < 0 ) {
        return;
    }

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

        scopedAccessor.clear();
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

    const auto& config = Configuration::get();
    const auto prefetchBufferSize = static_cast<size_t>( config.indexReadBufferSizeMb() );

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

    file.seek( state.pos );
    ioDuration = readFileInBlocks( file, blockPrefetcher );
    indexingGraph.wait_for_all();

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
        scopedAccessor.clear();
    }

    if ( scopedAccessor.getMaxLength().get()
         == std::numeric_limits<LineLength::UnderlyingType>::max() ) {
        dispatchToMainThread( [] {
            QMessageBox::critical( nullptr, "LogSquirl", "Can't index file: some lines are too long",
                                   QMessageBox::Close );
        } );

        scopedAccessor.clear();
    }

    if ( !scopedAccessor.getEncodingGuess() ) {
        scopedAccessor.setEncodingGuess( QTextCodec::codecForLocale() );
    }
}

// Called in the worker thread's context
OperationResult FullIndexOperation::run()
{
    try {
        LOG_INFO << "FullIndexOperation::run(), file " << fileName_.toStdString();

        Q_EMIT indexingProgressed( 0 );

        const auto& config = Configuration::get();

        // Try loading cached index from disk (skip temp files)
        const bool isTempFile = fileName_.startsWith( QDir::tempPath() );
        if ( config.useIndexCache() && !isTempFile ) {
            auto cached = IndexCache::tryLoad( fileName_ );
            if ( cached ) {
                // Validate the cached hash against the current file
                QFileInfo fi( fileName_ );
                const auto realFileSize = fi.size();

                if ( realFileSize == cached->hash.size ) {
                    // File size matches — verify header+tail hashes
                    QFile file( fileName_ );
                    if ( file.open( QIODevice::ReadOnly ) ) {
                        QByteArray buffer( IndexingBlockSize, Qt::Uninitialized );
                        bool valid = true;

                        // Check header
                        const auto headerRead
                            = file.read( buffer.data(), cached->hash.headerSize );
                        if ( headerRead == cached->hash.headerSize ) {
                            FileDigest headerDigest;
                            headerDigest.addData( buffer.data(),
                                                  static_cast<size_t>( headerRead ) );
                            if ( headerDigest.digest() != cached->hash.headerDigest ) {
                                valid = false;
                            }
                        }
                        else {
                            valid = false;
                        }

                        // Check tail
                        if ( valid && cached->hash.tailOffset > 0 ) {
                            file.seek( cached->hash.tailOffset );
                            const auto tailRead
                                = file.read( buffer.data(), cached->hash.tailSize );
                            if ( tailRead == cached->hash.tailSize ) {
                                FileDigest tailDigest;
                                tailDigest.addData( buffer.data(),
                                                    static_cast<size_t>( tailRead ) );
                                if ( tailDigest.digest() != cached->hash.tailDigest ) {
                                    valid = false;
                                }
                            }
                            else {
                                valid = false;
                            }
                        }

                        if ( valid ) {
                            LOG_INFO << "Using cached index for " << fileName_;

                            auto* codec
                                = QTextCodec::codecForName( cached->encodingName );
                            if ( !codec ) {
                                codec = QTextCodec::codecForLocale();
                            }

                            {
                                IndexingData::MutateAccessor scopedAccessor{
                                    indexing_data_.get() };
                                scopedAccessor.loadFromCache(
                                    std::move( cached->linePosition ), cached->maxLength,
                                    cached->hash, codec );
                                if ( forcedEncoding_ ) {
                                    scopedAccessor.forceEncoding( forcedEncoding_ );
                                }
                            }

                            Q_EMIT indexingProgressed( 100 );
                            Q_EMIT indexingFinished( true );
                            return true;
                        }
                    }
                }

                LOG_INFO << "Cached index stale for " << fileName_ << ", re-indexing";
                IndexCache::remove( fileName_ );
            }
        }

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear();
            scopedAccessor.forceEncoding( forcedEncoding_ );
        }

        doIndex( 0_offset );

        LOG_INFO << "FullIndexOperation: ... finished, interrupt = "
                 << static_cast<bool>( interruptRequest_ );

        const auto result = interruptRequest_ ? false : true;

        // Save to cache if indexing succeeded (and not a temp file)
        if ( result && config.useIndexCache() && !isTempFile ) {
            IndexingData::ConstAccessor accessor{ indexing_data_.get() };
            const auto* linePos = accessor.getCompressedLinePosition();
            // Don't cache empty indexes — they have no value, waste disk
            // space, and exercise the empty-storage code paths in
            // CompressedLinePositionStorage::serialize() unnecessarily.
            if ( linePos && linePos->size().get() > 0 ) {
                const auto* codec = accessor.getEncodingGuess();
                const auto encodingName = codec ? codec->name() : QByteArray( "UTF-8" );

                IndexCache::trySave( fileName_, *linePos, accessor.getMaxLength(),
                                     accessor.getHash(), encodingName,
                                     linePos->hasFakeFinalLF() );

                // Evict old entries if cache is too large
                const auto maxBytes
                    = static_cast<qint64>( config.indexCacheMaxSizeMb() ) * 1024 * 1024;
                IndexCache::evict( maxBytes );
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
            scopedAccessor.clear();
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
            scopedAccessor.clear();
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
    QFileInfo info( fileName_ );
    const auto indexedHash = IndexingData::ConstAccessor{ indexing_data_.get() }.getHash();
    const auto realFileSize = info.size();

    if ( realFileSize == 0 || realFileSize < indexedHash.size ) {
        LOG_INFO << "File truncated";
        return MonitoredFileStatus::Truncated;
    }
    else {
        QFile file( fileName_ );

        QByteArray buffer{ IndexingBlockSize, Qt::Uninitialized };

        bool isFileModified = false;
        const auto& config = Configuration::get();

        if ( !file.isOpen() && !file.open( QIODevice::ReadOnly ) ) {
            LOG_INFO << "File failed to open";
            return MonitoredFileStatus::Truncated;
        }

        const auto getDigest = [ &file, &buffer ]( const qint64 indexedSize ) {
            FileDigest fileDigest;
            auto readSize = 0ll;
            auto totalSize = 0ll;
            do {
                const auto bytesToRead
                    = std::min( static_cast<qint64>( buffer.size() ), indexedSize - totalSize );
                readSize = file.read( buffer.data(), bytesToRead );

                if ( readSize > 0 ) {
                    fileDigest.addData( buffer.data(), static_cast<size_t>( readSize ) );
                    totalSize += readSize;
                }

            } while ( readSize > 0 && totalSize < indexedSize );

            return fileDigest.digest();
        };
        if ( config.fastModificationDetection() ) {
            const auto headerDigest = getDigest( indexedHash.headerSize );

            LOG_INFO << "indexed header xxhash " << indexedHash.headerDigest;
            LOG_INFO << "current header xxhash " << headerDigest << ", size "
                     << indexedHash.headerSize;

            isFileModified = headerDigest != indexedHash.headerDigest;

            if ( !isFileModified ) {
                file.seek( indexedHash.tailOffset );
                const auto tailDigest = getDigest( indexedHash.tailSize );

                LOG_INFO << "indexed tail xxhash " << indexedHash.tailDigest;
                LOG_INFO << "current tail xxhash " << tailDigest << ", size "
                         << indexedHash.tailSize;

                isFileModified = tailDigest != indexedHash.tailDigest;
            }
        }
        else {

            const auto realHashDigest = getDigest( indexedHash.size );

            LOG_INFO << "indexed xxhash " << indexedHash.fullDigest;
            LOG_INFO << "current xxhash " << realHashDigest;

            isFileModified = realHashDigest != indexedHash.fullDigest;
        }

        if ( isFileModified ) {
            LOG_INFO << "File changed in indexed range";
            return MonitoredFileStatus::Truncated;
        }
        else if ( realFileSize > indexedHash.size ) {
            LOG_INFO << "New data on disk";
            return MonitoredFileStatus::DataAdded;
        }
        else {
            LOG_INFO << "No change in file";
            return MonitoredFileStatus::Unchanged;
        }
    }
}
