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

// The overview draws a line for the Matches and one for the other displayed
// Log Lines on each of its pixel rows, darker the more of them the row holds
// (#297): what it draws for a Search, Marks and Context Lines, while a Search
// adds Matches, and how often it recomputes while a Search runs.

#include "logdata.h"
#include "logfiltereddata.h"
#include "overview.h"
#include "overviewwidget.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QTemporaryFile>

#include <chrono>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

namespace {

constexpr int NbLogLines = 200;
const LinesCount LogLineCount{ NbLogLines };

// Matches every 10th Log Line: 0, 10, ... 190.
const RegularExpressionPattern EveryTenthLine{ "this is line [0-9]{5}0" };

// A loaded Log File whose Log Lines read "this is line NNNNNN".
struct OverviewLogFile {
    explicit OverviewLogFile( int contextLinesCount = 0 )
        : logData( policies.indexing, searchPolicy( contextLinesCount ), policies.fileAccess,
                   policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            file.write( QStringLiteral( "this is line %1\n" )
                            .arg( line, 6, 10, QLatin1Char( '0' ) )
                            .toLatin1() );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
    }

    SearchPolicy searchPolicy( int contextLinesCount )
    {
        auto search = policies.search;
        search.contextLinesCount = contextLinesCount;
        return search;
    }

    // Searches Log Lines [0, end) and waits until the Search completed.
    void search( uint64_t end, uint64_t expectedMatches )
    {
        filteredData->request( EveryTenthLine, 0_lnum, LineNumber( end ) );
        REQUIRE( waitUiState( [ & ]() {
            const auto state = filteredData->searchState();
            return state.phase == SearchSession::Phase::Complete
                   && state.matchCount == LinesCount( expectedMatches );
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "overview_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

using Lines = std::vector<std::pair<int, int>>;

Lines linesOf( const logsquirl::vector<Overview::WeightedLine>& weighted )
{
    Lines lines;
    for ( const auto& line : weighted ) {
        lines.emplace_back( line.position(), line.weight() );
    }
    return lines;
}

Lines everyRow( int rows, int weight )
{
    Lines lines;
    for ( int row = 0; row < rows; ++row ) {
        lines.emplace_back( row, weight );
    }
    return lines;
}

Lines matchLines( const Overview& overview )
{
    return linesOf( *overview.getMatchLines() );
}

Lines markLines( const Overview& overview )
{
    return linesOf( *overview.getMarkLines() );
}

// A clock a test moves by hand.
struct ManualClock {
    std::chrono::steady_clock::time_point now{ std::chrono::steady_clock::now() };

    Overview::Clock clock()
    {
        return [ this ]() { return now; };
    }
};

} // namespace

SCENARIO( "The overview draws the Matches and the other displayed Log Lines on its rows",
          "[overview]" )
{
    GIVEN( "a Search matching every 10th of 200 Log Lines" )
    {
        OverviewLogFile logFile;
        logFile.search( NbLogLines, 20 );

        Overview overview;
        overview.setFilteredData( logFile.filteredData.get() );
        overview.updateData( LogLineCount );

        THEN( "20 rows hold one Match each" )
        {
            overview.updateView( 20 );
            REQUIRE( matchLines( overview ) == everyRow( 20, 0 ) );
            REQUIRE( markLines( overview ).empty() );
        }

        THEN( "5 rows hold four Matches each, drawn darkest" )
        {
            overview.updateView( 5 );
            REQUIRE( matchLines( overview ) == everyRow( 5, 2 ) );
        }

        THEN( "7 rows hold three Matches each but the last, which holds two" )
        {
            overview.updateView( 7 );
            REQUIRE(
                matchLines( overview )
                == Lines{ { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, 2 }, { 5, 2 }, { 6, 1 } } );
        }

        THEN( "400 rows hold a Match on every 20th row" )
        {
            overview.updateView( 400 );
            Lines expected;
            for ( int row = 0; row < 400; row += 20 ) {
                expected.emplace_back( row, 0 );
            }
            REQUIRE( matchLines( overview ) == expected );
        }

        WHEN( "Log Lines 5, 10 and 15 are marked" )
        {
            logFile.filteredData->addMark( 5_lnum );
            logFile.filteredData->addMark( 10_lnum );
            logFile.filteredData->addMark( 15_lnum );
            overview.updateData( LogLineCount );
            overview.updateView( 20 );

            THEN( "the Marks that are not Matches are drawn beside the Matches" )
            {
                REQUIRE( matchLines( overview ) == everyRow( 20, 0 ) );
                REQUIRE( markLines( overview ) == Lines{ { 0, 0 }, { 1, 0 } } );
            }

            WHEN( "only the Marks are displayed" )
            {
                logFile.filteredData->setVisibility( LogFilteredData::VisibilityFlags::Marks );
                overview.updateData( LogLineCount );
                overview.updateView( 20 );

                THEN( "the marked Match is drawn as a Match, the other Marks as Marks" )
                {
                    REQUIRE( matchLines( overview ) == Lines{ { 1, 0 } } );
                    REQUIRE( markLines( overview ) == Lines{ { 0, 0 }, { 1, 0 } } );
                }
            }
        }
    }

    GIVEN( "a Search matching every 10th Log Line with one Context Line either side, displayed" )
    {
        OverviewLogFile logFile( 1 );
        logFile.filteredData->setVisibility( LogFilteredData::VisibilityFlags::Matches
                                             | LogFilteredData::VisibilityFlags::Marks
                                             | LogFilteredData::VisibilityFlags::Context );
        logFile.search( NbLogLines, 20 );

        Overview overview;
        overview.setFilteredData( logFile.filteredData.get() );
        overview.updateData( LogLineCount );
        overview.updateView( 20 );

        THEN( "each row holds a Match, and two Context Lines drawn darker but the last" )
        {
            // Row 19 holds Log Lines 190 to 199: 199 is no Context Line, as
            // there is no Match on 200.
            auto contextLines = everyRow( 19, 1 );
            contextLines.emplace_back( 19, 0 );
            REQUIRE( matchLines( overview ) == everyRow( 20, 0 ) );
            REQUIRE( markLines( overview ) == contextLines );
        }
    }
}

SCENARIO( "The overview follows a Search that adds Matches after the last ones", "[overview]" )
{
    for ( const bool withMark : { false, true } ) {
        GIVEN( "a Search over the first 100 of 200 Log Lines, drawn on 20 rows"
               << ( withMark ? ", with Log Line 5 marked" : "" ) )
        {
            OverviewLogFile logFile;
            if ( withMark ) {
                logFile.filteredData->addMark( 5_lnum );
            }
            logFile.search( 100, 10 );

            Overview overview;
            overview.setFilteredData( logFile.filteredData.get() );
            overview.updateData( LogLineCount );
            overview.updateView( 20 );
            REQUIRE( matchLines( overview ) == everyRow( 10, 0 ) );

            WHEN( "the Search goes on over the other 100" )
            {
                const auto rewrites = logFile.filteredData->displayedLinesRewrites();
                logFile.search( NbLogLines, 20 );
                // Only Matches after the last ones were added: the overview
                // counts only the rows from its last line again.
                REQUIRE( logFile.filteredData->displayedLinesRewrites() == rewrites );
                overview.updateData( LogLineCount );
                overview.updateView( 20 );

                THEN( "it draws the Matches of all 200" )
                {
                    REQUIRE( matchLines( overview ) == everyRow( 20, 0 ) );
                    REQUIRE( markLines( overview ) == ( withMark ? Lines{ { 0, 0 } } : Lines{} ) );
                }
            }
        }
    }
}

SCENARIO( "The overview recomputes at a bounded rate while a Search runs", "[overview]" )
{
    OverviewLogFile logFile;
    logFile.search( NbLogLines, 20 );

    ManualClock clock;
    Overview overview( 200ms, clock.clock() );
    overview.setFilteredData( logFile.filteredData.get() );
    overview.updateData( LogLineCount );
    REQUIRE_FALSE( overview.updateView( 20 ).has_value() );

    WHEN( "a Search tick changes the lines 50 ms after the last recompute" )
    {
        logFile.filteredData->addMark( 5_lnum );
        clock.now += 50ms;
        overview.updateData( LogLineCount, Overview::UpdatePace::WhileSearching );

        THEN( "the overview keeps what it drew and says when it recomputes" )
        {
            REQUIRE( overview.updateView( 20 ) == std::optional{ 150ms } );
            REQUIRE( markLines( overview ).empty() );
        }

        THEN( "once the interval passed, it recomputes" )
        {
            clock.now += 150ms;
            REQUIRE_FALSE( overview.updateView( 20 ).has_value() );
            REQUIRE( markLines( overview ) == Lines{ { 0, 0 } } );
        }

        THEN( "a change outside a Search is drawn at once" )
        {
            overview.updateData( LogLineCount );
            REQUIRE_FALSE( overview.updateView( 20 ).has_value() );
            REQUIRE( markLines( overview ) == Lines{ { 0, 0 } } );
        }

        THEN( "a new height is drawn at once" )
        {
            REQUIRE_FALSE( overview.updateView( 40 ).has_value() );
            // Log Line 5 of 200 on row 5 * 40 / 200.
            REQUIRE( markLines( overview ) == Lines{ { 1, 0 } } );
        }
    }

    WHEN( "a Search tick changes the lines longer than the interval after the last recompute" )
    {
        logFile.filteredData->addMark( 5_lnum );
        clock.now += 1s;
        overview.updateData( LogLineCount, Overview::UpdatePace::WhileSearching );

        THEN( "it recomputes at once" )
        {
            REQUIRE_FALSE( overview.updateView( 20 ).has_value() );
            REQUIRE( markLines( overview ) == Lines{ { 0, 0 } } );
        }
    }
}

SCENARIO( "The overview widget catches up with a recompute it put off", "[overview]" )
{
    OverviewLogFile logFile;
    logFile.search( NbLogLines, 20 );

    Overview overview( 100ms );
    overview.setFilteredData( logFile.filteredData.get() );
    overview.setVisible( true );
    overview.updateData( LogLineCount );

    OverviewWidget widget;
    widget.setOverview( &overview );
    widget.resize( 20, 20 );
    widget.show();
    widget.repaint();
    REQUIRE( markLines( overview ).empty() );

    WHEN( "a Search tick changes the lines right after it painted" )
    {
        logFile.filteredData->addMark( 5_lnum );
        overview.updateData( LogLineCount, Overview::UpdatePace::WhileSearching );
        widget.repaint();

        THEN( "it paints them without being asked again" )
        {
            REQUIRE( waitUiState( [ & ]() { return markLines( overview ) == Lines{ { 0, 0 } }; },
                                  5000 ) );
        }
    }
}
