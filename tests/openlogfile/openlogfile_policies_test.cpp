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

// Settings arrive as a snapshot, so a changed setting needs an explicit path
// back to whatever is already running (#95). The Open Log File is the end of
// that path for the Indexing, Search and Decoding Policies: it takes every
// Policy, and hands them to the Searches it has, the kept ones included,
// which nothing else holds a list of (#556). No widget is needed for it.

#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QSignalSpy>
#include <QTemporaryFile>

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int LineCount = 400;

bool generateTestFile( QTemporaryFile& file )
{
    char line[ 120 ];
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < LineCount; i++ ) {
        snprintf( line, sizeof( line ), "POLICY_CHANGE_TEST this is line %06d\n", i );
        file.write( line, static_cast<qint64>( qstrlen( line ) ) );
    }
    file.flush();
    return true;
}

using LineTypeFlags = LogFilteredData::LineTypeFlags;

LineTypeFlags lineTypeOf( const LogFilteredData& data, LineNumber line )
{
    return static_cast<LineTypeFlags>(
        static_cast<LogFilteredData::LineType::Int>( data.lineTypeByLine( line ) ) );
}

// An Open Log File of the Log File, loaded, under the Policies given.
struct OpenedLogFile {
    OpenedLogFile( const QString& path, const SettingsPolicies& settingsPolicies )
        : policies( settingsPolicies )
        , openLogFile( policies.indexing, policies.search, policies.fileAccess, policies.decoding,
                       policies.recognition, std::make_shared<LogFormatCatalog>(), nullptr )
    {
        QSignalSpy loaded( &openLogFile, &OpenLogFile::loadingFinished );
        openLogFile.open( path );
        REQUIRE( waitUiState( [ &loaded ] { return loaded.count() > 0; }, 10000 ) );
    }

    // Requests the current Search for pattern, which matches one Log Line,
    // and waits until it is complete.
    void searchForSingleLine( const QString& pattern )
    {
        openLogFile.requestSearch( RegularExpressionPattern( pattern ) );
        // The Search runs on a worker thread: wait until its completion, which
        // publishes the Matches with it, has reached this object.
        const auto& search = openLogFile.filteredData();
        REQUIRE( waitUiState( [ &search ]() {
            return search->searchState().phase == SearchSession::Phase::Complete
                   && search->getNbMatches() == 1_lcount;
        } ) );
    }

    SettingsPolicies policies;
    OpenLogFile openLogFile;
};

} // namespace

SCENARIO( "A changed Search Policy reaches every Search of an Open Log File",
          "[openlogfile][settings]" )
{
    QTemporaryFile file{ "policy_change_XXXXXX" };
    REQUIRE( generateTestFile( file ) );

    GIVEN( "a kept Search and the current one, as a tab that kept a Search has" )
    {
        auto policies = testSettingsPolicies();
        policies.search.useResultsCache = false;
        policies.search.contextLinesCount = 1;

        OpenedLogFile logFile( file.fileName(), policies );

        logFile.searchForSingleLine( "this is line 000100" );
        auto first = logFile.openLogFile.filteredData();
        auto second = logFile.openLogFile.startAnotherSearch();
        logFile.searchForSingleLine( "this is line 000200" );

        // One Context Line either side of each match, as the Policy says.
        REQUIRE( lineTypeOf( *first, 99_lnum ) == LineTypeFlags::Context );
        REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Plain );
        REQUIRE( lineTypeOf( *second, 199_lnum ) == LineTypeFlags::Context );
        REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Plain );

        WHEN( "a Search Policy with a wider Context Lines count arrives" )
        {
            auto changed = policies.search;
            changed.contextLinesCount = 3;
            logFile.openLogFile.setSearchPolicy( changed );

            THEN( "both Searches rebuilt their Context Lines, not only the current one" )
            {
                REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Context );
            }

            THEN( "neither of them lost its matches" )
            {
                REQUIRE( lineTypeOf( *first, 100_lnum ) == LineTypeFlags::Match );
                REQUIRE( lineTypeOf( *second, 200_lnum ) == LineTypeFlags::Match );
            }
        }

        WHEN( "a Search Policy that changes some other part of the axis arrives" )
        {
            auto changed = policies.search;
            changed.useResultsCache = true;
            logFile.openLogFile.setSearchPolicy( changed );

            THEN( "Context Lines are left exactly as they were" )
            {
                REQUIRE( lineTypeOf( *first, 99_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *first, 97_lnum ) == LineTypeFlags::Plain );
                REQUIRE( lineTypeOf( *second, 199_lnum ) == LineTypeFlags::Context );
                REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Plain );
            }
        }

        WHEN( "the kept Search is dropped and a Policy arrives afterwards" )
        {
            first.reset();

            auto changed = policies.search;
            changed.contextLinesCount = 3;

            THEN( "the current one is still reached, and nothing dangles" )
            {
                logFile.openLogFile.setSearchPolicy( changed );
                REQUIRE( lineTypeOf( *second, 197_lnum ) == LineTypeFlags::Context );
            }
        }
    }
}

SCENARIO( "A changed Indexing Policy reaches an Open Log File that has loaded",
          "[openlogfile][settings]" )
{
    QTemporaryFile file{ "policy_change_indexing_XXXXXX" };
    REQUIRE( generateTestFile( file ) );

    GIVEN( "an indexed Log File and a Search on it" )
    {
        auto policies = testSettingsPolicies();
        policies.indexing.useCompressedIndex = true;

        OpenedLogFile logFile( file.fileName(), policies );
        logFile.searchForSingleLine( "this is line 000100" );

        WHEN( "an Indexing Policy with different storage arrives and the file is reloaded" )
        {
            auto changed = policies.indexing;
            changed.useCompressedIndex = false;
            logFile.openLogFile.setIndexingPolicy( changed );

            QSignalSpy loaded( &logFile.openLogFile, &OpenLogFile::loadingFinished );
            logFile.openLogFile.reload();
            REQUIRE( waitUiState( [ &loaded ] { return loaded.count() > 0; }, 10000 ) );

            THEN( "the reindex ran under the new Policy and the file is complete" )
            {
                REQUIRE( logFile.openLogFile.lineCount() == LinesCount( LineCount ) );
            }
        }
    }
}

SCENARIO( "A changed Decoding Policy reaches an Open Log File that has loaded",
          "[openlogfile][settings]" )
{
    QTemporaryFile file{ "policy_change_decoding_XXXXXX" };
    REQUIRE( file.open() );
    file.write( "plain line\n" );
    file.write( "\x1B[31mERROR\x1B[0m: disk full\n" );
    file.write( "another plain line\n" );
    file.flush();

    GIVEN( "a Log File with ANSI color sequences, opened showing them" )
    {
        auto policies = testSettingsPolicies();
        policies.search.useResultsCache = false;
        policies.decoding.hideAnsiColorSequences = false;

        OpenedLogFile logFile( file.fileName(), policies );
        const auto& logData = *logFile.openLogFile.logData();
        REQUIRE( logData.getLineString( 1_lnum ).contains( "\x1B[31m" ) );

        WHEN( "a Decoding Policy hiding them arrives" )
        {
            QSignalSpy told( &logFile.openLogFile, &OpenLogFile::decodingPolicyChanged );
            auto changed = policies.decoding;
            changed.hideAnsiColorSequences = true;
            logFile.openLogFile.setDecodingPolicy( changed );

            THEN( "the Log Line reads without them" )
            {
                REQUIRE( logData.getLineString( 1_lnum ) == "ERROR: disk full" );
            }

            THEN( "its users are told to read the Log Lines again" )
            {
                REQUIRE( told.count() == 1 );
            }

            THEN( "a Search matches the text they interrupted" )
            {
                logFile.searchForSingleLine( "ERROR: disk full" );
                REQUIRE( logFile.openLogFile.lineType( 1_lnum ).testFlag( LineTypeFlags::Match ) );
            }

            AND_WHEN( "a Decoding Policy showing them arrives again" )
            {
                logFile.openLogFile.setDecodingPolicy( policies.decoding );

                THEN( "the Log Line reads with them again" )
                {
                    REQUIRE( logData.getLineString( 1_lnum ).contains( "\x1B[31m" ) );
                }
            }
        }
    }
}

// Hide and Show colors hide ANSI color sequences alike: the Decoding Policy
// is the same, and only the Decoration Policy, which the Open Log File does
// not hold, tells them apart (#573). A view that shows the colors reads them
// from the Log File as it is, next to the Search already run.
SCENARIO( "Showing ANSI colors needs no other Decoding Policy than hiding the sequences",
          "[openlogfile][settings][ansi]" )
{
    QTemporaryFile file{ "policy_change_ansi_colors_XXXXXX" };
    REQUIRE( file.open() );
    file.write( "plain line\n" );
    file.write( "\x1B[31mERROR\x1B[0m: disk full\n" );
    file.write( "another plain line\n" );
    file.flush();

    GIVEN( "a Log File opened hiding its ANSI color sequences, with a Search run" )
    {
        auto policies = testSettingsPolicies();
        policies.search.useResultsCache = false;
        policies.decoding.hideAnsiColorSequences = true;

        OpenedLogFile logFile( file.fileName(), policies );
        logFile.searchForSingleLine( "ERROR: disk full" );
        const auto& logData = *logFile.openLogFile.logData();
        const auto& search = logFile.openLogFile.filteredData();

        WHEN( "a view that shows the colors reads the Log Lines it shows" )
        {
            QSignalSpy told( &logFile.openLogFile, &OpenLogFile::decodingPolicyChanged );
            QSignalSpy searchRuns( search.get(), &LogFilteredData::searchStateChanged );

            const auto mainLines = logData.getAnsiColoredLines( 0_lnum, 3_lcount );
            const auto filteredLines = search->getAnsiColoredLines( 0_lnum, 1_lcount );

            THEN( "they come with their colors and the text the Search matched" )
            {
                REQUIRE( mainLines[ 1 ].text == logData.getLineString( 1_lnum ) );
                REQUIRE( mainLines[ 1 ].spans.size() == 1 );
                REQUIRE( filteredLines[ 0 ].text == "ERROR: disk full" );
                REQUIRE( filteredLines[ 0 ].spans == mainLines[ 1 ].spans );
            }

            THEN( "the Log File is not read again and the Search not run again" )
            {
                REQUIRE( told.isEmpty() );
                REQUIRE( searchRuns.isEmpty() );
                REQUIRE( search->getNbMatches() == 1_lcount );
            }
        }
    }
}
