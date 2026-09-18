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

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <qglobal.h>

#include <tbb/global_control.h>

#include <atomic>
#include <thread>
#include <vector>

#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

static const qint64 SL_NB_LINES = 500LL;

namespace {

bool generateDataFiles( QTemporaryFile& file, qint64 nbLines = SL_NB_LINES )
{
    // Room for the widest int: the line count is no longer a constant GCC can
    // bound to six digits.
    char newLine[ 128 ];

    if ( file.open() ) {
        for ( int i = 0; i < nbLines; i++ ) {
            snprintf( newLine, sizeof( newLine ),
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d\n",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
        }
        file.flush();
    }

    return true;
}

// Start an async search and wait until the spy reports 100 % progress.
// Uses waitUiState() polling instead of QSignalSpy::wait() to avoid
// platform-specific event-loop issues (Windows CI TBB hangs, #50).
// The 120 s timeout gives TBB enough headroom so REQUIRE rarely throws
// during exception unwinding.  Qt6's QSignalSpy auto-disconnects on
// destruction via its internal context object, so no explicit disconnect
// is required.
SearchSession::State lastSearchState( SafeQSignalSpy& searchStateSpy )
{
    return qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) );
}

void requestSearch( LogFilteredData* filtered_data, const QString& regexp,
                    SafeQSignalSpy& searchStateSpy )
{
    filtered_data->request( RegularExpressionPattern( regexp ) );

    const bool completed = waitUiState( [ & ]() {
        if ( searchStateSpy.count() == 0 ) {
            return false;
        }
        return lastSearchState( searchStateSpy ).progress >= 100;
    } );

    // In Qt6 QSignalSpy is not a QObject, so receiver-based disconnect is not
    // available.  Qt6 tracks the spy's internal context object and
    // auto-disconnects when the spy is destroyed, so no explicit disconnect is
    // needed.  The 120 s waitUiState timeout ensures TBB finishes before we
    // time out, so REQUIRE rarely throws and the spy is destroyed cleanly.
    REQUIRE( completed );

    // Let any queued progress/throttling events drain before the owning
    // LogData and LogFilteredData objects start tearing down.
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
}

} // namespace

using LineTypeFlags = LogFilteredData::LineTypeFlags;
using VisibilityFlags = LogFilteredData::VisibilityFlags;
using LineType = LogFilteredData::LineType;

static LogFilteredData::LineTypeFlags toFlags( LogFilteredData::LineType type )
{
    return static_cast<LineTypeFlags>( static_cast<LineType::Int>( type ) );
}

struct LogDataLoader {
    explicit LogDataLoader( SettingsPolicies policies = testSettingsPolicies(),
                            qint64 nbLines = SL_NB_LINES )
        : log_data( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        static int counter = 0;
        counter++;
        LOG_INFO << "Test run " << counter;

        REQUIRE( generateDataFiles( file, nbLines ) );
        SafeQSignalSpy loadEndSpy( &log_data, SIGNAL( loadingFinished( LoadingStatus ) ) );

        log_data.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }

    QTemporaryFile file{ "filtered_test_XXXXXX" };
    LogData log_data;
};

SCENARIO( "marks in filtered log data", "[logdata]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "loaded log data" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();

        WHEN( "Adding mark outside file" )
        {
            filtered_data->addMark( LineNumber( SL_NB_LINES + 25 ) );

            THEN( "No marked lines stored" )
            {
                for ( LineNumber i = 0_lnum; i < LineNumber( SL_NB_LINES ); ++i )
                    REQUIRE_FALSE(
                        filtered_data->lineTypeByLine( i ).testFlag( LineTypeFlags::Mark ) );
            }
        }

        WHEN( "Adding marks in log file" )
        {
            filtered_data->addMark( 10_lnum );
            filtered_data->addMark( 25_lnum );

            AND_WHEN( "Check for marked line" )
            {
                THEN( "Return true" )
                {
                    REQUIRE(
                        filtered_data->lineTypeByLine( 10_lnum ).testFlag( LineTypeFlags::Mark ) );
                    REQUIRE(
                        filtered_data->lineTypeByLine( 25_lnum ).testFlag( LineTypeFlags::Mark ) );
                }
            }

            AND_WHEN( "Get marks count" )
            {
                THEN( "Return all marks count" )
                {
                    REQUIRE( filtered_data->getNbMarks() == 2_lcount );
                }

                AND_WHEN( "Get marks" )
                {
                    auto marks = filtered_data->getMarks();
                    THEN( "Provide all marks" )
                    {
                        REQUIRE( marks.size() == 2 );
                    }
                }
            }

            AND_WHEN( "Get mark before has mark" )
            {
                const auto markBefore = filtered_data->getMarkBefore( 25_lnum );
                THEN( "Return previous mark" )
                {
                    REQUIRE( markBefore.has_value() );
                    REQUIRE( *markBefore == 10_lnum );
                }
            }

            AND_WHEN( "Get mark before has no data" )
            {
                const auto markBefore = filtered_data->getMarkBefore( 10_lnum );
                THEN( "Return no mark" )
                {
                    REQUIRE_FALSE( markBefore.has_value() );
                }
            }

            AND_WHEN( "Get mark before from a line without a mark" )
            {
                THEN( "Return the nearest mark above it" )
                {
                    REQUIRE( filtered_data->getMarkBefore( 30_lnum )
                             == OptionalLineNumber( 25_lnum ) );
                    REQUIRE( filtered_data->getMarkBefore( 15_lnum )
                             == OptionalLineNumber( 10_lnum ) );
                    REQUIRE_FALSE( filtered_data->getMarkBefore( 5_lnum ).has_value() );
                }
            }

            AND_WHEN( "Get mark after from a line without a mark" )
            {
                THEN( "Return the nearest mark below it" )
                {
                    REQUIRE( filtered_data->getMarkAfter( 5_lnum )
                             == OptionalLineNumber( 10_lnum ) );
                    REQUIRE( filtered_data->getMarkAfter( 15_lnum )
                             == OptionalLineNumber( 25_lnum ) );
                    REQUIRE_FALSE( filtered_data->getMarkAfter( 30_lnum ).has_value() );
                }
            }

            AND_WHEN( "Get mark after has mark" )
            {
                const auto markAfter = filtered_data->getMarkAfter( 10_lnum );
                THEN( "Return next mark" )
                {
                    REQUIRE( markAfter.has_value() );
                    REQUIRE( *markAfter == 25_lnum );
                }
            }

            AND_WHEN( "Get mark after has no data" )
            {
                const auto markAfter = filtered_data->getMarkAfter( 25_lnum );
                THEN( "Return no mark" )
                {
                    REQUIRE_FALSE( markAfter.has_value() );
                }
            }

            AND_WHEN( "Delete mark" )
            {
                filtered_data->deleteMark( 10_lnum );
                THEN( "Mark is removed" )
                {
                    REQUIRE_FALSE(
                        filtered_data->lineTypeByLine( 10_lnum ).testFlag( LineTypeFlags::Mark ) );
                    REQUIRE( filtered_data->getNbMarks() == 1_lcount );
                }
            }

            AND_WHEN( "Clear marks" )
            {
                filtered_data->clearMarks();
                THEN( "All marks are removed" )
                {
                    REQUIRE( filtered_data->getNbMarks() == 0_lcount );
                }
            }
        }
    }
}

SCENARIO( "search for regex", "[logdata]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "loaded log data" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();

        WHEN( "Searched for regex" )
        {
            const auto threadPoolSize = GENERATE( 0, 1, 2 );

            // Handed to the Log File, which passes it on to the
            // LogFilteredData above -- created before this line (#95).
            auto searchPolicy = testSettingsPolicies().search;
            searchPolicy.threadPoolSize = threadPoolSize;
            searchPolicy.useParallelSearch = threadPoolSize > 0;
            logDataLoader.log_data.setSearchPolicy( searchPolicy );

            auto filtered_lines = filtered_data->getNbLine();
            REQUIRE( filtered_lines.get() == 0 );

            SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                           &LogFilteredData::searchStateChanged };

            requestSearch( filtered_data.get(), "this is line [0-9]{5}9", searchStateSpy );

            THEN( "Matched lines are in data" )
            {
                REQUIRE( lastSearchState( searchStateSpy ).matchCount == 50_lcount );

                const auto matches_count = filtered_data->getNbMatches();
                REQUIRE( matches_count == 50_lcount );

                const auto lines = filtered_data->getExpandedLines( 0_lnum, matches_count );
                for ( const auto& l : lines ) {
                    REQUIRE( l.endsWith( '9' ) );
                }
            }
        }
    }
}

SCENARIO( "marks and matches in filtered log data", "[logdata]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "loaded log data" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();

        WHEN( "Searched for regex" )
        {
            auto searchPolicy = testSettingsPolicies().search;
            searchPolicy.threadPoolSize = 2;
            searchPolicy.useParallelSearch = true;
            logDataLoader.log_data.setSearchPolicy( searchPolicy );

            auto filtered_lines = filtered_data->getNbLine();
            REQUIRE( filtered_lines.get() == 0 );

            SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                           &LogFilteredData::searchStateChanged };

            requestSearch( filtered_data.get(), "this is line [0-9]{5}9", searchStateSpy );

            AND_WHEN( "Add marks at matched line" )
            {
                const auto& firstMatchedLine = filtered_data->getLineString( 0_lnum );
                REQUIRE( firstMatchedLine.right( 2 ).toStdString() == "09" );

                filtered_data->addMark( 9_lnum );

                THEN( "Has same number of lines" )
                {
                    REQUIRE( filtered_data->getNbLine() == 50_lcount );
                }
            }

            AND_WHEN( "Add marks at not matched line" )
            {
                filtered_data->addMark( 5_lnum );

                THEN( "Has one more line" )
                {
                    REQUIRE( filtered_data->getNbLine() == 51_lcount );
                }
            }

            AND_WHEN( "Has mixed marks and matches" )
            {
                filtered_data->addMark( 9_lnum );
                filtered_data->addMark( 5_lnum );

                AND_WHEN( "Only marks are visible" )
                {
                    filtered_data->setVisibility( VisibilityFlags::Marks );

                    THEN( "Has only marked lines count" )
                    {
                        REQUIRE( filtered_data->getNbLine() == 2_lcount );
                    }

                    AND_WHEN( "Ask for line type by line" )
                    {
                        THEN( "Return mark" )
                        {
                            auto type = filtered_data->lineTypeByLine( 5_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Mark );
                        }
                        THEN( "Return match" )
                        {
                            auto type = filtered_data->lineTypeByLine( 19_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Match );
                        }

                        THEN( "Return mark & match" )
                        {
                            auto type = filtered_data->lineTypeByLine( 9_lnum );
                            REQUIRE( toFlags( type )
                                     == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                        }
                    }

                    WHEN( "Ask for line type by index" )
                    {
                        THEN( "Return mark" )
                        {
                            auto type = filtered_data->lineTypeByIndex( 0_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Mark );
                        }
                        THEN( "Return mark & match" )
                        {
                            auto type = filtered_data->lineTypeByIndex( 1_lnum );
                            REQUIRE( toFlags( type )
                                     == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                        }
                    }
                }

                AND_WHEN( "Only matches are visible" )
                {
                    filtered_data->setVisibility( VisibilityFlags::Matches );

                    THEN( "Has only matches lines count" )
                    {
                        REQUIRE( filtered_data->getNbLine() == 50_lcount );
                    }

                    AND_WHEN( "Ask for line type by line" )
                    {
                        THEN( "Return mark" )
                        {
                            auto type = filtered_data->lineTypeByLine( 5_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Mark );
                        }
                        THEN( "Return match" )
                        {
                            auto type = filtered_data->lineTypeByLine( 19_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Match );
                        }
                        THEN( "Return mark & match" )
                        {
                            auto type = filtered_data->lineTypeByLine( 9_lnum );
                            REQUIRE( toFlags( type )
                                     == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                        }
                    }

                    AND_WHEN( "Ask for line type by index" )
                    {
                        THEN( "Return match" )
                        {
                            auto type = filtered_data->lineTypeByIndex( 1_lnum );
                            REQUIRE( toFlags( type ) == LineTypeFlags::Match );
                        }
                        THEN( "Return mark & match" )
                        {
                            auto type = filtered_data->lineTypeByIndex( 0_lnum );
                            REQUIRE( toFlags( type )
                                     == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                        }
                    }
                }

                filtered_data->setVisibility( VisibilityFlags::Matches | VisibilityFlags::Marks );

                AND_WHEN( "Ask for line type by line" )
                {
                    THEN( "Return Mark" )
                    {
                        auto type = filtered_data->lineTypeByLine( 5_lnum );
                        REQUIRE( toFlags( type ) == LineTypeFlags::Mark );
                    }
                    THEN( "Return match" )
                    {
                        auto type = filtered_data->lineTypeByLine( 19_lnum );
                        REQUIRE( toFlags( type ) == LineTypeFlags::Match );
                    }
                    THEN( "Return mark & match" )
                    {
                        auto type = filtered_data->lineTypeByLine( 9_lnum );
                        REQUIRE( toFlags( type )
                                 == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                    }
                }

                AND_WHEN( "Ask for line type by index" )
                {
                    THEN( "Return mark" )
                    {
                        auto type = filtered_data->lineTypeByIndex( 0_lnum );
                        REQUIRE( toFlags( type ) == LineTypeFlags::Mark );
                    }
                    THEN( "Return match" )
                    {
                        auto type = filtered_data->lineTypeByIndex( 2_lnum );
                        REQUIRE( toFlags( type ) == LineTypeFlags::Match );
                    }
                    THEN( "Return mark & match" )
                    {
                        auto type = filtered_data->lineTypeByIndex( 1_lnum );
                        REQUIRE( toFlags( type )
                                 == toFlags( LineTypeFlags::Mark | LineTypeFlags::Match ) );
                    }
                }
            }

            AND_WHEN( "Ask for matching line number" )
            {
                filtered_data->addMark( 1_lnum );

                AND_WHEN( "For marked line" )
                {
                    auto original_line = filtered_data->getMatchingLineNumber( 0_lnum );
                    THEN( "Original line is on mark" )
                    {
                        REQUIRE( original_line == 1_lnum );
                    }
                }

                AND_WHEN( "For matched line" )
                {
                    auto original_line = filtered_data->getMatchingLineNumber( 1_lnum );

                    const auto& firstMatchedLine = filtered_data->getLineString( 1_lnum );
                    REQUIRE( firstMatchedLine.right( 2 ).toStdString() == "09" );

                    THEN( "Original line is on match" )
                    {
                        REQUIRE( original_line == 9_lnum );
                    }
                }

                AND_WHEN( "For last line" )
                {
                    auto max_filtered_line = LineNumber( filtered_data->getNbLine().get() - 1 );
                    auto original_line = filtered_data->getMatchingLineNumber( max_filtered_line );
                    THEN( "Original line is last" )
                    {
                        REQUIRE( original_line == 499_lnum );
                    }
                }

                AND_WHEN( "For invalid line" )
                {
                    auto max_filtered_line = LineNumber( filtered_data->getNbLine().get() - 1 );
                    auto original_line
                        = filtered_data->getMatchingLineNumber( max_filtered_line + 1_lcount );
                    THEN( "Max line number is returned" )
                    {
                        REQUIRE( original_line == maxValue<LineNumber>() );
                    }
                }
            }

            AND_WHEN( "Ask for filtered line index" )
            {
                filtered_data->addMark( 1_lnum );

                AND_WHEN( "For marked line" )
                {
                    auto filtered_line = filtered_data->getLineIndexNumber( 1_lnum );
                    THEN( "Marked line returned" )
                    {
                        REQUIRE( filtered_line == 0_lnum );
                    }
                }

                AND_WHEN( "For matched line" )
                {
                    auto filtered_line = filtered_data->getLineIndexNumber( 9_lnum );
                    THEN( "Matched line returned" )
                    {
                        REQUIRE( filtered_line == 1_lnum );
                    }
                }

                AND_WHEN( "For last line" )
                {
                    auto max_filtered_line = LineNumber( filtered_data->getNbLine().get() - 1 );
                    auto filtered_line = filtered_data->getLineIndexNumber( 499_lnum );
                    THEN( "Last matched line returned" )
                    {
                        REQUIRE( filtered_line == max_filtered_line );
                    }
                }

                AND_WHEN( "For invalid line" )
                {
                    auto filtered_line = filtered_data->getMatchingLineNumber( 500_lnum );
                    THEN( "Max line number is returned" )
                    {
                        REQUIRE( filtered_line == maxValue<LineNumber>() );
                    }
                }
            }

            AND_WHEN( "Asked for line length" )
            {
                THEN( "Return expanded length" )
                {
                    REQUIRE( filtered_data->getLineLength( 1_lnum ) == LineLength( 92 ) );
                }
            }

            AND_WHEN( "Asked for line" )
            {
                THEN( "Return original line" )
                {
                    REQUIRE( filtered_data->getLineString( 2_lnum ).size() == 85 );
                }
            }

            AND_WHEN( "Asked for expanded line" )
            {
                THEN( "Return expanded line" )
                {
                    REQUIRE( filtered_data->getExpandedLineString( 2_lnum ).size() == 92 );
                }
            }

            AND_WHEN( "Asked to clear search" )
            {
                filtered_data->request();
                THEN( "Clear search results" )
                {
                    REQUIRE( filtered_data->getNbLine() == 0_lcount );
                }
            }
        }
    }
}

SCENARIO( "a Search superseded by a later one applies no stale results", "[logdata][search]" )
{
    GIVEN( "a log file where every line matches exactly one of two disjoint patterns" )
    {
        // Large enough, with a small enough chunk size and a single search thread,
        // that the first Search is still in flight when the second one starts --
        // this is what lets the test catch a genuine supersede, not just two
        // searches that happened to run back to back.
        static const qint64 nbLines = 20000;

        QTemporaryFile file{ "supersede_test_XXXXXX" };
        REQUIRE( [ & ]() {
            if ( !file.open() ) {
                return false;
            }
            char line[ 96 ];
            for ( qint64 i = 0; i < nbLines; ++i ) {
                const char* tag = ( i % 2 == 0 ) ? "EVEN" : "ODD";
                snprintf( line, sizeof( line ), "SUPERSEDE_TEST %s line %06lld\n", tag,
                          static_cast<long long>( i ) );
                file.write( line, static_cast<qint64>( qstrlen( line ) ) );
            }
            file.flush();
            return true;
        }() );

        auto policies = testSettingsPolicies();
        policies.search.threadPoolSize = 1;
        policies.search.useParallelSearch = false;
        policies.search.useResultsCache = false;

        LogData log_data{ policies.indexing, policies.search, policies.fileAccess,
                          policies.decoding };
        SafeQSignalSpy loadEndSpy( &log_data, SIGNAL( loadingFinished( LoadingStatus ) ) );
        log_data.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        auto filtered_data = log_data.getNewFilteredData();

        WHEN( "a second Search for the other pattern starts while the first is still running" )
        {
            SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                           &LogFilteredData::searchStateChanged };

            filtered_data->request( RegularExpressionPattern( "EVEN" ) );

            const bool firstSearchStarted
                = waitUiState( [ & ]() { return searchStateSpy.count() > 0; } );
            REQUIRE( firstSearchStarted );

            // Supersede it before it has had a chance to finish.
            filtered_data->request( RegularExpressionPattern( "ODD" ) );

            const bool secondSearchCompleted = waitUiState( [ & ]() {
                if ( searchStateSpy.count() == 0 ) {
                    return false;
                }
                return lastSearchState( searchStateSpy ).progress >= 100;
            } );
            REQUIRE( secondSearchCompleted );

            // Let any late progress from the superseded first Search be delivered
            // and (if the fix works) discarded, before we inspect the final state.
            QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

            THEN( "only the second pattern's matches are applied, with none of the first's" )
            {
                REQUIRE( filtered_data->getNbMatches() == 10000_lcount );

                const auto lines
                    = filtered_data->getExpandedLines( 0_lnum, filtered_data->getNbMatches() );
                for ( const auto& l : lines ) {
                    REQUIRE( l.contains( "ODD" ) );
                    REQUIRE_FALSE( l.contains( "EVEN" ) );
                }
            }
        }
    }
}

SCENARIO( "a Search completes even when TBB has no worker thread to spare", "[logdata][search]" )
{
    GIVEN( "a log file loaded under a Search Policy that reads it in 2000 small blocks" )
    {
        static const qint64 nbLines = 20000;

        QTemporaryFile file{ "no_worker_search_XXXXXX" };
        REQUIRE( [ & ]() {
            if ( !file.open() ) {
                return false;
            }
            char line[ 96 ];
            for ( qint64 i = 0; i < nbLines; ++i ) {
                const char* tag = ( i % 2 == 0 ) ? "EVEN" : "ODD";
                snprintf( line, sizeof( line ), "NO_WORKER_TEST %s line %06lld\n", tag,
                          static_cast<long long>( i ) );
                file.write( line, static_cast<qint64>( qstrlen( line ) ) );
            }
            file.flush();
            return true;
        }() );

        // The settings of the CI run that stalled for 120 s (#142).
        auto policies = testSettingsPolicies();
        policies.search.useParallelSearch = true;
        policies.search.threadPoolSize = 2;
        policies.search.readBufferSizeLines = 10;
        policies.search.useResultsCache = false;

        LogData log_data{ policies.indexing, policies.search, policies.fileAccess,
                          policies.decoding };
        SafeQSignalSpy loadEndSpy( &log_data, SIGNAL( loadingFinished( LoadingStatus ) ) );
        log_data.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        auto filtered_data = log_data.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

        WHEN( "a Search runs while no TBB worker thread is available to its graph" )
        {
            // TBB shares its workers between every graph in the process, so a
            // graph can find none free, as the one in #142 did for 120 s. A
            // parallelism of 1 makes that certain: only the thread running the
            // Search is left to process its blocks.
            tbb::global_control noWorkers( tbb::global_control::max_allowed_parallelism, 1 );

            filtered_data->request( RegularExpressionPattern( "ODD" ) );

            const bool searchCompleted = waitUiState(
                [ & ]() {
                    if ( searchStateSpy.count() == 0 ) {
                        return false;
                    }
                    return lastSearchState( searchStateSpy ).progress >= 100;
                },
                20000 );
            REQUIRE( searchCompleted );

            THEN( "it finds every match" )
            {
                REQUIRE( lastSearchState( searchStateSpy ).phase
                         == SearchSession::Phase::Complete );
                REQUIRE( filtered_data->getNbMatches() == 10000_lcount );
            }
        }
    }
}

SCENARIO( "A request repeating the same pattern and start with a grown end is a continuation",
          "[logdata][search]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "a completed search over the first half of the file" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        const RegularExpressionPattern pattern( "LOGDATA" );

        SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

        filtered_data->request( pattern, 0_lnum, LineNumber( SL_NB_LINES / 2 ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0 && lastSearchState( searchStateSpy ).progress >= 100;
        } ) );

        WHEN( "the same pattern and start are requested again with a larger end" )
        {
            filtered_data->request( pattern, 0_lnum, LineNumber( SL_NB_LINES ) );

            THEN( "the request is reported as a continuation" )
            {
                REQUIRE( filtered_data->searchState().isContinuation );

                REQUIRE( waitUiState(
                    [ & ]() { return lastSearchState( searchStateSpy ).progress >= 100; } ) );
                REQUIRE( filtered_data->getNbMatches() == LinesCount( SL_NB_LINES ) );
            }
        }

        WHEN( "a different pattern is requested over a larger range" )
        {
            filtered_data->request( RegularExpressionPattern( "glogg" ), 0_lnum,
                                    LineNumber( SL_NB_LINES ) );

            THEN( "the request starts fresh, not as a continuation" )
            {
                REQUIRE_FALSE( filtered_data->searchState().isContinuation );
            }
        }

        WHEN( "the same pattern is requested again without growing the end" )
        {
            filtered_data->request( pattern, 0_lnum, LineNumber( SL_NB_LINES / 2 ) );

            THEN( "the request starts fresh, not as a continuation" )
            {
                REQUIRE_FALSE( filtered_data->searchState().isContinuation );
            }
        }
    }
}

SCENARIO( "Context Lines are correct after a cache hit, and cleared when a Search is cleared",
          "[logdata][search]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "caching enabled and a non-zero Context Lines count" )
    {
        auto searchPolicy = testSettingsPolicies().search;
        searchPolicy.useResultsCache = true;
        searchPolicy.contextLinesCount = 2;
        logDataLoader.log_data.setSearchPolicy( searchPolicy );

        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

        // Matches exactly line 10; caches under this exact pattern/range.
        requestSearch( filtered_data.get(), "this is line 000010", searchStateSpy );
        REQUIRE_FALSE( filtered_data->searchState().fromCache );
        REQUIRE( toFlags( filtered_data->lineTypeByLine( 8_lnum ) ) == LineTypeFlags::Context );

        // A real (non-cached) search for a different, far-away match moves
        // Context Lines away from line 10's neighbourhood.
        requestSearch( filtered_data.get(), "this is line 000200", searchStateSpy );
        REQUIRE_FALSE( filtered_data->searchState().fromCache );
        REQUIRE( toFlags( filtered_data->lineTypeByLine( 8_lnum ) ) == LineTypeFlags::Plain );
        REQUIRE( toFlags( filtered_data->lineTypeByLine( 198_lnum ) ) == LineTypeFlags::Context );

        WHEN( "the first pattern is requested again and hits the cache" )
        {
            requestSearch( filtered_data.get(), "this is line 000010", searchStateSpy );

            THEN( "it was actually served from cache" )
            {
                REQUIRE( filtered_data->searchState().fromCache );
            }

            THEN( "Context Lines belong to this (cached) result, not the previous one" )
            {
                REQUIRE( toFlags( filtered_data->lineTypeByLine( 8_lnum ) )
                         == LineTypeFlags::Context );
                REQUIRE( toFlags( filtered_data->lineTypeByLine( 198_lnum ) )
                         == LineTypeFlags::Plain );
            }
        }

        WHEN( "the Search is cleared" )
        {
            filtered_data->request();

            THEN( "Context Lines are cleared along with it" )
            {
                REQUIRE( toFlags( filtered_data->lineTypeByLine( 198_lnum ) )
                         == LineTypeFlags::Plain );
            }
        }
    }
}

SCENARIO( "A cache hit is never treated as a base for a continuation", "[logdata][search]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "a cached pattern, and a different pattern that ran for real afterwards" )
    {
        auto searchPolicy = testSettingsPolicies().search;
        searchPolicy.useResultsCache = true;
        logDataLoader.log_data.setSearchPolicy( searchPolicy );

        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        const RegularExpressionPattern patternA( "this is line 000010" ); // matches only line 10
        const RegularExpressionPattern patternB( "this is line 000200" ); // matches only line 200

        SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

        auto waitForCompletion = [ & ]() {
            REQUIRE( waitUiState( [ & ]() {
                return searchStateSpy.count() > 0
                       && lastSearchState( searchStateSpy ).progress >= 100;
            } ) );
            QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        };

        // Caches patternA over [0, 250).
        filtered_data->request( patternA, 0_lnum, LineNumber( SL_NB_LINES / 2 ) );
        waitForCompletion();
        REQUIRE_FALSE( filtered_data->searchState().fromCache );

        // Runs for real over the same range, leaving the worker's own
        // persistent search data holding patternB's (unrelated) totals.
        filtered_data->request( patternB, 0_lnum, LineNumber( SL_NB_LINES / 2 ) );
        waitForCompletion();
        REQUIRE_FALSE( filtered_data->searchState().fromCache );

        // Re-requesting patternA now hits the cache from the first step.
        filtered_data->request( patternA, 0_lnum, LineNumber( SL_NB_LINES / 2 ) );
        waitForCompletion();
        REQUIRE( filtered_data->searchState().fromCache );

        WHEN( "patternA is requested again with a larger end" )
        {
            filtered_data->request( patternA, 0_lnum, LineNumber( SL_NB_LINES ) );

            THEN( "it is not treated as a continuation of the cache hit" )
            {
                REQUIRE_FALSE( filtered_data->searchState().isContinuation );
            }

            THEN( "once it completes, results reflect only patternA, not patternB's leftovers" )
            {
                waitForCompletion();
                REQUIRE( filtered_data->getNbMatches() == 1_lcount );
                REQUIRE( filtered_data->searchState().matchCount == 1_lcount );
            }
        }
    }
}

SCENARIO( "Requesting an invalid pattern discards a previous run's results", "[logdata][search]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "a completed search with matches" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

        requestSearch( filtered_data.get(), "this is line [0-9]{5}9", searchStateSpy );
        REQUIRE( filtered_data->getNbMatches() == 50_lcount );

        WHEN( "an invalid pattern is requested" )
        {
            filtered_data->request( RegularExpressionPattern( "[unterminated" ), 0_lnum,
                                    LineNumber( SL_NB_LINES ) );

            THEN( "the previous results are gone, not just uncounted" )
            {
                REQUIRE( filtered_data->searchState().phase
                         == SearchSession::Phase::InvalidPattern );
                REQUIRE( filtered_data->getNbMatches() == 0_lcount );
            }
        }
    }
}

namespace {

using LineNumbers = std::vector<LineNumber::UnderlyingType>;

// The Log Lines the Filtered View shows, read through its own line mapping,
// checking on the way that mapping a Log Line back gives the same index.
LineNumbers displayedLines( const LogFilteredData& filtered )
{
    LineNumbers lines;
    const auto nbLines = filtered.getNbLine().get();
    for ( LineNumber::UnderlyingType index = 0; index < nbLines; ++index ) {
        const auto line = filtered.getMatchingLineNumber( LineNumber( index ) );
        REQUIRE( filtered.getLineIndexNumber( line ) == LineNumber( index ) );
        REQUIRE( toFlags( filtered.lineTypeByIndex( LineNumber( index ) ) )
                 == toFlags( filtered.lineTypeByLine( line ) ) );
        lines.push_back( line.get() );
    }
    return lines;
}

// The Log Lines the Filtered View should show, worked out line by line from
// each Log Line's type and the visibility, without the combined line set.
LineNumbers expectedDisplayedLines( const LogFilteredData& filtered )
{
    const auto visibility = filtered.visibility();
    const bool matchesShown = visibility.testFlag( VisibilityFlags::Matches );
    // With Matches hidden the Filtered View shows Marks.
    const bool marksShown = visibility.testFlag( VisibilityFlags::Marks ) || !matchesShown;
    const bool contextShown = visibility.testFlag( VisibilityFlags::Context );

    LineNumbers lines;
    const auto nbTotalLines = filtered.getNbTotalLines().get();
    for ( LineNumber::UnderlyingType line = 0; line < nbTotalLines; ++line ) {
        const auto type = filtered.lineTypeByLine( LineNumber( line ) );
        if ( ( matchesShown && type.testFlag( LineTypeFlags::Match ) )
             || ( marksShown && type.testFlag( LineTypeFlags::Mark ) )
             || ( contextShown && type.testFlag( LineTypeFlags::Context ) ) ) {
            lines.push_back( line );
        }
    }
    return lines;
}

const auto AllVisible
    = VisibilityFlags::Matches | VisibilityFlags::Marks | VisibilityFlags::Context;

// A Log File of 2,000 lines whose Search Policy shows 2 Context Lines around
// each Match and Mark, and reads the file in small blocks so a Search reports
// progress before it completes.
SettingsPolicies contextLinesPolicies()
{
    auto policies = testSettingsPolicies();
    policies.search.contextLinesCount = 2;
    policies.search.useResultsCache = true;
    policies.search.useParallelSearch = false;
    policies.search.threadPoolSize = 1;
    policies.search.readBufferSizeLines = 10;
    return policies;
}

const qint64 ContextLinesFileLines = 2000;

// Matches every 10th Log Line.
const QString EveryTenthLine = "this is line [0-9]{5}0";

} // namespace

SCENARIO( "the Filtered View shows the right lines with Context Lines after each change to its "
          "inputs",
          "[logdata][search][context]" )
{
    const auto policies = contextLinesPolicies();
    LogDataLoader logDataLoader{ policies, ContextLinesFileLines };

    auto filtered_data = logDataLoader.log_data.getNewFilteredData();
    filtered_data->setVisibility( AllVisible );

    const auto checkDisplayedLines = [ & ]( const char* step ) {
        INFO( step );
        REQUIRE( displayedLines( *filtered_data ) == expectedDisplayedLines( *filtered_data ) );
    };

    // Checked at every notification, so a Search is checked while it
    // progresses as well as once it completes.
    int notifications = 0;
    int wrongNotifications = 0;
    QObject::connect( filtered_data.get(), &LogFilteredData::searchStateChanged,
                      filtered_data.get(), [ & ]( const SearchSession::State& ) {
                          ++notifications;
                          if ( displayedLines( *filtered_data )
                               != expectedDisplayedLines( *filtered_data ) ) {
                              ++wrongNotifications;
                          }
                      } );

    SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };

    requestSearch( filtered_data.get(), EveryTenthLine, searchStateSpy );
    REQUIRE( notifications > 0 );
    REQUIRE( wrongNotifications == 0 );
    checkDisplayedLines( "a Search completed" );
    REQUIRE( filtered_data->getNbLine() > filtered_data->getNbMatches() );

    filtered_data->addMark( 5_lnum );
    checkDisplayedLines( "a Mark was added" );

    filtered_data->toggleMark( 15_lnum );
    checkDisplayedLines( "a Mark was toggled on" );
    filtered_data->toggleMark( 15_lnum );
    checkDisplayedLines( "a Mark was toggled off" );

    filtered_data->deleteMark( 5_lnum );
    checkDisplayedLines( "a Mark was deleted" );

    filtered_data->addMark( 5_lnum );
    filtered_data->addMark( 25_lnum );
    filtered_data->clearMarks();
    checkDisplayedLines( "Marks were cleared" );

    auto searchPolicy = policies.search;
    searchPolicy.contextLinesCount = 1;
    logDataLoader.log_data.setSearchPolicy( searchPolicy );
    checkDisplayedLines( "the Context Lines count changed" );

    filtered_data->addMark( 5_lnum );
    for ( const auto visibility :
          { LogFilteredData::Visibility{ VisibilityFlags::Matches },
            LogFilteredData::Visibility{ VisibilityFlags::Marks },
            LogFilteredData::Visibility{ VisibilityFlags::Context },
            VisibilityFlags::Matches | VisibilityFlags::Marks,
            VisibilityFlags::Matches | VisibilityFlags::Context,
            VisibilityFlags::Marks | VisibilityFlags::Context, AllVisible } ) {
        filtered_data->setVisibility( visibility );
        checkDisplayedLines( "the visibility changed" );
    }

    requestSearch( filtered_data.get(), "this is line [0-9]{5}5", searchStateSpy );
    checkDisplayedLines( "a second Search completed" );

    requestSearch( filtered_data.get(), EveryTenthLine, searchStateSpy );
    REQUIRE( filtered_data->searchState().fromCache );
    checkDisplayedLines( "a Search hit the cache" );

    filtered_data->request();
    checkDisplayedLines( "the Search was cleared" );

    REQUIRE( wrongNotifications == 0 );
}

SCENARIO( "A continued Search shows the same lines as the Search run from scratch",
          "[logdata][search][context]" )
{
    const auto policies = contextLinesPolicies();
    LogDataLoader logDataLoader{ policies, ContextLinesFileLines };
    const RegularExpressionPattern pattern( EveryTenthLine );
    const auto half = LineNumber( ContextLinesFileLines / 2 );
    const auto whole = LineNumber( ContextLinesFileLines );

    const auto waitForCompletion = []( SafeQSignalSpy& spy ) {
        REQUIRE( waitUiState( [ & ]() {
            return spy.count() > 0
                   && lastSearchState( spy ).phase == SearchSession::Phase::Complete;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    };

    // Marks on a Match, next to one and near where the first run stopped.
    const auto addMarks = []( LogFilteredData& filtered ) {
        for ( const auto line : { 5_lnum, 20_lnum, 998_lnum, 1003_lnum } ) {
            filtered.addMark( line );
        }
        filtered.setVisibility( AllVisible );
    };

    auto continued = logDataLoader.log_data.getNewFilteredData();
    addMarks( *continued );
    int wrongNotifications = 0;
    QObject::connect( continued.get(), &LogFilteredData::searchStateChanged, continued.get(),
                      [ & ]( const SearchSession::State& ) {
                          if ( displayedLines( *continued )
                               != expectedDisplayedLines( *continued ) ) {
                              ++wrongNotifications;
                          }
                      } );
    {
        SafeQSignalSpy spy{ continued.get(), &LogFilteredData::searchStateChanged };
        continued->request( pattern, 0_lnum, half );
        waitForCompletion( spy );
    }
    {
        SafeQSignalSpy spy{ continued.get(), &LogFilteredData::searchStateChanged };
        continued->request( pattern, 0_lnum, whole );
        REQUIRE( continued->searchState().isContinuation );
        waitForCompletion( spy );
    }
    REQUIRE( wrongNotifications == 0 );

    auto fromScratch = logDataLoader.log_data.getNewFilteredData();
    addMarks( *fromScratch );
    {
        SafeQSignalSpy spy{ fromScratch.get(), &LogFilteredData::searchStateChanged };
        fromScratch->request( pattern, 0_lnum, whole );
        REQUIRE_FALSE( fromScratch->searchState().isContinuation );
        waitForCompletion( spy );
    }

    const auto lineTypes = []( const LogFilteredData& filtered ) {
        std::vector<LineType::Int> types;
        for ( LineNumber::UnderlyingType line = 0; line < ContextLinesFileLines; ++line ) {
            types.push_back(
                static_cast<LineType::Int>( filtered.lineTypeByLine( LineNumber( line ) ) ) );
        }
        return types;
    };

    REQUIRE( toFlags( continued->lineTypeByLine( 1005_lnum ) ) == LineTypeFlags::Context );
    REQUIRE( displayedLines( *continued ) == displayedLines( *fromScratch ) );
    REQUIRE( lineTypes( *continued ) == lineTypes( *fromScratch ) );

    WHEN( "a Mark is toggled off and on again" )
    {
        continued->toggleMark( 1003_lnum );
        fromScratch->toggleMark( 1003_lnum );
        REQUIRE( toFlags( continued->lineTypeByLine( 1005_lnum ) ) == LineTypeFlags::Plain );
        REQUIRE( displayedLines( *continued ) == displayedLines( *fromScratch ) );
        REQUIRE( lineTypes( *continued ) == lineTypes( *fromScratch ) );

        continued->toggleMark( 1003_lnum );
        fromScratch->toggleMark( 1003_lnum );
        THEN( "both still show the same lines" )
        {
            REQUIRE( displayedLines( *continued ) == displayedLines( *fromScratch ) );
            REQUIRE( lineTypes( *continued ) == lineTypes( *fromScratch ) );
        }
    }
}

SCENARIO( "A continued Search drops a last Log Line that stopped matching",
          "[logdata][search][context]" )
{
    // Excluding "fizz": the Log Lines without it are the Matches. The last
    // Log Line is still being written and has no "fizz" yet, so it matches
    // for now; once the rest of it arrives, it does not.
    QTemporaryFile file{ "filtered_test_stale_match_XXXXXX" };
    REQUIRE( file.open() );
    file.write( "drop fizz\nkeep\ndrop fizz\ndrop fizz\ndrop fizz\npartial" );
    file.flush();

    auto policies = testSettingsPolicies();
    policies.search.contextLinesCount = 1;
    policies.search.useParallelSearch = false;
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    REQUIRE( logData.getNbLine() == 6_lcount );

    auto filtered = logData.getNewFilteredData();
    filtered->setVisibility( AllVisible );
    const RegularExpressionPattern exclude( "fizz", true, true, false, false );

    const auto waitForCompletion = []( SafeQSignalSpy& spy ) {
        REQUIRE( waitUiState( [ & ]() {
            return spy.count() > 0
                   && lastSearchState( spy ).phase == SearchSession::Phase::Complete;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    };

    GIVEN( "a completed Search that matched the incomplete last Log Line" )
    {
        {
            SafeQSignalSpy spy{ filtered.get(), &LogFilteredData::searchStateChanged };
            filtered->request( exclude, 0_lnum, 6_lnum );
            waitForCompletion( spy );
        }

        REQUIRE( filtered->getNbMatches() == 2_lcount );
        REQUIRE( toFlags( filtered->lineTypeByLine( 5_lnum ) ) == LineTypeFlags::Match );
        REQUIRE( displayedLines( *filtered ) == LineNumbers{ 0, 1, 2, 4, 5 } );

        WHEN( "the Log File grows so that this Log Line no longer matches" )
        {
            REQUIRE( file.write( " fizz\ndrop fizz\nkeep\ndrop fizz\n" ) > 0 );
            file.flush();
            {
                SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
                logData.fileChangedOnDisk( file.fileName() );
                REQUIRE( loadEndSpy.safeWait( 10000 ) );
            }
            REQUIRE( logData.getNbLine() == 9_lcount );

            {
                SafeQSignalSpy spy{ filtered.get(), &LogFilteredData::searchStateChanged };
                filtered->request( exclude, 0_lnum, 9_lnum );
                REQUIRE( filtered->searchState().isContinuation );
                waitForCompletion( spy );
            }

            THEN( "neither the count nor the Displayed Lines include it any more" )
            {
                REQUIRE( filtered->getNbMatches() == 2_lcount );
                REQUIRE( toFlags( filtered->lineTypeByLine( 5_lnum ) ) == LineTypeFlags::Plain );
                REQUIRE( displayedLines( *filtered ) == LineNumbers{ 0, 1, 2, 6, 7, 8 } );
                REQUIRE( displayedLines( *filtered ) == expectedDisplayedLines( *filtered ) );
            }
        }
    }
}

SCENARIO( "iterating over the Filtered View's lines while making lookups from the callback",
          "[logdata][search][context]" )
{
    LogDataLoader logDataLoader{ contextLinesPolicies(), ContextLinesFileLines };

    auto filtered_data = logDataLoader.log_data.getNewFilteredData();
    filtered_data->setVisibility( AllVisible );
    SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };
    requestSearch( filtered_data.get(), EveryTenthLine, searchStateSpy );
    filtered_data->addMark( 5_lnum );

    const auto expected = expectedDisplayedLines( *filtered_data );
    REQUIRE( filtered_data->getNbLine() > filtered_data->getNbMatches() );

    LineNumbers iterated;
    LineNumbers lookedUp;
    filtered_data->iterateOverLines( [ & ]( LineNumber line ) {
        iterated.push_back( line.get() );
        const auto index = filtered_data->getLineIndexNumber( line );
        lookedUp.push_back( filtered_data->getMatchingLineNumber( index ).get() );
        static_cast<void>( filtered_data->getNbLine() );
        static_cast<void>( filtered_data->lineTypeByIndex( index ) );
    } );

    REQUIRE( iterated == expected );
    REQUIRE( lookedUp == expected );
}

SCENARIO( "the Filtered View's lines can be read from a second thread while the UI thread reads "
          "them",
          "[logdata][search][context][threading]" )
{
    LogDataLoader logDataLoader{ contextLinesPolicies(), ContextLinesFileLines };

    auto filtered_data = logDataLoader.log_data.getNewFilteredData();
    filtered_data->setVisibility( AllVisible );
    SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };
    requestSearch( filtered_data.get(), EveryTenthLine, searchStateSpy );
    REQUIRE( filtered_data->getNbLine() > filtered_data->getNbMatches() );

    // No change is in progress from here on: only reads.
    std::atomic<bool> stop{ false };
    std::atomic<int> workerErrors{ 0 };

    // Reads the way the Filtered View's QuickFind does on its worker thread.
    std::thread quickFindThread( [ & ]() {
        do {
            const auto nbLines = filtered_data->getNbLine().get();
            for ( LineNumber::UnderlyingType index = 0; index < nbLines; ++index ) {
                if ( filtered_data->getExpandedLineString( LineNumber( index ) ).isEmpty() ) {
                    ++workerErrors;
                }
            }
        } while ( !stop );
    } );

    // Reads the way painting and selection do on the UI thread.
    int uiErrors = 0;
    const auto nbLines = filtered_data->getNbLine().get();
    for ( int round = 0; round < 20; ++round ) {
        for ( LineNumber::UnderlyingType index = 0; index < nbLines; ++index ) {
            const auto line = filtered_data->getMatchingLineNumber( LineNumber( index ) );
            if ( filtered_data->getLineIndexNumber( line ) != LineNumber( index ) ) {
                ++uiErrors;
            }
        }
    }

    stop = true;
    quickFindThread.join();

    REQUIRE( uiErrors == 0 );
    REQUIRE( workerErrors == 0 );
}

SCENARIO( "The Displayed Lines read in a block as each of them does on its own",
          "[logdata][sparse-read]" )
{
    auto policies = testSettingsPolicies();
    policies.search.contextLinesCount = 2;
    LogDataLoader logDataLoader( policies );
    const auto& logData = logDataLoader.log_data;

    auto filtered_data = logDataLoader.log_data.getNewFilteredData();
    filtered_data->setVisibility( AllVisible );
    SafeQSignalSpy searchStateSpy{ filtered_data.get(), &LogFilteredData::searchStateChanged };
    requestSearch( filtered_data.get(), EveryTenthLine, searchStateSpy );
    filtered_data->addMark( 5_lnum );
    filtered_data->addMark( 256_lnum );
    filtered_data->addMark( 499_lnum );

    const auto nbDisplayed = filtered_data->getNbLine();
    REQUIRE( nbDisplayed > filtered_data->getNbMatches() );

    // What the Filtered View showed at each position when it read them one
    // by one: the displayed Log Line's text, or nothing past the last one.
    const auto oneByOne = [ & ]( LineNumber first, LinesCount count, bool expanded ) {
        std::vector<QString> text;
        for ( auto position = first; position < first + count; ++position ) {
            if ( position < nbDisplayed ) {
                const auto line = filtered_data->getMatchingLineNumber( position );
                text.push_back( expanded ? logData.getExpandedLineString( line )
                                         : logData.getLineString( line ) );
            }
            else {
                text.emplace_back();
            }
        }
        return text;
    };

    const auto expanded = GENERATE( false, true );
    const auto [ first, count ]
        = GENERATE( std::pair{ 0_lnum, 30_lcount }, std::pair{ 17_lnum, 1_lcount },
                    std::pair{ 40_lnum, 60_lcount }, std::pair{ 0_lnum, 0_lcount } );
    CAPTURE( expanded, first, count );
    const auto read = [ &, expanded = expanded ]( LineNumber from, LinesCount number ) {
        const auto lines = expanded ? filtered_data->getExpandedLines( from, number )
                                    : filtered_data->getLines( from, number );
        return std::vector<QString>( lines.begin(), lines.end() );
    };

    THEN( "a block of positions reads as each position does on its own" )
    {
        REQUIRE( read( first, count ) == oneByOne( first, count, expanded ) );
    }

    THEN( "a block reaching past the last position reads nothing there" )
    {
        const auto from = LineNumber( nbDisplayed.get() - 3 );
        REQUIRE( read( from, 10_lcount ) == oneByOne( from, 10_lcount, expanded ) );
        REQUIRE( read( from, 10_lcount )[ 3 ].isEmpty() );
    }
}

SCENARIO( "The Filtered View is as wide as its longest Mark as Marks come and go",
          "[logdata][marks]" )
{
    // Log Line n is n + 1 characters long.
    QTemporaryFile file{ "filtered_test_mark_lengths_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < 100; ++line ) {
        file.write( QByteArray( line + 1, 'x' ) + "\n" );
    }
    file.flush();

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    auto filtered_data = logData.getNewFilteredData();
    REQUIRE( filtered_data->getMaxLength() == 0_length );

    GIVEN( "Marks on Log Lines 9, 29, 19 and 49, one of them marked twice" )
    {
        filtered_data->addMark( 9_lnum );
        filtered_data->toggleMark( 29_lnum );
        filtered_data->addMark( 19_lnum );
        filtered_data->addMark( 49_lnum );
        filtered_data->addMark( 49_lnum );

        THEN( "it is as wide as the longest Mark" )
        {
            REQUIRE( filtered_data->getMaxLength() == LineLength( 50 ) );
        }

        WHEN( "the longest Marks are removed one after another" )
        {
            filtered_data->deleteMark( 49_lnum );
            REQUIRE( filtered_data->getMaxLength() == LineLength( 30 ) );
            filtered_data->toggleMark( 29_lnum );
            REQUIRE( filtered_data->getMaxLength() == LineLength( 20 ) );

            THEN( "a shorter Mark removed, or a Log Line not marked, leaves the width" )
            {
                filtered_data->deleteMark( 9_lnum );
                filtered_data->deleteMark( 70_lnum );
                REQUIRE( filtered_data->getMaxLength() == LineLength( 20 ) );
            }
        }

        WHEN( "every Mark is cleared" )
        {
            filtered_data->clearMarks();

            THEN( "it is as wide as no Mark" )
            {
                REQUIRE( filtered_data->getMaxLength() == 0_length );
            }
        }

        // Log Line n of the Log File written instead is 100 - n characters long.
        const auto rewriteLogFile = [ &file ] {
            QFile rewritten( file.fileName() );
            REQUIRE( rewritten.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            for ( int line = 0; line < 100; ++line ) {
                rewritten.write( QByteArray( 100 - line, 'y' ) + "\n" );
            }
        };

        WHEN( "the Log File is written again with other lengths and reloaded" )
        {
            rewriteLogFile();
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.reload();
            REQUIRE( loadEndSpy.safeWait( 10000 ) );

            THEN( "it is as wide as the longest Mark reads now" )
            {
                REQUIRE( filtered_data->getMaxLength() == LineLength( 91 ) );
            }
        }

        WHEN( "the Log File is cut short under Log Lines 29 and 49 and indexed again" )
        {
            {
                QFile truncated( file.fileName() );
                REQUIRE( truncated.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
                for ( int line = 0; line < 25; ++line ) {
                    truncated.write( QByteArray( 30 - line, 'z' ) + "\n" );
                }
            }
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.fileChangedOnDisk( file.fileName() );
            REQUIRE( loadEndSpy.safeWait( 10000 ) );
            REQUIRE( logData.getNbLine() == 25_lcount );

            THEN( "the Marks past its end are no wider than nothing" )
            {
                REQUIRE( filtered_data->getMaxLength() == LineLength( 21 ) );
            }
        }
    }
}
