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

// The regex wrapper compiles patterns for whichever engine takes them, and says
// so when none does (#444). What it matches is the same on every engine, so
// these cases go through the matcher variant and do not name the engine.

#include <catch2/catch_test_macros.hpp>

#include "hsregularexpression.h"

#include <string_view>
#include <variant>

namespace {

MatchedPatterns matchWith( const HsRegularExpression& expression, std::string_view line )
{
    auto matcher = expression.createMatcher();
    return std::visit( [ & ]( const auto& m ) { return m.match( line ); }, matcher );
}

RegularExpressionPattern plain( const QString& text, bool caseSensitive = true )
{
    return RegularExpressionPattern( text, caseSensitive, false, false, true );
}

} // namespace

SCENARIO( "A pattern that does not compile is reported, not matched", "[hsregex][regex]" )
{
    GIVEN( "An unbalanced group" )
    {
        const HsRegularExpression expression( RegularExpressionPattern( "(abc" ) );

        THEN( "The expression is invalid and says why" )
        {
            REQUIRE( !expression.isValid() );
            REQUIRE( !expression.errorString().isEmpty() );
        }
    }

    GIVEN( "One bad pattern among good ones" )
    {
        const HsRegularExpression expression( logsquirl::vector<RegularExpressionPattern>{
            RegularExpressionPattern( "fine" ), RegularExpressionPattern( "[unclosed" ),
            RegularExpressionPattern( "also fine" ) } );

        THEN( "The whole expression is invalid" )
        {
            REQUIRE( !expression.isValid() );
            REQUIRE( !expression.errorString().isEmpty() );
        }
    }

    GIVEN( "A pattern that only looks broken as a regex, taken as plain text" )
    {
        const HsRegularExpression expression( plain( "(abc" ) );

        THEN( "It is valid and matches the text literally" )
        {
            REQUIRE( expression.isValid() );
            REQUIRE( matchWith( expression, "call (abc now" ) == MatchedPatterns( 1, '\1' ) );
            REQUIRE( matchWith( expression, "call abc now" ) == MatchedPatterns( 1, '\0' ) );
        }
    }
}

SCENARIO( "The wrapper reports which patterns matched a line", "[hsregex][regex]" )
{
    GIVEN( "Several patterns" )
    {
        const HsRegularExpression expression( logsquirl::vector<RegularExpressionPattern>{
            RegularExpressionPattern( "error" ), RegularExpressionPattern( "^warn" ),
            RegularExpressionPattern( "[0-9]+ms" ) } );
        REQUIRE( expression.isValid() );

        THEN( "One flag per pattern is set, in pattern order" )
        {
            REQUIRE( matchWith( expression, "error after 12ms" )
                     == MatchedPatterns( "\1\0\1", 3 ) );
            REQUIRE( matchWith( expression, "warn: slow" ) == MatchedPatterns( "\0\1\0", 3 ) );
            REQUIRE( matchWith( expression, "nothing here" ) == MatchedPatterns( "\0\0\0", 3 ) );
        }

        THEN( "A matcher can be reused for the next line" )
        {
            auto matcher = expression.createMatcher();
            const auto match = [ & ]( std::string_view line ) {
                return std::visit( [ & ]( const auto& m ) { return m.match( line ); }, matcher );
            };
            REQUIRE( match( "error" ) == MatchedPatterns( "\1\0\0", 3 ) );
            REQUIRE( match( "quiet" ) == MatchedPatterns( "\0\0\0", 3 ) );
            REQUIRE( match( "5ms" ) == MatchedPatterns( "\0\0\1", 3 ) );
        }
    }

    GIVEN( "A case insensitive and a case sensitive pattern" )
    {
        const HsRegularExpression expression( logsquirl::vector<RegularExpressionPattern>{
            plain( "Fatal", false ), plain( "Fatal", true ) } );

        THEN( "Only the insensitive one matches another spelling" )
        {
            REQUIRE( matchWith( expression, "FATAL crash" ) == MatchedPatterns( "\1\0", 2 ) );
            REQUIRE( matchWith( expression, "Fatal crash" ) == MatchedPatterns( "\1\1", 2 ) );
        }
    }

    GIVEN( "Non-ASCII text" )
    {
        const HsRegularExpression expression( RegularExpressionPattern( "gr\xc3\xbc\xc3\x9f" ) );

        THEN( "It is matched as UTF-8" )
        {
            REQUIRE( matchWith( expression, "Gr\xc3\xbc\xc3\x9f Gott" )
                     == MatchedPatterns( 1, '\0' ) );
            REQUIRE( matchWith( expression, "sagt gr\xc3\xbc\xc3\x9f Gott" )
                     == MatchedPatterns( 1, '\1' ) );
        }
    }

    GIVEN( "An empty line and an empty pattern list" )
    {
        const HsRegularExpression single( RegularExpressionPattern( "x" ) );
        const HsRegularExpression none{ logsquirl::vector<RegularExpressionPattern>{} };

        THEN( "The line matches nothing and the empty list yields no flags" )
        {
            REQUIRE( matchWith( single, "" ) == MatchedPatterns( 1, '\0' ) );
            REQUIRE( none.isValid() );
            REQUIRE( matchWith( none, "anything" ).empty() );
        }
    }
}

SCENARIO( "A pattern one engine only approximates is confirmed by the other",
          "[hsregex][regex][prefilter]" )
{
    GIVEN( "A backreference, which Vectorscan cannot compile" )
    {
        const HsRegularExpression expression( RegularExpressionPattern( "(ab)\\1" ) );

        THEN( "It is valid and matches only a real repetition" )
        {
            REQUIRE( expression.isValid() );
            REQUIRE( matchWith( expression, "xxababxx" ) == MatchedPatterns( 1, '\1' ) );
            REQUIRE( matchWith( expression, "xxabxbxx" ) == MatchedPatterns( 1, '\0' ) );
        }
    }

    GIVEN( "A backreference next to an ordinary pattern" )
    {
        const HsRegularExpression expression( logsquirl::vector<RegularExpressionPattern>{
            RegularExpressionPattern( "(ab)\\1" ), RegularExpressionPattern( "plain" ) } );

        THEN( "Each pattern is reported on its own" )
        {
            REQUIRE( matchWith( expression, "abab" ) == MatchedPatterns( "\1\0", 2 ) );
            REQUIRE( matchWith( expression, "abx plain" ) == MatchedPatterns( "\0\1", 2 ) );
        }
    }
}
