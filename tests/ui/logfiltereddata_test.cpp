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

#include "configuration.h"
#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

static const qint64 SL_NB_LINES = 500LL;

namespace {

bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
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
    LogDataLoader()
        : log_data( testSettingsPolicies().indexing, testSettingsPolicies().search,
                    testSettingsPolicies().fileAccess )
    {
        static int counter = 0;
        counter++;
        LOG_INFO << "Test run " << counter;

        REQUIRE( generateDataFiles( file ) );
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

            auto& config = Configuration::getSynced();

            config.setSearchThreadPoolSize( threadPoolSize );
            config.setUseParallelSearch( threadPoolSize > 0 );

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
            auto& config = Configuration::getSynced();
            config.setSearchThreadPoolSize( 2 );
            config.setUseParallelSearch( true );

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

        auto& config = Configuration::getSynced();
        config.setSearchThreadPoolSize( 1 );
        config.setUseParallelSearch( false );
        config.setUseSearchResultsCache( false );

        const auto policies = testSettingsPolicies();
        LogData log_data{ policies.indexing, policies.search, policies.fileAccess };
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

SCENARIO( "A request repeating the same pattern and start with a grown end is a continuation",
         "[logdata][search]" )
{
    LogDataLoader logDataLoader;

    GIVEN( "a completed search over the first half of the file" )
    {
        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        const RegularExpressionPattern pattern( "LOGDATA" );

        SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                      &LogFilteredData::searchStateChanged };

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

                REQUIRE( waitUiState( [ & ]() {
                    return lastSearchState( searchStateSpy ).progress >= 100;
                } ) );
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
        auto& config = Configuration::getSynced();
        config.setUseSearchResultsCache( true );
        config.setContextLinesCount( 2 );

        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                      &LogFilteredData::searchStateChanged };

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
        auto& config = Configuration::getSynced();
        config.setUseSearchResultsCache( true );

        auto filtered_data = logDataLoader.log_data.getNewFilteredData();
        const RegularExpressionPattern patternA( "this is line 000010" ); // matches only line 10
        const RegularExpressionPattern patternB( "this is line 000200" ); // matches only line 200

        SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                      &LogFilteredData::searchStateChanged };

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
        SafeQSignalSpy searchStateSpy{ filtered_data.get(),
                                      &LogFilteredData::searchStateChanged };

        requestSearch( filtered_data.get(), "this is line [0-9]{5}9", searchStateSpy );
        REQUIRE( filtered_data->getNbMatches() == 50_lcount );

        WHEN( "an invalid pattern is requested" )
        {
            filtered_data->request( RegularExpressionPattern( "[unterminated" ), 0_lnum,
                                    LineNumber( SL_NB_LINES ) );

            THEN( "the previous results are gone, not just uncounted" )
            {
                REQUIRE( filtered_data->searchState().phase == SearchSession::Phase::InvalidPattern );
                REQUIRE( filtered_data->getNbMatches() == 0_lcount );
            }
        }
    }
}
