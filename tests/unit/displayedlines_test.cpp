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

#include "displayedlines.h"

#include <cstdint>
#include <initializer_list>
#include <random>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

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

// The matches deltas a Search Session hands over. Each holds on to the
// bitmaps it is given, so it is applied within the expression that makes it.
using Outcome = MatchesDelta::Outcome;
const SearchResultArray NoMatches;

// While a Search runs or when it stopped: the Matches were replaced.
MatchesDelta arrived()
{
    return MatchesDelta{ Outcome::Arrived, nullptr, NoMatches };
}
// While a Search runs or when it stopped: the Matches grew by added.
MatchesDelta arrived( const SearchResultArray& added )
{
    return MatchesDelta{ Outcome::Arrived, &added, NoMatches };
}
// While a Search continued over a grown Log File runs: the Matches grew by
// added and lost removed.
MatchesDelta arrived( const SearchResultArray& added, const SearchResultArray& removed )
{
    return MatchesDelta{ Outcome::Arrived, &added, removed };
}
// The Matches lost removed, and nothing arrived with it.
MatchesDelta removed( const SearchResultArray& removed )
{
    return arrived( NoMatches, removed );
}
// The Search completed with Matches that replaced the previous ones: a fresh
// run reporting all of them, or a cache hit.
MatchesDelta completed()
{
    return MatchesDelta{ Outcome::Completed, nullptr, NoMatches };
}
// The Search completed after the Matches grew by added.
MatchesDelta completed( const SearchResultArray& added )
{
    return MatchesDelta{ Outcome::Completed, &added, NoMatches };
}
// The Search was cleared, its pattern was invalid or it failed.
MatchesDelta discarded()
{
    return MatchesDelta{ Outcome::Discarded, nullptr, NoMatches };
}

} // namespace

SCENARIO( "The Displayed Lines combine Matches and Marks", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10, 20, 30 } );
    auto displayed = displayedLinesOf( logFile, 0 );
    displayed.apply( completed() );
    displayed.addMark( 15_lnum, 0_length );
    displayed.addMark( 20_lnum, 0_length );

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
    displayed.apply( completed() );
    displayed.addMark( 90_lnum, 0_length );
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

namespace {

// The Log Lines a cursor stands on, stepping forwards until it has none.
Lines walkForward( DisplayedLinesCursor cursor )
{
    Lines lines;
    for ( ; cursor.hasLine(); cursor.next() ) {
        lines.push_back( cursor.logLine().get() );
    }
    return lines;
}

// The Log Lines a cursor stands on, stepping backwards until it has none.
Lines walkBackward( DisplayedLinesCursor cursor )
{
    Lines lines;
    for ( ; cursor.hasLine(); cursor.previous() ) {
        lines.push_back( cursor.logLine().get() );
    }
    return lines;
}

Lines numbersOf( const logsquirl::vector<LineNumber>& lines )
{
    Lines numbers;
    for ( const auto line : lines ) {
        numbers.push_back( line.get() );
    }
    return numbers;
}

} // namespace

SCENARIO( "The Displayed Lines are walked from a position", "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 3, 7, 50 } );
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.apply( completed() );
    displayed.addMark( 90_lnum, 0_length );
    displayed.setShown( Everything );

    // Position:          0  1  2  3  4  5  6   7   8   9   10  11
    const Lines expected{ 2, 3, 4, 6, 7, 8, 49, 50, 51, 89, 90, 91 };

    THEN( "walking forwards from a position meets the Log Lines from it to the last" )
    {
        REQUIRE( walkForward( displayed.cursorAt( 0_lnum ) ) == expected );
        REQUIRE( walkForward( displayed.cursorAt( 7_lnum ) ) == Lines{ 50, 51, 89, 90, 91 } );
        REQUIRE( walkForward( displayed.cursorAt( 11_lnum ) ) == Lines{ 91 } );
    }

    THEN( "walking backwards from a position meets the Log Lines from it to the first" )
    {
        REQUIRE( walkBackward( displayed.cursorAt( 11_lnum ) )
                 == Lines{ 91, 90, 89, 51, 50, 49, 8, 7, 6, 4, 3, 2 } );
        REQUIRE( walkBackward( displayed.cursorAt( 4_lnum ) ) == Lines{ 7, 6, 4, 3, 2 } );
        REQUIRE( walkBackward( displayed.cursorAt( 0_lnum ) ) == Lines{ 2 } );
    }

    THEN( "the cursor knows the position it stands on" )
    {
        auto cursor = displayed.cursorAt( 5_lnum );
        REQUIRE( cursor.position() == 5_lnum );
        cursor.next();
        REQUIRE( cursor.position() == 6_lnum );
        REQUIRE( cursor.logLine() == 49_lnum );
        cursor.previous();
        cursor.previous();
        REQUIRE( cursor.position() == 4_lnum );
        REQUIRE( cursor.logLine() == 7_lnum );
    }

    THEN( "a position past the last displayed Log Line stands on none" )
    {
        auto cursor = displayed.cursorAt( 12_lnum );
        REQUIRE_FALSE( cursor.hasLine() );
        REQUIRE_FALSE( displayed.cursorAt( 1000_lnum ).hasLine() );
        REQUIRE_FALSE( displayed.cursorAt( maxValue<LineNumber>() ).hasLine() );

        WHEN( "it steps back" )
        {
            cursor.previous();

            THEN( "it stands on the last displayed Log Line" )
            {
                REQUIRE( cursor.hasLine() );
                REQUIRE( cursor.position() == 11_lnum );
                REQUIRE( cursor.logLine() == 91_lnum );
            }
        }
    }

    THEN( "stepping back before the first displayed Log Line and forwards again returns to it" )
    {
        auto cursor = displayed.cursorAt( 0_lnum );
        cursor.previous();
        REQUIRE_FALSE( cursor.hasLine() );
        cursor.next();
        REQUIRE( cursor.hasLine() );
        REQUIRE( cursor.position() == 0_lnum );
        REQUIRE( cursor.logLine() == 2_lnum );
    }

    THEN( "Log Lines are taken forwards in blocks" )
    {
        auto cursor = displayed.cursorAt( 2_lnum );
        REQUIRE( numbersOf( cursor.takeForward( 4_lcount ) ) == Lines{ 4, 6, 7, 8 } );
        REQUIRE( cursor.position() == 6_lnum );
        REQUIRE( numbersOf( cursor.takeForward( 4_lcount ) ) == Lines{ 49, 50, 51, 89 } );
        REQUIRE( numbersOf( cursor.takeForward( 4_lcount ) ) == Lines{ 90, 91 } );
        REQUIRE_FALSE( cursor.hasLine() );
        REQUIRE( cursor.takeForward( 4_lcount ).empty() );
    }

    THEN( "Log Lines are taken backwards in blocks, each block in ascending order" )
    {
        auto cursor = displayed.cursorAt( 9_lnum );
        REQUIRE( numbersOf( cursor.takeBackward( 4_lcount ) ) == Lines{ 49, 50, 51, 89 } );
        REQUIRE( cursor.position() == 5_lnum );
        REQUIRE( numbersOf( cursor.takeBackward( 4_lcount ) ) == Lines{ 4, 6, 7, 8 } );
        REQUIRE( numbersOf( cursor.takeBackward( 4_lcount ) ) == Lines{ 2, 3 } );
        REQUIRE_FALSE( cursor.hasLine() );
        REQUIRE( cursor.takeBackward( 4_lcount ).empty() );
    }
}

SCENARIO( "A copy of the Displayed Lines is walked from a position", "[displayedlines]" )
{
    // Far apart, so the Log Lines sit in different 32-bit buckets of the
    // bitmap, one of which is left empty by a removed line.
    const uint64_t high = uint64_t{ 1 } << 32;
    auto lines = bitmapOf( { 5, high + 1, 2 * high + 7, 3 * high + 2 } );
    lines.remove( 2 * high + 7 );

    THEN( "the walk passes the empty bucket both ways" )
    {
        REQUIRE( walkForward( DisplayedLinesCursor( lines, 0_lnum ) )
                 == Lines{ 5, high + 1, 3 * high + 2 } );
        REQUIRE( walkBackward( DisplayedLinesCursor( lines, 2_lnum ) )
                 == Lines{ 3 * high + 2, high + 1, 5 } );

        DisplayedLinesCursor pastTheEnd( lines, 3_lnum );
        pastTheEnd.previous();
        REQUIRE( pastTheEnd.logLine() == LineNumber( 3 * high + 2 ) );
    }

    THEN( "over no lines the cursor stands on none" )
    {
        const SearchResultArray none;
        DisplayedLinesCursor cursor( none, 0_lnum );
        REQUIRE_FALSE( cursor.hasLine() );
        cursor.previous();
        REQUIRE_FALSE( cursor.hasLine() );
        cursor.next();
        REQUIRE_FALSE( cursor.hasLine() );
        REQUIRE( cursor.takeForward( 3_lcount ).empty() );
        REQUIRE( cursor.takeBackward( 3_lcount ).empty() );
    }
}

SCENARIO( "The Displayed Lines compute Context Lines around Matches and Marks", "[displayedlines]" )
{
    LogFile logFile;
    logFile.nbLines = 40;
    logFile.matches = bitmapOf( { 1, 20 } );
    auto displayed = displayedLinesOf( logFile, 2 );
    displayed.setShown( Everything );
    displayed.apply( completed() );

    THEN( "Context Lines surround each Match, within the Log File" )
    {
        REQUIRE( linesOf( displayed.lines() ) == Lines{ 0, 1, 2, 3, 18, 19, 20, 21, 22 } );
        REQUIRE( displayed.lineType( 19_lnum ) == LineType{ LineTypeFlags::Context } );
        REQUIRE( displayed.lineType( 20_lnum ) == LineType{ LineTypeFlags::Match } );
    }

    WHEN( "a Mark is added near the end of the Log File" )
    {
        displayed.addMark( 39_lnum, 0_length );

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
        displayed.apply( discarded() );

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
    displayed.apply( completed() );

    WHEN( "Marks are added" )
    {
        REQUIRE( displayed.addMark( 5_lnum, 0_length ) );
        REQUIRE( displayed.addMark( 30_lnum, 0_length ) );

        THEN( "adding one twice reports it was already there" )
        {
            REQUIRE_FALSE( displayed.addMark( 5_lnum, 0_length ) );
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

SCENARIO( "The Displayed Lines keep the length of every Mark with the Mark",
          "[displayedlines][marks]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10 } );
    auto displayed = displayedLinesOf( logFile, 0 );
    displayed.apply( completed() );

    THEN( "without Marks the widest line is the longest Match" )
    {
        REQUIRE( displayed.maxLength( 0_length ) == 0_length );
        REQUIRE( displayed.maxLength( LineLength( 30 ) ) == LineLength( 30 ) );
    }

    GIVEN( "Marks of lengths 10, 40, 25 and a second one of 40" )
    {
        REQUIRE( displayed.addMark( 3_lnum, LineLength( 10 ) ) );
        REQUIRE( displayed.addMark( 7_lnum, LineLength( 40 ) ) );
        REQUIRE( displayed.addMark( 9_lnum, LineLength( 25 ) ) );
        REQUIRE( displayed.addMark( 12_lnum, LineLength( 40 ) ) );

        THEN( "with Marks only the widest line is the longest Mark" )
        {
            REQUIRE( displayed.maxLength( 0_length ) == LineLength( 40 ) );
        }

        THEN( "with Marks and Matches the widest line is the longer of the longest Mark and "
              "the longest Match" )
        {
            REQUIRE( displayed.maxLength( LineLength( 35 ) ) == LineLength( 40 ) );
            REQUIRE( displayed.maxLength( LineLength( 55 ) ) == LineLength( 55 ) );
        }

        WHEN( "a Mark is toggled off and on again" )
        {
            REQUIRE( displayed.removeMark( 7_lnum ) );
            REQUIRE( displayed.removeMark( 12_lnum ) );

            THEN( "the longest Mark left is the widest line while it is off" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 25 ) );
            }

            AND_WHEN( "it is marked again" )
            {
                REQUIRE( displayed.addMark( 7_lnum, LineLength( 40 ) ) );

                THEN( "its length counts again" )
                {
                    REQUIRE( displayed.maxLength( 0_length ) == LineLength( 40 ) );
                }
            }
        }

        WHEN( "one of the two longest is unmarked" )
        {
            REQUIRE( displayed.removeMark( 7_lnum ) );

            THEN( "the other one is still the longest" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 40 ) );
            }
        }

        WHEN( "a Log Line already marked is marked again with another length" )
        {
            REQUIRE_FALSE( displayed.addMark( 12_lnum, LineLength( 90 ) ) );

            THEN( "the length it was marked with stays" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 40 ) );
                REQUIRE( displayed.removeMark( 7_lnum ) );
                REQUIRE( displayed.removeMark( 12_lnum ) );
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 25 ) );
            }
        }

        WHEN( "the Marks are cleared" )
        {
            displayed.clearMarks();

            THEN( "no Mark is wide any more" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == 0_length );
            }

            AND_WHEN( "a shorter Log Line is marked" )
            {
                displayed.addMark( 7_lnum, LineLength( 5 ) );

                THEN( "only its length counts" )
                {
                    REQUIRE( displayed.maxLength( 0_length ) == LineLength( 5 ) );
                }
            }
        }
    }

    GIVEN( "a Mark on a long Log Line and one on the last Log Line" )
    {
        std::vector<LineLength> lengths( logFile.nbLines, LineLength( 20 ) );
        lengths[ 30 ] = LineLength( 60 );
        lengths[ 99 ] = LineLength( 5 );
        displayed.addMark( 30_lnum, lengths[ 30 ] );
        displayed.addMark( 99_lnum, lengths[ 99 ] );

        WHEN( "the last Log Line grew and the lengths are read again from there" )
        {
            lengths[ 30 ] = LineLength( 1 );
            lengths[ 99 ] = LineLength( 70 );
            Lines read;
            displayed.logLinesChanged( 99_lnum, [ & ]( LineNumber line ) {
                read.push_back( line.get() );
                return lengths[ line.get() ];
            } );

            THEN( "only the Marks from there on are read again, and the grown one is the widest" )
            {
                REQUIRE( read == Lines{ 99 } );
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 70 ) );
            }
        }

        WHEN( "the last Log Line was cut short and the lengths are read again from there" )
        {
            displayed.logLinesChanged( 50_lnum, []( LineNumber ) { return 0_length; } );

            THEN( "the Mark before the change keeps its length" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 60 ) );
            }
        }

        WHEN( "every Log Line changed" )
        {
            lengths[ 30 ] = LineLength( 8 );
            displayed.logLinesChanged( 0_lnum,
                                       [ & ]( LineNumber line ) { return lengths[ line.get() ]; } );

            THEN( "every Mark's length is read again" )
            {
                REQUIRE( displayed.maxLength( 0_length ) == LineLength( 8 ) );
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
    displayed.addMark( 50_lnum, 0_length );
    displayed.apply( completed() );
    REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 49, 50, 51 } );

    WHEN( "a Search in progress finds more Matches" )
    {
        logFile.matches.add( uint64_t{ 30 } );
        displayed.apply( arrived() );

        THEN( "they are displayed at once, their Context Lines once the Search completes" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 30, 49, 50, 51 } );
            REQUIRE( displayed.positionOf( 30_lnum ) == 3_lnum );

            displayed.apply( completed() );
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 29, 30, 31, 49, 50, 51 } );
        }
    }

    WHEN( "only Matches are shown and more arrive" )
    {
        displayed.setShown( LineTypeFlags::Match );
        logFile.matches.add( uint64_t{ 70 } );
        displayed.apply( arrived() );

        THEN( "they are displayed" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 70 } );
            REQUIRE( displayed.logLineAt( 1_lnum ) == OptionalLineNumber{ 70_lnum } );
        }
    }
}

SCENARIO( "The Displayed Lines drop a Match that stopped matching", "[displayedlines]" )
{
    // Log Line 30 was the incomplete last Log Line of the Log File; it
    // matched then and does not any more.
    LogFile logFile;
    logFile.nbLines = 40;
    logFile.matches = bitmapOf( { 10, 30 } );
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.setShown( Everything );
    displayed.apply( completed() );
    REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 29, 30, 31 } );

    WHEN( "the Match is removed" )
    {
        const auto rewritesBefore = displayed.rewrites();
        logFile.matches.remove( uint64_t{ 30 } );
        displayed.apply( removed( bitmapOf( { 30 } ) ) );

        THEN( "neither it nor its Context Lines are displayed any more" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11 } );
            REQUIRE( displayed.lineType( 30_lnum ) == LineType{ LineTypeFlags::Plain } );
            REQUIRE( displayed.lineType( 29_lnum ) == LineType{ LineTypeFlags::Plain } );
            REQUIRE( displayed.rewrites() != rewritesBefore );
        }
    }

    WHEN( "the Match is removed while a Mark sits next to it" )
    {
        displayed.addMark( 31_lnum, 0_length );
        logFile.matches.remove( uint64_t{ 30 } );
        displayed.apply( removed( bitmapOf( { 30 } ) ) );

        THEN( "it stays displayed as the Mark's Context Line" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 30, 31, 32 } );
            REQUIRE( displayed.lineType( 30_lnum ) == LineType{ LineTypeFlags::Context } );
        }
    }

    WHEN( "a Match that is Marked too is removed" )
    {
        displayed.addMark( 30_lnum, 0_length );
        logFile.matches.remove( uint64_t{ 30 } );
        displayed.apply( removed( bitmapOf( { 30 } ) ) );

        THEN( "it stays displayed as a Mark, with its Context Lines" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 29, 30, 31 } );
            REQUIRE( displayed.lineType( 30_lnum ) == LineType{ LineTypeFlags::Mark } );
        }
    }

    WHEN( "nothing is removed" )
    {
        const auto rewritesBefore = displayed.rewrites();
        displayed.apply( removed( SearchResultArray{} ) );

        THEN( "nothing changes" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 9, 10, 11, 29, 30, 31 } );
            REQUIRE( displayed.rewrites() == rewritesBefore );
        }
    }
}

namespace {

// What the Displayed Lines should be once their Context Lines are up to date,
// worked out Log Line by Log Line from the definitions: a Context Line is a
// Log Line of the Log File within reach of a Match or a Mark that is neither.
struct ExpectedLines {
    Lines lines;
    std::vector<LineType> types;
};

ExpectedLines expectedLinesOf( const LogFile& logFile, const SearchResultArray& marks, int reach,
                               LineType shown, uint64_t linesChecked )
{
    const bool matchesShown = shown.testFlag( LineTypeFlags::Match );
    const bool marksShown = shown.testFlag( LineTypeFlags::Mark ) || !matchesShown;
    const bool contextShown = shown.testFlag( LineTypeFlags::Context );

    const auto isMatchOrMark = [ & ]( uint64_t line ) {
        return logFile.matches.contains( line ) || marks.contains( line );
    };

    ExpectedLines expected;
    for ( uint64_t line = 0; line < linesChecked; ++line ) {
        LineType type = LineTypeFlags::Plain;
        if ( logFile.matches.contains( line ) ) {
            type |= LineTypeFlags::Match;
        }
        if ( marks.contains( line ) ) {
            type |= LineTypeFlags::Mark;
        }
        if ( type == LineTypeFlags::Plain && line < logFile.nbLines ) {
            for ( uint64_t distance = 1; distance <= static_cast<uint64_t>( reach ); ++distance ) {
                if ( ( line >= distance && isMatchOrMark( line - distance ) )
                     || isMatchOrMark( line + distance ) ) {
                    type |= LineTypeFlags::Context;
                    break;
                }
            }
        }
        expected.types.push_back( type );

        if ( ( matchesShown && type.testFlag( LineTypeFlags::Match ) )
             || ( marksShown && type.testFlag( LineTypeFlags::Mark ) )
             || ( contextShown && type.testFlag( LineTypeFlags::Context ) ) ) {
            expected.lines.push_back( line );
        }
    }
    return expected;
}

std::vector<LineType> lineTypesOf( const DisplayedLines& displayed, uint64_t linesChecked )
{
    std::vector<LineType> types;
    for ( uint64_t line = 0; line < linesChecked; ++line ) {
        types.push_back( displayed.lineType( LineNumber( line ) ) );
    }
    return types;
}

// Two Displayed Lines over the same Matches: one told of every change by its
// delta, the other told only that something changed, so it rebuilds.
struct IncrementalAndRebuilt {
    IncrementalAndRebuilt( LogFile& file, int contextLinesCount, LineType shown )
        : logFile( file )
        , reach( contextLinesCount )
        , incremental( displayedLinesOf( file, contextLinesCount ) )
        , rebuilt( displayedLinesOf( file, contextLinesCount ) )
    {
        incremental.setShown( shown );
        rebuilt.setShown( shown );
    }

    // Some Log Lines became Matches while the Search runs.
    void matchesArrived( const SearchResultArray& newMatches )
    {
        logFile.matches |= newMatches;
        incremental.apply( arrived( newMatches ) );
        rebuilt.apply( arrived() );
    }

    // Log Lines that were Matches are none any more: the previously last Log
    // Line of the Log File, searched again once it was complete. The twin to
    // compare against builds its Context Lines afresh around the Matches that
    // are left.
    void matchesRemoved( const SearchResultArray& removedMatches )
    {
        logFile.matches -= removedMatches;
        incremental.apply( removed( removedMatches ) );
        rebuilt.apply( completed() );
    }

    // Another Search replaced the Matches: neither is told by how much.
    void matchesReplaced( SearchResultArray matches )
    {
        logFile.matches = std::move( matches );
        incremental.apply( arrived() );
        rebuilt.apply( arrived() );
    }

    void searchDiscarded()
    {
        logFile.matches = SearchResultArray{};
        incremental.apply( discarded() );
        rebuilt.apply( discarded() );
    }

    void searchCompleted( const SearchResultArray& newMatches )
    {
        logFile.matches |= newMatches;
        incremental.apply( completed( newMatches ) );
        rebuilt.apply( completed() );
    }

    void toggleMark( uint64_t line )
    {
        if ( !incremental.addMark( LineNumber( line ), 0_length ) ) {
            REQUIRE( incremental.removeMark( LineNumber( line ) ) );
            REQUIRE( rebuilt.removeMark( LineNumber( line ) ) );
        }
        else {
            REQUIRE( rebuilt.addMark( LineNumber( line ), 0_length ) );
        }
    }

    uint64_t linesChecked() const
    {
        return logFile.nbLines + static_cast<uint64_t>( reach ) + 2;
    }

    // Both agree, whether or not the Context Lines are up to date.
    void requireSame() const
    {
        REQUIRE( linesOf( incremental.lines() ) == linesOf( rebuilt.lines() ) );
        REQUIRE( linesByPosition( incremental ) == linesOf( rebuilt.lines() ) );
        REQUIRE( lineTypesOf( incremental, linesChecked() )
                 == lineTypesOf( rebuilt, linesChecked() ) );
    }

    // Both agree with the definitions: every Match and Mark has its Context
    // Lines.
    void requireUpToDate() const
    {
        requireSame();
        const auto expected = expectedLinesOf( logFile, incremental.marks(), reach,
                                               incremental.shown(), linesChecked() );
        REQUIRE( linesOf( incremental.lines() ) == expected.lines );
        REQUIRE( lineTypesOf( incremental, linesChecked() ) == expected.types );
    }

    LogFile& logFile;
    int reach;
    DisplayedLines incremental;
    DisplayedLines rebuilt;
};

// Random Matches over [first, end), about one Log Line in density, split in
// batches the way a Search reports its progress.
std::vector<SearchResultArray> matchBatches( std::mt19937& random, uint64_t first, uint64_t end,
                                             int density, int nbBatches )
{
    std::vector<SearchResultArray> batches( static_cast<size_t>( nbBatches ) );
    std::uniform_int_distribution<int> isMatch( 0, density - 1 );
    std::uniform_int_distribution<int> batch( 0, nbBatches - 1 );
    for ( auto line = first; line < end; ++line ) {
        if ( isMatch( random ) == 0 ) {
            batches[ static_cast<size_t>( batch( random ) ) ].add( line );
        }
    }
    return batches;
}

const std::vector<LineType> ShownCombinations{
    LineType{ LineTypeFlags::Match },
    LineType{ LineTypeFlags::Mark },
    LineType{ LineTypeFlags::Context },
    LineType{ LineTypeFlags::Match } | LineTypeFlags::Mark,
    LineType{ LineTypeFlags::Match } | LineTypeFlags::Context,
    LineType{ LineTypeFlags::Mark } | LineTypeFlags::Context,
    Everything,
};

} // namespace

SCENARIO( "The Displayed Lines updated by their delta equal a full rebuild",
          "[displayedlines][incremental]" )
{
    const int contextLinesCount = GENERATE( 0, 1, 3 );
    const auto shown = GENERATE( from_range( ShownCombinations ) );
    INFO( "Context Lines: " << contextLinesCount << ", shown: " << shown.toInt() );

    std::mt19937 random( 292 );
    LogFile logFile;
    logFile.nbLines = 1000;
    IncrementalAndRebuilt displayed( logFile, contextLinesCount, shown );

    // Marks set before any Search: some will be Matches, some neighbours of
    // Matches, one on the last Log Line.
    for ( const uint64_t line : { 0u, 41u, 42u, 500u, 503u, 999u } ) {
        displayed.toggleMark( line );
    }
    displayed.requireUpToDate();

    WHEN( "a Search reports its Matches in progress ticks and completes" )
    {
        auto batches = matchBatches( random, 0, logFile.nbLines, 7, 8 );
        for ( size_t tick = 0; tick + 1 < batches.size(); ++tick ) {
            displayed.matchesArrived( batches[ tick ] );
            displayed.requireSame();
        }
        displayed.searchCompleted( batches.back() );

        THEN( "every Match and Mark has its Context Lines" )
        {
            displayed.requireUpToDate();
        }

        AND_WHEN( "Marks are toggled on and off, on Matches, next to them and on their own" )
        {
            std::uniform_int_distribution<uint64_t> line( 0, logFile.nbLines - 1 );
            for ( int toggle = 0; toggle < 60; ++toggle ) {
                displayed.toggleMark( line( random ) );
                displayed.requireUpToDate();
            }

            THEN( "removing every Mark leaves the Context Lines of the Matches alone" )
            {
                auto marks = linesOf( displayed.incremental.marks() );
                for ( const auto mark : marks ) {
                    displayed.toggleMark( mark );
                    displayed.requireUpToDate();
                }
                REQUIRE( displayed.incremental.marks().isEmpty() );
            }
        }

        AND_WHEN( "the Log File grows and the Search continues over the appended Log Lines" )
        {
            // A Match on the former last Log Line gets the Context Lines the
            // end of the Log File cut off.
            displayed.matchesArrived( bitmapOf( { 999 } ) - logFile.matches );
            displayed.toggleMark( 1003 );
            logFile.nbLines = 1500;

            auto appended = matchBatches( random, 1000, logFile.nbLines, 5, 4 );
            for ( size_t tick = 0; tick + 1 < appended.size(); ++tick ) {
                displayed.matchesArrived( appended[ tick ] );
                displayed.requireSame();
            }
            displayed.searchCompleted( appended.back() );

            THEN( "the appended Matches and those before them have their Context Lines" )
            {
                displayed.requireUpToDate();
            }
        }

        AND_WHEN( "the previously last Log Line stopped matching as the Log File grew" )
        {
            // It matched while it was incomplete, and its Match goes when the
            // continuation searches it again.
            displayed.matchesArrived( bitmapOf( { 999 } ) - logFile.matches );
            displayed.searchCompleted( SearchResultArray{} );
            displayed.requireUpToDate();

            logFile.nbLines = 1200;
            displayed.toggleMark( 1003 );
            displayed.matchesRemoved( bitmapOf( { 999 } ) );

            THEN( "the Displayed Lines equal a full rebuild without it" )
            {
                displayed.requireUpToDate();
            }

            AND_WHEN( "the continuation reports the Matches it found after it" )
            {
                auto appended = matchBatches( random, 1000, logFile.nbLines, 5, 3 );
                for ( size_t tick = 0; tick + 1 < appended.size(); ++tick ) {
                    displayed.matchesArrived( appended[ tick ] );
                    displayed.requireSame();
                }
                displayed.searchCompleted( appended.back() );
                displayed.requireUpToDate();

                THEN( "a Match removed among them equals a full rebuild too" )
                {
                    // The last Match of the Log File, whichever it is now.
                    REQUIRE_FALSE( logFile.matches.isEmpty() );
                    const auto lastMatch = logFile.matches.maximum();
                    displayed.matchesRemoved( bitmapOf( { lastMatch } ) );
                    displayed.requireUpToDate();

                    // And one in the middle, next to a Mark.
                    displayed.toggleMark( 501 );
                    displayed.requireUpToDate();
                    displayed.matchesRemoved( logFile.matches & bitmapOf( { 500, 502, 503 } ) );
                    displayed.requireUpToDate();
                }
            }
        }

        AND_WHEN( "another Search replaces the Matches and reports new ones by their delta" )
        {
            displayed.matchesReplaced( SearchResultArray{} );
            displayed.requireSame();
            auto batches2 = matchBatches( random, 0, logFile.nbLines, 11, 4 );
            for ( size_t tick = 0; tick + 1 < batches2.size(); ++tick ) {
                displayed.matchesArrived( batches2[ tick ] );
                displayed.requireSame();
            }
            displayed.searchCompleted( batches2.back() );

            THEN( "the Context Lines belong to the new Matches only" )
            {
                displayed.requireUpToDate();
            }
        }

        AND_WHEN( "the Search is discarded, a Mark toggled and another Search runs" )
        {
            displayed.searchDiscarded();
            displayed.requireSame();
            displayed.toggleMark( 300 );
            displayed.requireUpToDate();
            displayed.matchesReplaced( SearchResultArray{} );
            auto batches2 = matchBatches( random, 0, logFile.nbLines, 13, 3 );
            displayed.matchesArrived( batches2[ 0 ] );
            displayed.matchesArrived( batches2[ 1 ] );
            displayed.requireSame();
            displayed.searchCompleted( batches2[ 2 ] );

            THEN( "every Match and Mark has its Context Lines" )
            {
                displayed.requireUpToDate();
            }
        }

        AND_WHEN( "Marks are toggled while a Search continues" )
        {
            logFile.nbLines = 1200;
            auto appended = matchBatches( random, 1000, logFile.nbLines, 3, 3 );
            displayed.matchesArrived( appended[ 0 ] );
            displayed.toggleMark( 1010 );
            displayed.requireUpToDate();
            displayed.toggleMark( 1010 );
            displayed.requireUpToDate();
            displayed.matchesArrived( appended[ 1 ] );
            displayed.requireSame();
            displayed.searchCompleted( appended[ 2 ] );

            THEN( "every Match and Mark has its Context Lines" )
            {
                displayed.requireUpToDate();
            }
        }
    }
}

SCENARIO( "The Displayed Lines count the Matches and the other Log Lines in a range",
          "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10, 20, 30, 40 } );
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.apply( completed() );
    displayed.addMark( 20_lnum, 0_length );
    displayed.addMark( 25_lnum, 0_length );
    displayed.addMark( 60_lnum, 0_length );

    const auto requireCount
        = [ & ]( uint64_t first, uint64_t end, uint64_t matches, uint64_t others ) {
              INFO( "[" << first << ", " << end << ")" );
              const auto count = displayed.countIn( LineNumber( first ), LineNumber( end ) );
              REQUIRE( count.matches == matches );
              REQUIRE( count.others == others );
          };

    THEN( "with Matches and Marks shown, a marked Match counts as a Match" )
    {
        requireCount( 0, 100, 4, 2 );
        requireCount( 10, 21, 2, 0 );
        requireCount( 21, 60, 2, 1 );
        requireCount( 60, 61, 0, 1 );
        requireCount( 61, 100, 0, 0 );
        requireCount( 30, 30, 0, 0 );
    }

    WHEN( "the Context Lines are shown too" )
    {
        displayed.setShown( Everything );

        THEN( "they count with the Marks" )
        {
            // Context Lines 9, 11, 19, 21, 24, 26, 29, 31, 39, 41, 59, 61.
            requireCount( 0, 100, 4, 14 );
            requireCount( 9, 12, 1, 2 );
        }
    }

    WHEN( "only the Marks are shown" )
    {
        displayed.setShown( LineType{ LineTypeFlags::Mark } );

        THEN( "a marked Match still counts as a Match" )
        {
            requireCount( 0, 100, 1, 2 );
            requireCount( 21, 100, 0, 2 );
        }
    }
}

SCENARIO( "The Displayed Lines tell whether they changed only past their last Log Line",
          "[displayedlines]" )
{
    LogFile logFile;
    logFile.matches = bitmapOf( { 10, 20 } );
    auto displayed = displayedLinesOf( logFile, 0 );
    displayed.apply( completed() );
    displayed.addMark( 30_lnum, 0_length );
    auto rewrites = displayed.rewrites();

    WHEN( "Matches arrive after every Match and Mark" )
    {
        const auto newMatches = bitmapOf( { 40, 50 } );
        logFile.matches |= newMatches;
        displayed.apply( arrived( newMatches ) );

        THEN( "it is no rewrite, and neither is completing the Search after more of them" )
        {
            REQUIRE( displayed.rewrites() == rewrites );

            const auto lastMatches = bitmapOf( { 60 } );
            logFile.matches |= lastMatches;
            displayed.apply( completed( lastMatches ) );
            REQUIRE( displayed.rewrites() == rewrites );
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 20, 30, 40, 50, 60 } );
        }
    }

    WHEN( "a Match arrives before a Mark" )
    {
        const auto newMatches = bitmapOf( { 25, 40 } );
        logFile.matches |= newMatches;
        displayed.apply( arrived( newMatches ) );

        THEN( "it is a rewrite" )
        {
            REQUIRE( displayed.rewrites() != rewrites );
        }
    }

    WHEN( "the Matches are replaced" )
    {
        logFile.matches = bitmapOf( { 50 } );
        displayed.apply( arrived() );

        THEN( "it is a rewrite" )
        {
            REQUIRE( displayed.rewrites() != rewrites );
        }
    }

    WHEN( "a Mark is added past the last Log Line" )
    {
        displayed.addMark( 90_lnum, 0_length );

        THEN( "it is a rewrite, as is any change of the Marks or of what is shown" )
        {
            REQUIRE( displayed.rewrites() != rewrites );
            rewrites = displayed.rewrites();
            displayed.setShown( Everything );
            REQUIRE( displayed.rewrites() != rewrites );
        }
    }
}

SCENARIO( "The Displayed Lines apply the matches deltas of a Search", "[displayedlines]" )
{
    // The deltas in the order a Search Session hands them over, applied
    // without a Session, an event loop or a worker.
    LogFile logFile;
    auto displayed = displayedLinesOf( logFile, 1 );
    displayed.setShown( Everything );
    displayed.addMark( 50_lnum, 0_length );
    // An earlier Search completed without a Match: the Mark has its Context
    // Lines.
    displayed.apply( completed() );
    REQUIRE( linesOf( displayed.lines() ) == Lines{ 49, 50, 51 } );

    WHEN( "a Search starts and its first Matches arrive" )
    {
        // A fresh run replaces the Matches before it finds any.
        displayed.apply( arrived() );
        const auto first = bitmapOf( { 10, 30 } );
        logFile.matches |= first;
        displayed.apply( arrived( first ) );

        THEN( "they are displayed as Matches, without Context Lines yet" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 10, 30, 49, 50, 51 } );
            REQUIRE( displayed.lineType( 10_lnum ) == LineType{ LineTypeFlags::Match } );
            REQUIRE( displayed.lineType( 11_lnum ) == LineType{ LineTypeFlags::Plain } );
            REQUIRE( displayed.lineType( 49_lnum ) == LineType{ LineTypeFlags::Context } );
        }

        AND_WHEN( "more arrive together with the removal of one of them" )
        {
            const auto added = bitmapOf( { 70 } );
            const auto gone = bitmapOf( { 30 } );
            logFile.matches |= added;
            logFile.matches -= gone;
            displayed.apply( arrived( added, gone ) );

            THEN( "the removed one is gone, and the Context Lines follow at once" )
            {
                // A Match that is gone must not keep Context Lines nothing
                // reaches any more, so they come up to date around every Match
                // there is, the ones that arrived with the removal included.
                REQUIRE( linesOf( displayed.lines() )
                         == Lines{ 9, 10, 11, 49, 50, 51, 69, 70, 71 } );
                REQUIRE( displayed.lineType( 30_lnum ) == LineType{ LineTypeFlags::Plain } );
                REQUIRE( displayed.lineType( 70_lnum ) == LineType{ LineTypeFlags::Match } );
            }

            AND_WHEN( "the Search completes with its last Matches" )
            {
                const auto last = bitmapOf( { 90 } );
                logFile.matches |= last;
                displayed.apply( completed( last ) );

                THEN( "every Match has its Context Lines" )
                {
                    REQUIRE( linesOf( displayed.lines() )
                             == Lines{ 9, 10, 11, 49, 50, 51, 69, 70, 71, 89, 90, 91 } );
                    REQUIRE( displayed.lineType( 11_lnum ) == LineType{ LineTypeFlags::Context } );
                    REQUIRE( displayed.lineType( 90_lnum ) == LineType{ LineTypeFlags::Match } );
                    REQUIRE( displayed.lineType( 29_lnum ) == LineType{ LineTypeFlags::Plain } );
                }
            }
        }

        AND_WHEN( "the Search completes with a removal and nothing new" )
        {
            const auto gone = bitmapOf( { 10 } );
            logFile.matches -= gone;
            displayed.apply( MatchesDelta{ Outcome::Completed, &NoMatches, gone } );

            THEN( "the Matches left have their Context Lines, the removed one none" )
            {
                REQUIRE( linesOf( displayed.lines() ) == Lines{ 29, 30, 31, 49, 50, 51 } );
                REQUIRE( displayed.lineType( 10_lnum ) == LineType{ LineTypeFlags::Plain } );
            }
        }
    }

    WHEN( "a Search completes from the cache" )
    {
        logFile.matches = bitmapOf( { 20, 60 } );
        displayed.apply( completed() );

        THEN( "its Matches are displayed with their Context Lines" )
        {
            REQUIRE( linesOf( displayed.lines() ) == Lines{ 19, 20, 21, 49, 50, 51, 59, 60, 61 } );
            REQUIRE( displayed.lineType( 60_lnum ) == LineType{ LineTypeFlags::Match } );
            REQUIRE( displayed.lineType( 61_lnum ) == LineType{ LineTypeFlags::Context } );
        }

        AND_WHEN( "the Search is discarded" )
        {
            logFile.matches = SearchResultArray{};
            displayed.apply( discarded() );

            THEN( "only the Mark is displayed, without Context Lines" )
            {
                REQUIRE( linesOf( displayed.lines() ) == Lines{ 50 } );
                REQUIRE( displayed.lineType( 20_lnum ) == LineType{ LineTypeFlags::Plain } );
                REQUIRE( displayed.lineType( 49_lnum ) == LineType{ LineTypeFlags::Plain } );
                REQUIRE( displayed.lineType( 50_lnum ) == LineType{ LineTypeFlags::Mark } );
            }
        }
    }
}
