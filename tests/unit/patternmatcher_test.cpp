/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "regularexpression.h"

// Constructed with an explicit engine and no global settings bootstrap.
static constexpr auto TestEngine = RegexpEngine::Vectorscan;

SCENARIO( "Pattern matcher in boolean mode", "[patternmatcher]" )
{
    std::string_view matchLine = "\"This\" is matching pattern";

    WHEN( "Using single pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"matching\"", false, false, true, true ), TestEngine );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match\"", false, false, true, true ),
            TestEngine );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern with ()" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "(\"not_match\" | \"match\") & !(\"pattern\")", false, false,
                                      true, false ),
            TestEngine );
        const auto matcher = expression.createMatcher();
        REQUIRE_FALSE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with escaped quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"\\\"This\\\"\"", false, false, true, false ), TestEngine );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with not matched quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match", false, false, true, false ),
            TestEngine );

        REQUIRE_FALSE( expression.isValid() );
    }
}

// A run of backslashes right before a quote of a sub-pattern is read two for
// one, and the quote after an odd run is part of the sub-pattern; a backslash
// anywhere else is read as written (#405).
SCENARIO( "Backslashes in a boolean pattern", "[patternmatcher]" )
{
    struct Row {
        std::string pattern;
        bool isPlainText;
        std::string lineWith;
        std::string lineWithout;
    };

    const auto row = GENERATE( values<Row>( {
        // Written as today: a regexp class, an escaped backslash, a quote.
        { R"("\d+")", false, "id 42", "id x" },
        { R"("a\\b")", false, R"(x a\b y)", "x a b y" },
        { R"("a\\b")", true, R"(x a\\b y)", R"(x a\b y)" },
        { R"("say \"hi\"")", true, R"(say "hi")", "say hi" },
        // A sub-pattern ending in a backslash.
        { R"("C:\temp\\")", true, R"(C:\temp\ x)", R"(C:\temp x)" },
        { R"("dir\\\\")", false, R"(dir\ x)", "dir x" },
        { R"("a\\" or "b")", true, R"(x a\ y)", "x a y" },
        // A backslash before a quote inside a sub-pattern.
        { R"("a\\\"b")", true, R"(x a\"b y)", R"(x a"b y)" },
    } ) );

    GIVEN( "the pattern " << row.pattern
                          << ( row.isPlainText ? " of fixed strings" : " of regexps" ) )
    {
        RegularExpression expression(
            RegularExpressionPattern( QString::fromStdString( row.pattern ), true, false, true,
                                      row.isPlainText ),
            TestEngine );

        THEN( "it matches the lines with its sub-pattern" )
        {
            INFO( expression.errorString().toStdString() );
            REQUIRE( expression.isValid() );
            const auto matcher = expression.createMatcher();
            REQUIRE( matcher->hasMatch( row.lineWith ) );
            REQUIRE_FALSE( matcher->hasMatch( row.lineWithout ) );
        }
    }
}

// #689: PCRE2 gives up on a Log Line it would have to backtrack through too
// long and reports no match; the matcher tells that apart from a "no match"
// it decided. Vectorscan runs in linear time and never gives up.
SCENARIO( "A pattern matcher tells a Log Line the engine gave up on", "[patternmatcher]" )
{
    const auto backtracking = std::string( 5000, 'a' ) + "!";

    GIVEN( "the QRegularExpression engine and a pattern that backtracks catastrophically" )
    {
        const auto inverse = GENERATE( false, true );
        RegularExpression expression(
            RegularExpressionPattern( "(a|aa)+$", false, inverse, false, false ),
            RegexpEngine::QRegularExpression );
        const auto matcher = expression.createMatcher();

        THEN( "a Log Line it gives up on is undecided, whichever way it is counted" )
        {
            const auto verdict = matcher->decide( backtracking );
            REQUIRE( verdict.isUndecided );
            REQUIRE( verdict.isMatch == inverse );
            REQUIRE( matcher->hasMatch( backtracking ) == verdict.isMatch );
        }

        THEN( "a Log Line it decides is not" )
        {
            REQUIRE_FALSE( matcher->decide( "aaaa" ).isUndecided );
            REQUIRE( matcher->decide( "aaaa" ).isMatch != inverse );
            REQUIRE_FALSE( matcher->decide( "aaab" ).isUndecided );
            REQUIRE( matcher->decide( "aaab" ).isMatch == inverse );
        }
    }

    GIVEN( "a logical combination of such a pattern with another one" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"(a|aa)+$\" | \"!\"", false, false, true, false ),
            RegexpEngine::QRegularExpression );
        const auto matcher = expression.createMatcher();

        THEN( "the Log Line is still undecided, though the other sub-pattern matches it" )
        {
            const auto verdict = matcher->decide( backtracking );
            REQUIRE( verdict.isUndecided );
            REQUIRE( verdict.isMatch );
        }
    }

    GIVEN( "the Vectorscan engine" )
    {
        RegularExpression expression( RegularExpressionPattern( "(a|aa)+$" ),
                                      RegexpEngine::Vectorscan );
        const auto matcher = expression.createMatcher();

        THEN( "the same Log Line is decided" )
        {
            const auto verdict = matcher->decide( backtracking );
            REQUIRE_FALSE( verdict.isUndecided );
            REQUIRE_FALSE( verdict.isMatch );
        }
    }
}
