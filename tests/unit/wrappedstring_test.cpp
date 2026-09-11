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

#include <memory>
#include <optional>

#include <QString>

#include "wrappedstring.h"

SCENARIO( "WrappedString wraps on word boundaries", "[wrappedstring]" )
{
    GIVEN( "A line longer than the visible columns" )
    {
        const WrappedString wrapped{ QString( "hello wonderful world" ), 12_length };

        THEN( "it breaks after the last space that fits" )
        {
            REQUIRE( wrapped.wrappedLinesCount() == 3 );
            REQUIRE( wrapped.wrappedLine( 0 ) == QStringView( u"hello " ) );
            REQUIRE( wrapped.wrappedLine( 1 ) == QStringView( u"wonderful " ) );
            REQUIRE( wrapped.wrappedLine( 2 ) == QStringView( u"world" ) );
        }
    }

    GIVEN( "A line with no spaces at all" )
    {
        const WrappedString wrapped{ QString( "abcdefghij" ), 4_length };

        THEN( "it breaks at the column limit" )
        {
            REQUIRE( wrapped.wrappedLinesCount() == 3 );
            REQUIRE( wrapped.wrappedLine( 0 ) == QStringView( u"abcd" ) );
            REQUIRE( wrapped.wrappedLine( 2 ) == QStringView( u"ij" ) );
        }
    }

    GIVEN( "An empty line" )
    {
        const WrappedString wrapped{ QString(), 10_length };

        THEN( "it still occupies one row" )
        {
            REQUIRE( wrapped.isEmpty() );
            REQUIRE( wrapped.wrappedLinesCount() == 1 );
        }
    }

    GIVEN( "A degenerate column count of zero" )
    {
        const WrappedString wrapped{ QString( "abc" ), 0_length };

        THEN( "the whole line becomes one row instead of looping forever" )
        {
            REQUIRE( wrapped.wrappedLinesCount() == 1 );
            REQUIRE( wrapped.wrappedLine( 0 ) == QStringView( u"abc" ) );
        }
    }
}

SCENARIO( "WrappedString owns the text it was built from", "[wrappedstring]" )
{
    GIVEN( "A WrappedString built from a string that is then destroyed" )
    {
        auto source = std::make_unique<QString>( "alpha beta gamma delta" );
        std::optional<WrappedString> wrapped{ std::in_place, *source, 11_length };

        WHEN( "the source string is gone" )
        {
            source.reset();

            THEN( "the wrapped rows still read correctly" )
            {
                REQUIRE( wrapped->wrappedLinesCount() == 2 );
                REQUIRE( wrapped->wrappedLine( 0 ) == QStringView( u"alpha beta " ) );
                REQUIRE( wrapped->unwrappedLine() == QStringView( u"alpha beta gamma delta" ) );
            }
        }
    }

    GIVEN( "A copy of a WrappedString whose original is destroyed" )
    {
        // This is the case the paint loop hits: the wrapped line is copied into
        // the viewport's row table and the original goes out of scope.
        auto original
            = std::make_unique<WrappedString>( QString( "one two three four five" ), 9_length );
        WrappedString copy = *original;

        WHEN( "the original is destroyed" )
        {
            const auto expectedRows = copy.wrappedLinesCount();
            original.reset();

            THEN( "the copy still reads its own storage" )
            {
                REQUIRE( copy.wrappedLinesCount() == expectedRows );
                REQUIRE( copy.unwrappedLine() == QStringView( u"one two three four five" ) );
                REQUIRE( copy.wrappedLine( 0 ) == QStringView( u"one two " ) );

                const auto chunks = copy.mid( 0_lcol, 12_length );
                QString joined;
                for ( const auto& chunk : chunks ) {
                    joined += chunk.toString();
                }
                REQUIRE( joined == QString( "one two thre" ) );
            }
        }
    }
}
