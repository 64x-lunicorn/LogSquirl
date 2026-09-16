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

#include <catch2/catch.hpp>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "displayedlines.h"

// The Displayed Lines over plain bitmaps: the Matches stand in for the ones
// a Search Session holds, and the Log File is just a line count.

namespace {

using LineTypeFlags = DisplayedLines::LineTypeFlags;
using LineType = DisplayedLines::LineType;
using Lines = std::vector<uint64_t>;

SearchResultArray bitmapOf( std::initializer_list<uint64_t> lines )
{
    SearchResultArray bitmap;
    for ( const auto line : lines ) {
        bitmap.add( line );
    }
    return bitmap;
}

Lines linesOf( const SearchResultArray& bitmap )
{
    Lines lines;
    for ( const auto line : bitmap ) {
        lines.push_back( line );
    }
    return lines;
}

// The Log Lines at each position, read one position at a time.
Lines linesByPosition( const DisplayedLines& displayed )
{
    Lines lines;
    for ( uint64_t position = 0; position < displayed.count().get(); ++position ) {
        const auto line = displayed.logLineAt( LineNumber( position ) );
        REQUIRE( line.has_value() );
        lines.push_back( line->get() );
    }
    return lines;
}

const LineType MatchesAndMarks = LineType{ LineTypeFlags::Match } | LineTypeFlags::Mark;
const LineType Everything = MatchesAndMarks | LineTypeFlags::Context;

struct LogFile {
    uint64_t nbLines = 100;
    SearchResultArray matches;
};

DisplayedLines displayedLinesOf( LogFile& logFile, int contextLinesCount )
{
    return DisplayedLines{ logFile.matches, [ &logFile ] { return LinesCount( logFile.nbLines ); },
                           contextLinesCount };
}

} // namespace

SCENARIO( "The Displayed Lines combine Matches and Marks", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10, 20, 30 } );
    auto displayed = displayedLinesOf( logFile, 0 );
    displayed.searchCompleted();
    displayed.addMark( 15_lnum );
    displayed.addMark( 20_lnum );

    THEN( "Matches and Marks are shown by default, each Log Line once" )
    {
        REQUIRE( displayed.shown() == MatchesAndMarks );
        REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 15, 20, 30 } );
        REQUIRE( displayed.count() == 4_lcount );
    }

    THEN( "each Log Line has its type" )
    {
        REQUIRE( displayed.lineType( 10_lnum ) == LineType{ LineTypeFlags::Match } );
        REQUIRE( displayed.lineType( 15_lnum ) == LineType{ LineTypeFlags::Mark } );
        REQUIRE( displayed.lineType( 20_lnum ) == MatchesAndMarks );
        REQUIRE( displayed.lineType( 11_lnum ) == LineType{ LineTypeFlags::Plain } );
    }

    WHEN( "only the Matches are shown" )
    {
        displayed.setShown( LineTypeFlags::Match );

        THEN( "the Marks that do not match are left out" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 20, 30 } );
        }
    }

    WHEN( "the Matches are hidden" )
    {
        displayed.setShown( LineTypeFlags::Context );

        THEN( "the Marks are shown" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 15, 20 } );
        }
    }
}

SCENARIO( "The Displayed Lines map positions to Log Lines and back", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 3, 7, 50 } );
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.searchCompleted();
    displayed.addMark( 90_lnum );
    displayed.setShown( Everything );

    const Lines expected{ 2, 3, 4, 6, 7, 8, 49, 50, 51, 89, 90, 91 };

    THEN( "each position holds the next displayed Log Line" )
    {
        REQUIRE( linesByPosition( displayed ) == expected );
    }

    THEN( "each displayed Log Line maps back to its position" )
    {
        for ( uint64_t position = 0; position < expected.size(); ++position ) {
            REQUIRE( displayed.positionOf( LineNumber( expected[ position ] ) )
                     == LineNumber( position ) );
        }
    }

    THEN( "a position past the last displayed Log Line holds none" )
    {
        REQUIRE_FALSE( displayed.logLineAt( LineNumber( expected.size() ) ).has_value() );
    }
}

SCENARIO( "The Displayed Lines compute Context Lines around Matches and Marks", "[displayedlines]" )
{
    LogFile logFile;
    logFile.nbLines = 40;
    logFile.matches = bitmapOf( { 1, 20 } );
    auto displayed = displayedLinesOf( logFile, 2 );
    displayed.setShown( Everything );
    displayed.searchCompleted();

    THEN( "Context Lines surround each Match, within the Log File" )
    {
        REQUIRE( linesOf( displayed.lines() ) == Lines{ 0, 1, 2, 3, 18, 19, 20, 21, 22 } );
        REQUIRE( displayed.lineType( 19_lnum ) == LineType{ LineTypeFlags::Context } );
        REQUIRE( displayed.lineType( 20_lnum ) == LineType{ LineTypeFlags::Match } );
    }

    WHEN( "a Mark is added near the end of the Log File" )
    {
        displayed.addMark( 39_lnum );

        THEN( "Context Lines surround it too, not past the last Log Line" )
        {
            REQUIRE( linesOf( displayed.lines() )
                     == Lines{ 0, 1, 2, 3, 18, 19, 20, 21, 22, 37, 38, 39 } );
            REQUIRE( displayed.lineType( 38_lnum ) == LineType{ LineTypeFlags::Context } );
        }

        AND_WHEN( "the Mark is removed again" )
        {
            REQUIRE( displayed.removeMark( 39_lnum ) );

            THEN( "its Context Lines go with it" )
            {
                REQUIRE( linesOf( displayed.lines() ) == Lines{ 0, 1, 2, 3, 18, 19, 20, 21, 22 } );
                REQUIRE( displayed.lineType( 38_lnum ) == LineType{ LineTypeFlags::Plain } );
            }
        }
    }

    WHEN( "Context Lines are hidden" )
    {
        displayed.setShown( MatchesAndMarks );

        THEN( "only the Matches are displayed, though the Context Lines are still known" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 1, 20 } );
            REQUIRE( displayed.lineType( 19_lnum ) == LineType{ LineTypeFlags::Context } );
        }
    }

    WHEN( "the Context Lines count changes" )
    {
        displayed.setContextLinesCount( 1 );

        THEN( "the Context Lines are rebuilt with it" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 0, 1, 2, 19, 20, 21 } );
        }
    }

    WHEN( "the Search is discarded" )
    {
        logFile.matches = SearchResultArray{};
        displayed.searchDiscarded();

        THEN( "neither Matches nor their Context Lines are displayed" )
        {
            REQUIRE( displayed.count() == 0_lcount );
            REQUIRE( displayed.lineType( 19_lnum ) == LineType{ LineTypeFlags::Plain } );
        }
    }
}

SCENARIO( "The Displayed Lines keep their Marks", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10 } );
    auto displayed = displayedLinesOf( logFile, 0 );
    displayed.searchCompleted();

    WHEN( "Marks are added" )
    {
        REQUIRE( displayed.addMark( 5_lnum ) );
        REQUIRE( displayed.addMark( 30_lnum ) );

        THEN( "adding one twice reports it was already there" )
        {
            REQUIRE_FALSE( displayed.addMark( 5_lnum ) );
            REQUIRE( linesOf( displayed.marks() ) == Lines{ 5, 30 } );
        }

        THEN( "they are displayed between the Matches" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 5, 10, 30 } );
            REQUIRE( displayed.positionOf( 30_lnum ) == 2_lnum );
        }

        THEN( "the next and previous Mark are strictly after and before a Log Line" )
        {
            REQUIRE( displayed.markAfter( 5_lnum ) == OptionalLineNumber{ 30_lnum } );
            REQUIRE( displayed.markAfter( 4_lnum ) == OptionalLineNumber{ 5_lnum } );
            REQUIRE_FALSE( displayed.markAfter( 30_lnum ).has_value() );
            REQUIRE( displayed.markBefore( 30_lnum ) == OptionalLineNumber{ 5_lnum } );
            REQUIRE_FALSE( displayed.markBefore( 5_lnum ).has_value() );
        }

        AND_WHEN( "one is removed" )
        {
            REQUIRE( displayed.removeMark( 5_lnum ) );

            THEN( "it is no longer displayed, and removing it again reports so" )
            {
                REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 30 } );
                REQUIRE_FALSE( displayed.removeMark( 5_lnum ) );
            }
        }

        AND_WHEN( "they are cleared" )
        {
            displayed.clearMarks();

            THEN( "only the Matches are displayed" )
            {
                REQUIRE( displayed.marks().isEmpty() );
                REQUIRE( linesOf( displayed.lines() ) == Lines{ 10 } );
            }
        }
    }
}

SCENARIO( "The Displayed Lines follow new Matches arriving", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10 } );
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.setShown( Everything );
    displayed.addMark( 50_lnum );
    displayed.searchCompleted();
    REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 49, 50, 51 } );

    WHEN( "a Search in progress finds more Matches" )
    {
        logFile.matches.add( uint64_t{ 30 } );
        displayed.matchesArrived();

        THEN( "they are displayed at once, their Context Lines once the Search completes" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 30, 49, 50, 51 } );
            REQUIRE( displayed.positionOf( 30_lnum ) == 3_lnum );

            displayed.searchCompleted();
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 29, 30, 31, 49, 50, 51 } );
        }
    }

    WHEN( "only Matches are shown and more arrive" )
    {
        displayed.setShown( LineTypeFlags::Match );
        logFile.matches.add( uint64_t{ 70 } );
        displayed.matchesArrived();

        THEN( "they are displayed" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 70 } );
            REQUIRE( displayed.logLineAt( 1_lnum ) == OptionalLineNumber{ 70_lnum } );
        }
    }
}
