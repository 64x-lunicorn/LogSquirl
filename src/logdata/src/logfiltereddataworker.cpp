/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <qsemaphore.h>
#include <unordered_map>
#include <utility>

#include <robin_hood.h>
#include <tbb/flow_graph.h>
#include <tbb/info.h>
#include <vector>

#include "linetypes.h"
#include "log.h"
#include "progress.h"
#include "runnable_lambda.h"

#include "regularexpression.h"
#include "searchblocksource.h"

#include "logfiltereddataworker.h"
#include "synchronization.h"

namespace {
struct PartialSearchResults {
    PartialSearchResults() = default;

    PartialSearchResults( const PartialSearchResults& ) = delete;
    PartialSearchResults( PartialSearchResults&& ) = default;
    PartialSearchResults& operator=( const PartialSearchResults& ) = delete;
    PartialSearchResults& operator=( PartialSearchResults&& ) = default;

    SearchResultArray matchingLines;
    LineLength maxLength;

    LineNumber chunkStart;
    LinesCount processedLines;
};

struct SearchBlockData {
    SearchBlockData() = default;
    SearchBlockData( LineNumber start, RawLines blockLines )
        : chunkStart( start )
        , lines( std::move( blockLines ) )
    {
    }

    SearchBlockData( const SearchBlockData& ) = delete;
    SearchBlockData( SearchBlockData&& ) = default;

    SearchBlockData& operator=( const SearchBlockData& ) = delete;
    SearchBlockData& operator=( SearchBlockData&& ) = default;

    LineNumber chunkStart;
    RawLines lines;

    PartialSearchResults searchResults;
};

// The blocks a Search has read and not yet combined. They are owned here, not
// by the pointers passed through the search graph: a failure -- a block that
// cannot be read, say -- cancels the graph, and the blocks it holds then never
// reach the node that combines them. Whatever is left when the Search ends is
// released with this. One lock per block, none per Log Line.
class ReadBlocks {
public:
    ReadBlocks() = default;
    ReadBlocks( const ReadBlocks& ) = delete;
    ReadBlocks( ReadBlocks&& ) = delete;
    ReadBlocks& operator=( const ReadBlocks& ) = delete;
    ReadBlocks& operator=( ReadBlocks&& ) = delete;
    ~ReadBlocks() = default;

    SearchBlockData* add( std::unique_ptr<SearchBlockData> block )
    {
        auto* added = block.get();
        std::lock_guard lock( mutex_ );
        blocks_.emplace( added, std::move( block ) );
        return added;
    }

    // The block was combined: its Log Lines are let go of at once.
    void release( SearchBlockData* block )
    {
        std::unique_ptr<SearchBlockData> released;
        {
            std::lock_guard lock( mutex_ );
            const auto found = blocks_.find( block );
            if ( found != blocks_.end() ) {
                released = std::move( found->second );
                blocks_.erase( found );
            }
        }
    }

private:
    std::mutex mutex_;
    std::unordered_map<SearchBlockData*, std::unique_ptr<SearchBlockData>> blocks_;
};

PartialSearchResults filterLines( const PatternMatcher& matcher, const RawLines& rawLines,
                                  LineNumber chunkStart )
{
    LOG_DEBUG << "Filter lines at " << chunkStart;
    PartialSearchResults results;
    results.chunkStart = chunkStart;
    results.processedLines = LinesCount{ rawLines.endOfLines.size() };

    const auto& lines = rawLines.buildUtf8View();

    for ( auto offset = 0u; offset < lines.size(); ++offset ) {
        const auto& line = lines[ offset ];

        const auto hasMatch = matcher.hasMatch( line );

        if ( hasMatch ) {
            results.maxLength = qMax( results.maxLength, getUntabifiedLength( line ) );
            const auto lineNumber = chunkStart + LinesCount{ offset };
            results.matchingLines.add( lineNumber.get() );

            // LOG_INFO << "Match at " << lineNumber << ": " << line;
        }
    }
    return results;
}

} // namespace

SearchResults SearchData::takeCurrentResults() const
{
    UniqueLock lock( dataMutex_ );
    return SearchResults{ std::exchange( newMatches_, {} ), maxLength_,
                          LinesCount{ searchedUntil_.get() } };
}

void SearchData::searchFrom( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    searchedUntil_ = line;
    searchedAhead_.clear();
}

void SearchData::addAll( LineLength length, const SearchResultArray& matches, LineNumber blockStart,
                         LinesCount blockLines )
{
    UniqueLock lock( dataMutex_ );

    maxLength_ = qMax( maxLength_, length );
    newMatches_ |= matches;

    const auto blockEnd = blockStart.get() + blockLines.get();
    if ( blockStart > searchedUntil_ ) {
        auto& searchedEnd = searchedAhead_[ blockStart.get() ];
        searchedEnd = std::max( searchedEnd, blockEnd );
        return;
    }

    // The block reaches the Log Lines searched without a gap: so do the
    // blocks combined beyond it that now follow on.
    auto searchedUntil = std::max( searchedUntil_.get(), blockEnd );
    auto ahead = searchedAhead_.begin();
    while ( ahead != searchedAhead_.end() && ahead->first <= searchedUntil ) {
        searchedUntil = std::max( searchedUntil, ahead->second );
        ahead = searchedAhead_.erase( ahead );
    }
    searchedUntil_ = LineNumber( searchedUntil );
}

LineNumber SearchData::getLastProcessedLine() const
{
    SharedLock lock( dataMutex_ );
    return searchedUntil_;
}

void SearchData::clear()
{
    UniqueLock locker( dataMutex_ );

    maxLength_ = LineLength( 0 );
    searchedUntil_ = LineNumber( 0 );
    searchedAhead_.clear();
    newMatches_ = {};
}

LogFilteredDataWorker::LogFilteredDataWorker( const SearchBlockSource& blockSource,
                                              const SearchPolicy& searchPolicy )
    : blockSource_( blockSource )
    , searchPolicy_( searchPolicy )
{
    operationsPool_.setMaxThreadCount( 1 );
}

LogFilteredDataWorker::~LogFilteredDataWorker() noexcept
{
    try {
        // Signal all running search operations to stop early
        activeSearchId_.store( 0, std::memory_order_release );

        // Remove pending runnables from the pool (thread-safe, no mutex needed)
        operationsPool_.clear();

        // Wait for the active runnable to finish WITHOUT holding operationsMutex_.
        // The pool thread needs to notice the id no longer matches before it can
        // exit. Holding the mutex here would deadlock.
        // Use a timeout so the process can exit even if TBB hangs on Windows.
        if ( !operationsPool_.waitForDone( 10000 ) ) {
            LOG_ERROR << "Search thread did not finish within 10 s — giving up";
        }

        LOG_INFO << "LogFilteredDataWorker shutdown";
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy LogFilteredDataWorker: " << e.what();
    }
}

void LogFilteredDataWorker::connectSignalsAndRun( SearchOperation* operationRequested )
{
    connect( operationRequested, &SearchOperation::searchProgressed, this,
             &LogFilteredDataWorker::searchProgressed );
    connect( operationRequested, &SearchOperation::searchFinished, this,
             &LogFilteredDataWorker::searchFinished, Qt::QueuedConnection );

    operationRequested->run( searchData_ );
    operationRequested->disconnect( this );
}

SearchId LogFilteredDataWorker::search( std::shared_ptr<const RegularExpression> compiledExpression,
                                        LineNumber startLine, LineNumber endLine )
{
    ScopedLock locker( operationsMutex_ ); // to protect enqueueing against interrupt()

    // Becoming the active run immediately (without waiting for whatever ran
    // before us to acknowledge) is what lets a still-running search be
    // superseded rather than waited on.
    const auto id = SearchId( ++nextSearchId_ );
    activeSearchId_.store( id.get(), std::memory_order_release );

    LOG_INFO << "Search requested";
    QSemaphore operationStarted;
    operationsPool_.start( createRunnable( [ this, &operationStarted, id, compiledExpression,
                                             startLine, endLine, searchPolicy = searchPolicy_ ] {
        operationStarted.release();
        // Deliberately not holding operationsMutex_ here: the pool (maxThreadCount 1)
        // already serializes actual execution, and holding it across a run -- which
        // can take a while -- would block a superseding search() call from even
        // updating activeSearchId_ until this run finished on its own, defeating
        // supersession entirely (the same trap the destructor's wait avoids).
        auto operationRequested = std::make_unique<FullSearchOperation>(
            blockSource_, id, activeSearchId_, compiledExpression, startLine, endLine,
            searchPolicy );
        connectSignalsAndRun( operationRequested.get() );
    } ) );
    operationStarted.acquire();

    return id;
}

SearchId
LogFilteredDataWorker::updateSearch( std::shared_ptr<const RegularExpression> compiledExpression,
                                     LineNumber startLine, LineNumber endLine, LineNumber position )
{
    ScopedLock locker( operationsMutex_ ); // to protect enqueueing against interrupt()

    const auto id = SearchId( ++nextSearchId_ );
    activeSearchId_.store( id.get(), std::memory_order_release );

    LOG_INFO << "Search update requested from " << position.get();

    QSemaphore operationStarted;
    operationsPool_.start(
        createRunnable( [ this, &operationStarted, id, compiledExpression, startLine, endLine,
                          position, searchPolicy = searchPolicy_ ] {
            operationStarted.release();
            // See the comment in search(): not holding operationsMutex_ here is what
            // lets a superseding call proceed without waiting for this run to finish.
            auto operationRequested = std::make_unique<UpdateSearchOperation>(
                blockSource_, id, activeSearchId_, compiledExpression, startLine, endLine, position,
                searchPolicy );
            connectSignalsAndRun( operationRequested.get() );
        } ) );

    operationStarted.acquire();

    return id;
}

void LogFilteredDataWorker::setSearchPolicy( const SearchPolicy& searchPolicy )
{
    ScopedLock locker( operationsMutex_ );
    searchPolicy_ = searchPolicy;
}

void LogFilteredDataWorker::interrupt()
{
    LOG_INFO << "Search interruption requested";
    // 0 is never handed out as a real search id (ids start at 1), so this
    // makes whatever is currently running see itself as superseded without
    // starting a replacement run.
    activeSearchId_.store( 0, std::memory_order_release );
}

// This will do an atomic copy of the object
SearchResults LogFilteredDataWorker::getSearchResults() const
{
    return searchData_.takeCurrentResults();
}

//
// Operations implementation
//

SearchOperation::SearchOperation( const SearchBlockSource& blockSource, SearchId searchId,
                                  const std::atomic<uint64_t>& activeSearchId,
                                  std::shared_ptr<const RegularExpression> compiledExpression,
                                  LineNumber startLine, LineNumber endLine,
                                  SearchPolicy searchPolicy )

    : searchId_( searchId )
    , activeSearchId_( activeSearchId )
    , compiledExpression_( std::move( compiledExpression ) )
    , blockSource_( blockSource )
    , startLine_( startLine )
    , endLine_( endLine )
    , searchPolicy_( searchPolicy )

{
}

bool SearchOperation::isSuperseded() const
{
    return activeSearchId_.load( std::memory_order_acquire ) != searchId_.get();
}

void SearchOperation::doSearch( SearchData& searchData, LineNumber initialLine )
{
    const auto nbSourceLines = blockSource_.getNbLines();

    LOG_INFO << "Searching from line " << initialLine << " to " << nbSourceLines;

    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    // From this run's own Search Policy: no settings object is read here at
    // all, so pressing Apply while this search is in flight cannot be
    // observed by it (#94).
    const auto useParallelSearch = searchPolicy_.useParallelSearch;
    const auto configuredThreadPoolSize = searchPolicy_.threadPoolSize;
    const auto searchReadBufferSizeLines = searchPolicy_.readBufferSizeLines;

    const auto matchingThreadsCount
        = static_cast<uint32_t>( [ useParallelSearch, configuredThreadPoolSize ]() {
              if ( !useParallelSearch ) {
                  return 1;
              }
              return qMax( 1, configuredThreadPoolSize == 0 ? tbb::info::default_concurrency()
                                                            : configuredThreadPoolSize );
          }() );

    LOG_INFO << "Using " << matchingThreadsCount << " matching threads";

    // Declared before the graph, so that it outlives the graph and every node.
    ReadBlocks readBlocks;

    // The graph pulls its blocks from an input_node while this thread waits in
    // wait_for_all(), which makes this thread one of those running the graph.
    // Pushing blocks in from here instead, sleeping whenever the limiter was
    // full, only worked while TBB had a worker free for this graph: the blocks
    // already accepted sat queued for as long as it had none, 120 s in CI (#142).
    tbb::flow::graph searchGraph;

    if ( initialLine < startLine_ ) {
        initialLine = startLine_;
    }
    searchData.searchFrom( initialLine );

    const auto endLine = qMin( LineNumber( nbSourceLines.get() ), endLine_ );
    const auto nbLinesInChunk
        = LinesCount( static_cast<LinesCount::UnderlyingType>( searchReadBufferSizeLines ) );

    std::chrono::microseconds fileReadingDuration{ 0 };
    // Counted per block, as the blocks are read, for the io perf reported below.
    std::size_t bytesRead = 0;

    using BlockDataType = SearchBlockData*;
    auto blockPrefetcher
        = tbb::flow::limiter_node<BlockDataType>( searchGraph, matchingThreadsCount * 3 );

    auto lineBlocksQueue = tbb::flow::buffer_node<BlockDataType>( searchGraph );

    using RegexMatcherNode
        = tbb::flow::function_node<BlockDataType, BlockDataType, tbb::flow::rejecting>;

    using PatternMatcherPtr = std::unique_ptr<PatternMatcher>;
    using MatcherContext = std::tuple<PatternMatcherPtr, microseconds, RegexMatcherNode>;

    logsquirl::vector<MatcherContext> regexMatchers;
    regexMatchers.reserve( matchingThreadsCount );
    // compiledExpression_ was already compiled by whoever validated the
    // pattern before starting this run (see SearchOperation's ctor) --
    // reused here rather than recompiled.
    for ( auto index = 0u; index < matchingThreadsCount; ++index ) {
        regexMatchers.emplace_back(
            compiledExpression_->createMatcher(), microseconds{ 0 },
            RegexMatcherNode(
                searchGraph, 1, [ &regexMatchers, index, this ]( const BlockDataType& blockData ) {
                    if ( isSuperseded() ) {
                        LOG_INFO << "Matcher " << index << " interrupted";
                        auto results = std::make_shared<PartialSearchResults>();
                        blockData->searchResults.chunkStart = blockData->chunkStart;
                        blockData->searchResults.processedLines
                            = LinesCount{ blockData->lines.endOfLines.size() };
                        return blockData;
                    }

                    const auto& matcher = std::get<PatternMatcherPtr>( regexMatchers.at( index ) );
                    const auto matchStartTime = high_resolution_clock::now();

                    blockData->searchResults
                        = filterLines( *matcher, blockData->lines, blockData->chunkStart );

                    const auto matchEndTime = high_resolution_clock::now();

                    microseconds& matchDuration
                        = std::get<microseconds>( regexMatchers.at( index ) );
                    matchDuration += duration_cast<microseconds>( matchEndTime - matchStartTime );
                    LOG_DEBUG << "Searcher " << index << " block " << blockData->chunkStart
                              << " sending matches "
                              << blockData->searchResults.matchingLines.cardinality();
                    return blockData;
                } ) );
    }

    auto resultsQueue = tbb::flow::buffer_node<BlockDataType>( searchGraph );

    const auto totalLines = endLine - initialLine;
    LinesCount totalProcessedLines = 0_lcount;
    LineLength maxLength = 0_length;
    // Only to report progress when Matches were found: what the Search Session
    // counts are the Matches themselves.
    LinesCount nbMatches = 0_lcount;
    auto reportedMatches = nbMatches;
    int reportedPercentage = 0;

    std::chrono::microseconds matchCombiningDuration{ 0 };

    auto matchProcessor
        = tbb::flow::function_node<BlockDataType, tbb::flow::continue_msg, tbb::flow::rejecting>(
            searchGraph, 1, [ & ]( const BlockDataType& blockData ) {
                if ( isSuperseded() ) {
                    LOG_INFO << "Match processor interrupted";
                    // Released on the interrupt path too, not only when the Search ends.
                    readBlocks.release( blockData );
                    return tbb::flow::continue_msg{};
                }

                const auto& matchResults = blockData->searchResults;

                const auto matchProcessorStartTime = high_resolution_clock::now();

                if ( matchResults.processedLines.get() ) {

                    maxLength = qMax( maxLength, matchResults.maxLength );
                    const LinesCount matchesCount
                        = LinesCount( matchResults.matchingLines.cardinality() );
                    nbMatches += matchesCount;

                    totalProcessedLines += matchResults.processedLines;

                    // After each block, copy the data to shared data
                    // and update the client
                    searchData.addAll( maxLength, matchResults.matchingLines,
                                       matchResults.chunkStart, matchResults.processedLines );

                    LOG_DEBUG << "done Searching chunk starting at " << matchResults.chunkStart
                              << ", " << matchResults.processedLines << " lines read.";
                }

                readBlocks.release( blockData );

                const auto matchProcessorEndTime = high_resolution_clock::now();
                matchCombiningDuration += duration_cast<microseconds>( matchProcessorEndTime
                                                                       - matchProcessorStartTime );
                const int percentage
                    = calculateProgress( totalProcessedLines.get(), totalLines.get() );

                if ( percentage > reportedPercentage || nbMatches > reportedMatches ) {

                    Q_EMIT searchProgressed( std::min( 99, percentage ), initialLine, searchId_ );

                    reportedPercentage = percentage;
                    reportedMatches = nbMatches;
                }

                return tbb::flow::continue_msg{};
            } );

    tbb::flow::make_edge( blockPrefetcher, lineBlocksQueue );

    for ( auto& regexMatcher : regexMatchers ) {
        tbb::flow::make_edge( lineBlocksQueue, std::get<RegexMatcherNode>( regexMatcher ) );
        tbb::flow::make_edge( std::get<RegexMatcherNode>( regexMatcher ), resultsQueue );
    }

    tbb::flow::make_edge( resultsQueue, matchProcessor );
    tbb::flow::make_edge( matchProcessor, blockPrefetcher.decrementer() );

    // blockReader's body reads one block per call. Once it stops, the Search
    // ends as soon as the blocks already read have gone through the graph.
    auto chunkStart = initialLine;
    auto blockReader = tbb::flow::input_node<BlockDataType>(
        searchGraph, [ & ]( tbb::flow_control& control ) -> BlockDataType {
            if ( chunkStart >= endLine || isSuperseded() ) {
                control.stop();
                return nullptr;
            }

            const auto lineSourceStartTime = high_resolution_clock::now();
            LOG_DEBUG << "Reading chunk starting at " << chunkStart;

            const auto linesInChunk
                = LinesCount( qMin( nbLinesInChunk.get(), ( endLine - chunkStart ).get() ) );
            BlockDataType blockData = readBlocks.add( std::make_unique<SearchBlockData>(
                chunkStart, blockSource_.getLinesRaw( chunkStart, linesInChunk ) ) );

            bytesRead += blockData->lines.buffer.size();
            chunkStart = chunkStart + nbLinesInChunk;
            fileReadingDuration += duration_cast<microseconds>( high_resolution_clock::now()
                                                                - lineSourceStartTime );
            return blockData;
        } );

    tbb::flow::make_edge( blockReader, blockPrefetcher );

    blockReader.activate();
    searchGraph.wait_for_all();

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    const auto durationUs = duration_cast<microseconds>( t2 - t1 );

    LOG_INFO << "Searching done, overall duration " << durationUs;
    LOG_INFO << "Line reading took " << fileReadingDuration;
    LOG_INFO << "Results combining took " << matchCombiningDuration;

    for ( const auto& regexMatcher : regexMatchers ) {
        LOG_INFO << "Matching took " << std::get<microseconds>( regexMatcher );
    }

    // A small file searches in well under a millisecond; dividing by the
    // millisecond count would then divide by zero, and casting the resulting
    // infinity to an integer is undefined behaviour (UBSan aborts on it).
    const auto elapsedSeconds
        = std::max( static_cast<double>( durationUs.count() ), 1.0 ) / 1'000'000.0;

    LOG_INFO << "Searching perf "
             << static_cast<uint64_t>( std::floor(
                    static_cast<double>( ( endLine - initialLine ).get() ) / elapsedSeconds ) )
             << " lines/s";
    LOG_INFO << "Searching io perf "
             << ( static_cast<double>( bytesRead ) / elapsedSeconds ) / ( 1024 * 1024 ) << " MiB/s";

    // Completion is reported once, here, rather than folded into the last progress
    // tick -- that is what lets a superseded/interrupted run be told apart from a
    // genuinely finished one instead of both claiming 100%.
    Q_EMIT searchFinished( searchId_, initialLine, isSuperseded(), {} );
}

void SearchOperation::run( SearchData& searchData )
{
    try {
        doRun( searchData );
    } catch ( const std::exception& err ) {
        const auto failure
            = QString( "%1 failed: %2" ).arg( metaObject()->className(), err.what() );
        LOG_ERROR << failure;
        searchData.clear();
        // Still reported finished: whoever started this run paired it with
        // one attachReader() that only searchFinished balances.
        Q_EMIT searchFinished( searchId_, startLine_, false, failure );
    }
}

// Called in the worker thread's context
void FullSearchOperation::doRun( SearchData& searchData )
{
    if ( isSuperseded() ) {
        // Superseded before we even started (e.g. several patterns were typed in
        // quick succession); skip the work entirely rather than clobbering data
        // the now-active run may already be relying on. Still report finished --
        // whoever started us paired it with one attachReader() that only our
        // searchFinished balances with a detachReader().
        LOG_INFO << "Search superseded before it started, skipping";
        Q_EMIT searchFinished( searchId_, startLine_, true, {} );
        return;
    }

    // Clear the shared data
    searchData.clear();
    doSearch( searchData, 0_lnum );
}

// Called in the worker thread's context
void UpdateSearchOperation::doRun( SearchData& searchData )
{
    if ( isSuperseded() ) {
        LOG_INFO << "Search update superseded before it started, skipping";
        Q_EMIT searchFinished( searchId_, initialPosition_, true, {} );
        return;
    }

    auto initialLine = qMax( searchData.getLastProcessedLine(), initialPosition_ );

    if ( initialLine.get() >= 1 ) {
        // We need to re-search the last line because it might have
        // been updated (if it was not LF-terminated). If it matches again,
        // it is still one Match: the Matches are a set of Log Lines.
        --initialLine;
    }

    doSearch( searchData, initialLine );
}
