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

#include <catch2/catch_test_macros.hpp>

#include "wordrule.h"

#include <QString>

using Word = std::optional<std::pair<int, int>>;

SCENARIO( "A word is a run of letters, numbers and connector punctuation", "[wordrule]" )
{
    GIVEN( "words of letters, numbers and underscores" )
    {
        const QString text = "foo_bar 42x";

        THEN( "any character of a word gives the whole word" )
        {
            REQUIRE( wordAt( text, 0 ) == Word{ { 0, 7 } } );
            REQUIRE( wordAt( text, 3 ) == Word{ { 0, 7 } } );
            REQUIRE( wordAt( text, 6 ) == Word{ { 0, 7 } } );
            REQUIRE( wordAt( text, 9 ) == Word{ { 8, 11 } } );
        }
    }

    GIVEN( "a word joined by connector punctuation other than the underscore" )
    {
        const auto text = QString( "a foo" ) + QChar( 0x203F ) + "bar." + QChar( 0x2040 );

        THEN( "the connector punctuation belongs to the word" )
        {
            REQUIRE( wordAt( text, 2 ) == Word{ { 2, 9 } } );
            REQUIRE( wordAt( text, 5 ) == Word{ { 2, 9 } } );
        }
    }

    GIVEN( "letters beyond ASCII" )
    {
        const QString text = QString::fromUtf8( "grüße wörld" );

        THEN( "they are letters of a word" )
        {
            REQUIRE( wordAt( text, 2 ) == Word{ { 0, 5 } } );
            REQUIRE( wordAt( text, 7 ) == Word{ { 6, 11 } } );
        }
    }

    GIVEN( "separators between words" )
    {
        const QString text = "a b.c-d\tx";

        THEN( "a separator gives no word" )
        {
            for ( const auto separator : { 1, 3, 5, 7 } ) {
                REQUIRE( wordAt( text, separator ) == Word{} );
            }
        }
    }

    THEN( "a position outside the text gives no word" )
    {
        REQUIRE( wordAt( QString(), 0 ) == Word{} );
        REQUIRE( wordAt( QString( "abc" ), -1 ) == Word{} );
        REQUIRE( wordAt( QString( "abc" ), 3 ) == Word{} );
    }
}
