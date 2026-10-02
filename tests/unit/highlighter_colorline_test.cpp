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

// What a single Highlighter colors of a line, as a Highlighter Set colors it
// and the Regex Lab shows it (#660).

#include <catch2/catch_test_macros.hpp>

#include "highlighter.h"

namespace {

HighlighterMatchType colorLine( const QString& pattern, bool onlyMatch, const QString& line,
                                logsquirl::vector<HighlightedMatch>& matches )
{
    const Highlighter highlighter( pattern, false, onlyMatch, Qt::black, Qt::yellow );
    return highlighter.colorLine( line, matches );
}

HighlighterMatchType setMatch( const QString& pattern, bool onlyMatch, const QString& line )
{
    HighlighterSet set = HighlighterSet::createNewSet( QStringLiteral( "Set" ) );
    set.addHighlighter( Highlighter( pattern, false, onlyMatch, Qt::black, Qt::yellow ) );
    HighlightedMatchRanges ranges;
    return set.matchLine( line, ranges );
}

} // namespace

SCENARIO( "A Highlighter colors a line as its Highlighter Set does", "[highlighters]" )
{
    logsquirl::vector<HighlightedMatch> matches;

    GIVEN( "a Highlighter that colors the whole Log Line" )
    {
        THEN( "a matching line is colored whole, and no match is left" )
        {
            CHECK( colorLine( "id=[0-9]+", false, "user id=7", matches )
                   == HighlighterMatchType::LineMatch );
            CHECK( matches.empty() );
            CHECK( setMatch( "id=[0-9]+", false, "user id=7" ) == HighlighterMatchType::LineMatch );
        }
    }

    GIVEN( "a Highlighter that colors only the matched text, with a group" )
    {
        THEN( "the text of the group is what is colored" )
        {
            REQUIRE( colorLine( "id=([0-9]+)", true, "user id=17", matches )
                     == HighlighterMatchType::WordMatch );
            REQUIRE( matches.size() == 1 );
            CHECK( matches[ 0 ].startColumn().get() == 8 );
            CHECK( matches[ 0 ].size().get() == 2 );
        }
    }

    GIVEN( "a pattern whose only group is optional, on a line where it takes no part" )
    {
        THEN( "the line is not colored, whole or in part, by the Highlighter nor by its set" )
        {
            for ( const auto onlyMatch : { false, true } ) {
                CHECK( colorLine( "(foo)?bar", onlyMatch, "bar", matches )
                       == HighlighterMatchType::NoMatch );
                CHECK( setMatch( "(foo)?bar", onlyMatch, "bar" ) == HighlighterMatchType::NoMatch );
                CHECK( colorLine( "(foo)?bar", onlyMatch, "foobar", matches )
                       != HighlighterMatchType::NoMatch );
            }
        }
    }

    GIVEN( "a line longer than a Highlighter colors" )
    {
        const auto line = QString( Highlighter::MaxHighlightLineLength, QChar( 'a' ) ) + "id=7";

        THEN( "it is not colored" )
        {
            CHECK( colorLine( "id=7", false, line, matches ) == HighlighterMatchType::NoMatch );
            CHECK( setMatch( "id=7", false, line ) == HighlighterMatchType::NoMatch );
        }
    }
}
