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

#include <chrono>
#include <cmath>
#include <exception>
#include <qsemaphore.h>
#include <utility>

#include <robin_hood.h>
#include <tbb/flow_graph.h>
#include <vector>

#include "configuration.h"
#include "dispatch_to.h"
#include "issuereporter.h"
#include "linetypes.h"
#include "log.h"
#include "progress.h"
#include "runnable_lambda.h"

#include "logdata.h"
#include "regularexpression.h"

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
    SearchBlockData( LineNumber start, LogData::RawLines blockLines )
        : chunkStart( start )
        , lines( std::move( blockLines ) )
    {
    }

    SearchBlockData( const SearchBlockData& ) = delete;
    SearchBlockData( SearchBlockData&& ) = default;

    SearchBlockData& operator=( const SearchBlockData& ) = delete;
    SearchBlockData& operator=( SearchBlockData&& ) = default;

    LineNumber chunkStart;
    LogData::RawLines lines;

    PartialSearchResults searchResults;
};

PartialSearchResults filterLines( const PatternMatcher& matcher, const LogData::RawLines& rawLines,
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
    return SearchResults{ std::exchange( newMatches_, {} ), maxLength_, nbLinesProcessed_ };
}

void SearchData::addAll( LineLength length, const SearchResultArray& matches,
                         LinesCount matchedLines, LinesCount processedLines )
{
    UniqueLock lock( dataMutex_ );

    maxLength_ = qMax( maxLength_, length );
    nbLinesProcessed_ = qMax( nbLinesProcessed_, processedLines );
    nbMatches_ += matchedLines;

    newMatches_ |= matches;
}

LinesCount SearchData::getNbMatches() const
{
    SharedLock lock( dataMutex_ );
    return nbMatches_;
}

LineNumber SearchData::getLastProcessedLine() const
{
    SharedLock lock( dataMutex_ );
    return LineNumber{ nbLinesProcessed_.get() };
}

void SearchData::deleteMatch( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    matches_.remove( line.get() );
}

void SearchData::clear()
{
    UniqueLock locker( dataMutex_ );

    maxLength_ = LineLength( 0 );
    nbLinesProcessed_ = LinesCount( 0 );
    nbMatches_ = LinesCount( 0 );
    matches_ = {};
    newMatches_ = {};
}

LogFilteredDataWorker::LogFilteredDataWorker( const LogData& sourceLogData )
    : sourceLogData_( sourceLogData )
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
                                             startLine, endLine ] {
        operationStarted.release();
        // Deliberately not holding operationsMutex_ here: the pool (maxThreadCount 1)
        // already serializes actual execution, and holding it across a run -- which
        // can take a while -- would block a superseding search() call from even
        // updating activeSearchId_ until this run finished on its own, defeating
        // supersession entirely (the same trap the destructor's wait avoids).
        auto operationRequested = std::make_unique<FullSearchOperation>(
            sourceLogData_, id, activeSearchId_, compiledExpression, startLine, endLine );
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
    operationsPool_.start( createRunnable( [ this, &operationStarted, id, compiledExpression,
                                             startLine, endLine, position ] {
        operationStarted.release();
        // See the comment in search(): not holding operationsMutex_ here is what
        // lets a superseding call proceed without waiting for this run to finish.
        auto operationRequested = std::make_unique<UpdateSearchOperation>(
            sourceLogData_, id, activeSearchId_, compiledExpression, startLine, endLine,
            position );
        connectSignalsAndRun( operationRequested.get() );
    } ) );

    operationStarted.acquire();

    return id;
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

SearchOperation::SearchOperation( const LogData& sourceLogData, SearchId searchId,
                                  const std::atomic<uint64_t>& activeSearchId,
                                  std::shared_ptr<const RegularExpression> compiledExpression,
                                  LineNumber startLine, LineNumber endLine )

    : searchId_( searchId )
    , activeSearchId_( activeSearchId )
    , compiledExpression_( std::move( compiledExpression ) )
    , sourceLogData_( sourceLogData )
    , startLine_( startLine )
    , endLine_( endLine )

{
}

bool SearchOperation::isSuperseded() const
{
    return activeSearchId_.load( std::memory_order_acquire ) != searchId_.get();
}

void SearchOperation::doSearch( SearchData& searchData, LineNumber initialLine )
{
    const auto nbSourceLines = sourceLogData_.getNbLine();

    LOG_INFO << "Searching from line " << initialLine << " to " << nbSourceLines;

    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    // Copied at the start of the run rather than read through a reference
    // held for its duration. The options dialog is modal to the window but
    // does not stop this pool, so pressing Apply writes these very fields
    // from the UI thread while this search is reading them.
    //
    // Copying is not synchronisation -- the settings object still has
    // neither a mutex nor atomics, and a thread sanitizer still flags each
    // read below -- but it does mean the run stays consistent with itself
    // rather than picking up a new value part-way through. A setting
    // changed mid-run takes effect on the next run, exactly as before.
    // The read disappears entirely once this worker is handed a Search
    // Policy instead (#93).
    const auto useParallelSearch = Configuration::get().useParallelSearch();
    const auto configuredThreadPoolSize = Configuration::get().searchThreadPoolSize();
    const auto searchReadBufferSizeLines = Configuration::get().searchReadBufferSizeLines();

    const auto matchingThreadsCount = static_cast<uint32_t>(
        [ useParallelSearch, configuredThreadPoolSize ]() {
            if ( !useParallelSearch ) {
                return 1;
            }
            return qMax( 1, configuredThreadPoolSize == 0 ? tbb::info::default_concurrency()
                                                          : configuredThreadPoolSize );
        }() );

    LOG_INFO << "Using " << matchingThreadsCount << " matching threads";

    // A persistent, explicitly entered tbb::task_arena around this whole
    // section (constructing the graph, feeding it and wait_for_all()) was
    // tried here as a fix for the #85 CI-only ~120s stall (theory: rapid
    // back-to-back graph construction/teardown on the same thread, since
    // 80f08072 removed the blocking wait between runs, could leave a fresh
    // implicit arena's task demand unregistered). It had zero effect -- CI
    // reproduced the identical stall with it in place -- so it was reverted
    // rather than left as unexplained complexity. See the per-chunk
    // checkpoints below for the next round of narrowing: qtests_main.cpp
    // deliberately sets a 10-line read buffer for this binary, so a 20000-line
    // test file means ~2000 chunks and heavy churn through the limiter's
    // capacity of matchingThreadsCount * 3 -- plenty of iterations for a
    // rare per-iteration race to surface, if that's what this is.
    tbb::flow::graph searchGraph;

    if ( initialLine < startLine_ ) {
        initialLine = startLine_;
    }

    const auto endLine = qMin( LineNumber( nbSourceLines.get() ), endLine_ );
    const auto nbLinesInChunk
        = LinesCount( static_cast<LinesCount::UnderlyingType>( searchReadBufferSizeLines ) );

    std::chrono::microseconds fileReadingDuration{ 0 };

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
    // Diagnostic for the #85 CI-only ~120s stall (see logfiltereddata_test.cpp's
    // "a Search superseded by a later one" scenario): bisects doSearch's overall
    // duration so a failing CI run pins down which phase actually ate the time,
    // rather than guessing between regex compilation, graph setup, the feed loop
    // and wait_for_all(). Remove once that investigation concludes.
    LOG_INFO << "doSearch checkpoint: pattern compiled after "
             << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );
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
    LinesCount nbMatches = searchData.getNbMatches();
    auto reportedMatches = nbMatches;
    int reportedPercentage = 0;

    std::chrono::microseconds matchCombiningDuration{ 0 };

    auto matchProcessor
        = tbb::flow::function_node<BlockDataType, tbb::flow::continue_msg, tbb::flow::rejecting>(
            searchGraph, 1, [ & ]( const BlockDataType& blockData ) {
                if ( isSuperseded() ) {
                    LOG_INFO << "Match processor interrupted";
                    // The processor owns the block once it reaches this node; release the
                    // heap-allocated SearchBlockData even on the interrupt path to avoid leaks.
                    delete blockData;
                    return tbb::flow::continue_msg{};
                }

                const auto& matchResults = blockData->searchResults;

                const auto matchProcessorStartTime = high_resolution_clock::now();

                if ( matchResults.processedLines.get() ) {

                    maxLength = qMax( maxLength, matchResults.maxLength );
                    const LinesCount matchesCount
                        = LinesCount( matchResults.matchingLines.cardinality() );
                    nbMatches += matchesCount;

                    const auto processedLines = LinesCount{ matchResults.chunkStart.get()
                                                            + matchResults.processedLines.get() };

                    totalProcessedLines += matchResults.processedLines;

                    // After each block, copy the data to shared data
                    // and update the client
                    searchData.addAll( maxLength, matchResults.matchingLines, matchesCount,
                                       processedLines );

                    LOG_DEBUG << "done Searching chunk starting at " << matchResults.chunkStart
                              << ", " << matchResults.processedLines << " lines read.";
                }

                delete blockData;

                const auto matchProcessorEndTime = high_resolution_clock::now();
                matchCombiningDuration += duration_cast<microseconds>( matchProcessorEndTime
                                                                       - matchProcessorStartTime );
                const int percentage
                    = calculateProgress( totalProcessedLines.get(), totalLines.get() );

                if ( percentage > reportedPercentage || nbMatches > reportedMatches ) {

                    Q_EMIT searchProgressed( nbMatches, std::min( 99, percentage ), initialLine,
                                             searchId_ );

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

    LOG_INFO << "doSearch checkpoint: graph wired after "
             << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );

    auto chunkStart = initialLine;
    int chunkIndex = 0;
    while ( chunkStart < endLine && !isSuperseded() ) {
        const auto lineSourceStartTime = high_resolution_clock::now();
        LOG_DEBUG << "Reading chunk starting at " << chunkStart;

        // Diagnostic for #85 (see the comment above the graph construction
        // above): narrows the CI-only ~120s stall further than "somewhere in
        // the feed loop" by bracketing the two things that loop actually
        // does per chunk -- reading it, and handing it to the graph. Remove
        // once that investigation concludes.
        LOG_INFO << "doSearch checkpoint: chunk " << chunkIndex << " read starting after "
                 << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );

        const auto linesInChunk
            = LinesCount( qMin( nbLinesInChunk.get(), ( endLine - chunkStart ).get() ) );
        auto lines = sourceLogData_.getLinesRaw( chunkStart, linesInChunk );

        LOG_INFO << "doSearch checkpoint: chunk " << chunkIndex << " read done after "
                 << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );

        /*LOG_DEBUG << "Sending chunk starting at " << chunkStart << ", " <<
            lines.second.size()
                << " lines read.";*/
        BlockDataType blockData = new SearchBlockData{ chunkStart, std::move( lines ) };

        const auto lineSourceEndTime = high_resolution_clock::now();
        const auto chunkReadTime
            = duration_cast<microseconds>( lineSourceEndTime - lineSourceStartTime );

        /*LOG_DEBUG << "Sent chunk starting at " << chunkStart << ", " <<
        blockData->lines.second.size()
                << " lines read in " << static_cast<float>( chunkReadTime.count() )
        / 1000.f
                << " ms";*/

        chunkStart = chunkStart + nbLinesInChunk;
        fileReadingDuration += chunkReadTime;

        // Wait until the prefetcher accepts the block or the search is interrupted. If the
        // block was never published it is still owned by us, so it must be freed here to
        // avoid leaking the SearchBlockData.
        bool blockAccepted = false;
        int putAttempts = 0;
        while ( !isSuperseded() ) {
            ++putAttempts;
            if ( blockPrefetcher.try_put( blockData ) ) {
                blockAccepted = true;
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        LOG_INFO << "doSearch checkpoint: chunk " << chunkIndex << " put "
                 << ( blockAccepted ? "accepted" : "superseded" ) << " after " << putAttempts
                 << " attempt(s), "
                 << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );
        if ( !blockAccepted ) {
            delete blockData;
            break;
        }
        ++chunkIndex;
    }

    LOG_INFO << "doSearch checkpoint: feed loop done after "
             << duration_cast<milliseconds>( high_resolution_clock::now() - t1 );

    searchGraph.wait_for_all();

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    const auto durationUs = duration_cast<microseconds>( t2 - t1 );
    const auto durationMs = duration_cast<milliseconds>( t2 - t1 );

    LOG_INFO << "Searching done, overall duration " << durationUs;
    LOG_INFO << "Line reading took " << fileReadingDuration;
    LOG_INFO << "Results combining took " << matchCombiningDuration;

    for ( const auto& regexMatcher : regexMatchers ) {
        LOG_INFO << "Matching took " << std::get<microseconds>( regexMatcher );
    }

    const auto totalFileSize = sourceLogData_.getFileSize();

    LOG_INFO << "Searching perf "
             << static_cast<uint64_t>(
                    std::floor( 1000.f * static_cast<float>( ( endLine - initialLine ).get() )
                                / static_cast<float>( durationMs.count() ) ) )
             << " lines/s";
    LOG_INFO << "Searching io perf "
             << ( 1000.f * static_cast<float>( totalFileSize )
                  / static_cast<float>( durationMs.count() ) )
                    / ( 1024 * 1024 )
             << " MiB/s";

    // Completion is reported once, here, rather than folded into the last progress
    // tick -- that is what lets a superseded/interrupted run be told apart from a
    // genuinely finished one instead of both claiming 100%.
    Q_EMIT searchFinished( searchId_, nbMatches, initialLine, isSuperseded() );
}

// Called in the worker thread's context
void FullSearchOperation::run( SearchData& searchData )
{
    if ( isSuperseded() ) {
        // Superseded before we even started (e.g. several patterns were typed in
        // quick succession); skip the work entirely rather than clobbering data
        // the now-active run may already be relying on. Still report finished --
        // whoever started us paired it with one attachReader() that only our
        // searchFinished balances with a detachReader().
        LOG_INFO << "Search superseded before it started, skipping";
        Q_EMIT searchFinished( searchId_, 0_lcount, startLine_, true );
        return;
    }

    try {
        // Clear the shared data
        searchData.clear();
        doSearch( searchData, 0_lnum );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "FullSearchOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}

// Called in the worker thread's context
void UpdateSearchOperation::run( SearchData& searchData )
{
    if ( isSuperseded() ) {
        LOG_INFO << "Search update superseded before it started, skipping";
        Q_EMIT searchFinished( searchId_, 0_lcount, initialPosition_, true );
        return;
    }

    try {
        auto initialLine = qMax( searchData.getLastProcessedLine(), initialPosition_ );

        if ( initialLine.get() >= 1 ) {
            // We need to re-search the last line because it might have
            // been updated (if it was not LF-terminated)
            --initialLine;
            // In case the last line matched, we don't want it to match twice.
            searchData.deleteMatch( initialLine );
        }

        doSearch( searchData, initialLine );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "UpdateSearchOpertaion failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}
