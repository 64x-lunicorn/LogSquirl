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

// The Filtered View scrolls by Visual Lines and stops at its bottom exactly as
// the main view does (#153, #154): the same checks as
// tests/unit/abstractlogview_test.cpp, run on a FilteredView showing the Matches
// of a Search over a real Log File.

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>

#include "filteredview.h"
#include "log_view_scrolling.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

// A Log File of the given Log Lines, loaded, and a Search that selects every
// one of its first searchedLines Log Lines (all of them by default).
struct FilteredLogFile {
    explicit FilteredLogFile( const QStringList& lines, qsizetype searchedLines = -1 )
        : logData( policies.indexing, policies.search, policies.fileAccess )
    {
        REQUIRE( file.open() );
        for ( const auto& line : lines ) {
            file.write( line.toLatin1() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
        searchUpTo( searchedLines < 0 ? lines.size() : searchedLines );
    }

    // Searches the first searchedLines Log Lines, and waits for the Matches.
    void searchUpTo( qsizetype searchedLines )
    {
        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( QStringLiteral( "." ) ), 0_lnum,
                               LineNumber( static_cast<uint64_t>( searchedLines ) ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

        REQUIRE( filteredData->getNbLine().get() == static_cast<uint64_t>( searchedLines ) );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "filtered_view_scrolling_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

} // namespace

SCENARIO( "The Filtered View scrolls by Visual Lines", "[filteredview][scrollposition]" )
{
    using namespace logviewscrolling;

    FilteredLogFile logFile{ tallLogLines() };
    QuickFindPattern quickFindPattern;
    FilteredView view( logFile.filteredData.get(), &quickFindPattern, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "a Match taller than the Viewport in the middle of the Search results" )
    {
        THEN( "a wheel step moves the same number of Visual Lines, inside it and across Log Lines, "
              "and reaches its last Visual Line" )
        {
            requireWheelStepsMoveTheSameVisualLinesEverywhere( view );
        }

        THEN( "an arrow key moves one Visual Line, into the next Log Line and back" )
        {
            requireArrowKeysStepOneVisualLineAcrossLogLines( view );
        }

        THEN( "Page Down followed by Page Up returns to the same Scroll Position" )
        {
            requirePageDownThenPageUpReturns( view );
        }

        THEN( "the scrollbar counts whole Log Lines" )
        {
            requireScrollbarCountsWholeLogLines( view );
        }

        THEN( "turning wrapping off and on keeps the same Log Line at the top" )
        {
            requireWrapToggleKeepsTheLogLineAtTheTop( view );
        }
    }
}

SCENARIO( "The bottom of the Filtered View shows exactly its last Visual Line",
          "[filteredview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern quickFindPattern;

    GIVEN( "Search results whose last Matches are one Visual Line each" )
    {
        FilteredLogFile logFile{ tallLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum the last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view );
        }

        THEN( "keys and the wheel go no further" )
        {
            requireScrollingStopsAtTheBottom( view );
        }
    }

    GIVEN( "Search results whose last Match is taller than the Viewport" )
    {
        FilteredLogFile logFile{ tallLastLogLines() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view );
        }

        THEN( "pages and the wheel scroll through it down to that same bottom" )
        {
            requireScrollingDownFromTheTopReachesTheBottom( view );
        }
    }

    GIVEN( "Search results with fewer Visual Lines than the Viewport has rows" )
    {
        FilteredLogFile logFile{ fewerVisualLinesThanRows() };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        THEN( "they show from their top, with no scroll range" )
        {
            requireFewerVisualLinesThanRowsShowFromTheTop( view );
        }
    }

    GIVEN( "a Search that has covered part of the Log File so far" )
    {
        const auto lines = tallLogLines();
        FilteredLogFile logFile{ lines, 50 };
        FilteredView view( logFile.filteredData.get(), &quickFindPattern, true );
        showOneColumnWide( view );

        const auto searchTheRest = [ & ]() {
            logFile.searchUpTo( lines.size() );
            view.updateData();
        };

        THEN( "follow mode keeps the new last Visual Line on the last row as Matches are added" )
        {
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, searchTheRest, LastWord );
        }

        THEN( "without follow mode, a view at the bottom stays where it is" )
        {
            requireGrowthWithoutFollowLeavesTheBottomView( view, searchTheRest );
        }
    }
}
