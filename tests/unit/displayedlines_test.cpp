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
        incremental.matchesArrived( newMatches );
        rebuilt.matchesArrived();
    }

    // Another Search replaced the Matches: neither is told by how much.
    void matchesReplaced( SearchResultArray matches )
    {
        logFile.matches = std::move( matches );
        incremental.matchesArrived();
        rebuilt.matchesArrived();
    }

    void searchDiscarded()
    {
        logFile.matches = SearchResultArray{};
        incremental.searchDiscarded();
        rebuilt.searchDiscarded();
    }

    void searchCompleted( const SearchResultArray& newMatches )
    {
        logFile.matches |= newMatches;
        incremental.searchCompleted( newMatches );
        rebuilt.searchCompleted();
    }

    void toggleMark( uint64_t line )
    {
        if ( !incremental.addMark( LineNumber( line ) ) ) {
            REQUIRE( incremental.removeMark( LineNumber( line ) ) );
            REQUIRE( rebuilt.removeMark( LineNumber( line ) ) );
        }
        else {
            REQUIRE( rebuilt.addMark( LineNumber( line ) ) );
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
