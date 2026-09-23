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
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <atomic>
#include <memory>
#include <string_view>
#include <thread>

#include "regularexpression.h"
#include "regularexpressionpattern.h"

// Every test in this file constructs its matchers with an explicit engine
// and no global settings bootstrap -- the regex module reads no settings.
static constexpr auto TestEngine = RegexpEngine::Vectorscan;

// Helper to create a simple regex pattern (not boolean, not plain text)
static RegularExpressionPattern makeRegex( const QString& expr, bool caseSensitive = true,
                                           bool inverse = false )
{
    return RegularExpressionPattern( expr, caseSensitive, inverse, false, false );
}

// Helper to create a plain-text boolean pattern
static RegularExpressionPattern makeBoolean( const QString& expr, bool caseSensitive = false )
{
    return RegularExpressionPattern( expr, caseSensitive, false, true, true );
}

SCENARIO( "RegularExpression basic regex matching", "[regex]" )
{
    GIVEN( "A simple regex pattern" )
    {
        RegularExpression expression( makeRegex( "error" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        WHEN( "Line contains the pattern" )
        {
            THEN( "It matches" )
            {
                REQUIRE( matcher->hasMatch( "2026-01-01 error: something broke" ) );
            }
        }

        WHEN( "Line does not contain the pattern" )
        {
            THEN( "It does not match" )
            {
                REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 info: all good" ) );
            }
        }
    }

    GIVEN( "A case-insensitive regex pattern" )
    {
        RegularExpression expression( makeRegex( "ERROR", false ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches regardless of case" )
        {
            REQUIRE( matcher->hasMatch( "error in line" ) );
            REQUIRE( matcher->hasMatch( "ERROR in line" ) );
            REQUIRE( matcher->hasMatch( "Error in line" ) );
        }
    }

    GIVEN( "A case-sensitive regex pattern" )
    {
        RegularExpression expression( makeRegex( "ERROR", true ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It only matches the exact case" )
        {
            REQUIRE( matcher->hasMatch( "ERROR in line" ) );
            REQUIRE_FALSE( matcher->hasMatch( "error in line" ) );
        }
    }
}

SCENARIO( "RegularExpression with special regex syntax", "[regex]" )
{
    GIVEN( "A pattern with regex quantifiers" )
    {
        RegularExpression expression( makeRegex( "err(or)?s?" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches various forms" )
        {
            REQUIRE( matcher->hasMatch( "errors" ) );
            REQUIRE( matcher->hasMatch( "error" ) );
            REQUIRE( matcher->hasMatch( "err" ) );
        }
    }

    GIVEN( "A pattern with character classes" )
    {
        RegularExpression expression( makeRegex( "[0-9]{3}\\.[0-9]{3}" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches the digit pattern" )
        {
            REQUIRE( matcher->hasMatch( "code 123.456 end" ) );
            REQUIRE_FALSE( matcher->hasMatch( "code 12.34 end" ) );
        }
    }

    GIVEN( "A pattern with anchors" )
    {
        RegularExpression expression( makeRegex( "^ERROR" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It only matches at the start of line" )
        {
            REQUIRE( matcher->hasMatch( "ERROR: something" ) );
            REQUIRE_FALSE( matcher->hasMatch( "some ERROR here" ) );
        }
    }
}

SCENARIO( "RegularExpression inverse matching", "[regex]" )
{
    GIVEN( "An inverse pattern" )
    {
        RegularExpressionPattern pat( "debug", true, true, false, false );
        RegularExpression expression( pat, TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "Lines without the pattern match" )
        {
            REQUIRE( matcher->hasMatch( "error: something broke" ) );
        }

        THEN( "Lines with the pattern do not match" )
        {
            REQUIRE_FALSE( matcher->hasMatch( "debug: tracing" ) );
        }
    }
}

SCENARIO( "RegularExpression invalid patterns", "[regex]" )
{
    GIVEN( "An invalid regex" )
    {
        RegularExpression expression( makeRegex( "[invalid" ), TestEngine );

        THEN( "It reports as invalid" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE_FALSE( expression.errorString().isEmpty() );
        }
    }

    GIVEN( "An empty pattern" )
    {
        RegularExpression expression( makeRegex( "" ), TestEngine );

        // Empty patterns may be valid regex (matches everything) depending on engine
        // Just verify no crash
        THEN( "It does not crash" )
        {
            auto matcher = expression.createMatcher();
            // Should not crash regardless of validity
        }
    }
}

SCENARIO( "RegularExpression boolean operators", "[regex][boolean]" )
{
    std::string_view line = "This is an error in the system log";

    GIVEN( "Boolean AND operation" )
    {
        RegularExpression expression( makeBoolean( "\"error\" & \"log\"" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches when both terms are present" )
        {
            REQUIRE( matcher->hasMatch( line ) );
        }

        THEN( "It does not match when one term is missing" )
        {
            REQUIRE_FALSE( matcher->hasMatch( "This is an error message" ) );
        }
    }

    GIVEN( "Boolean OR operation" )
    {
        RegularExpression expression( makeBoolean( "\"warning\" | \"error\"" ), TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches when any term is present" )
        {
            REQUIRE( matcher->hasMatch( "This is an error" ) );
            REQUIRE( matcher->hasMatch( "This is a warning" ) );
        }

        THEN( "It does not match when no term is present" )
        {
            REQUIRE_FALSE( matcher->hasMatch( "This is info" ) );
        }
    }

    GIVEN( "Boolean NOT operation" )
    {
        // Uses 'and' and 'not' keywords, which are more portable with exprtk
        RegularExpression expression( RegularExpressionPattern( "(\"error\") and not (\"debug\")",
                                                                false, false, true, false ),
                                      TestEngine );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "It matches when positive term present and negated term absent" )
        {
            REQUIRE( matcher->hasMatch( "error in production" ) );
        }

        THEN( "It does not match when negated term is also present" )
        {
            REQUIRE_FALSE( matcher->hasMatch( "error in debug mode" ) );
        }
    }

    GIVEN( "Invalid boolean pattern — unmatched quotes" )
    {
        RegularExpression expression( makeBoolean( "\"error\" | \"warn" ), TestEngine );

        THEN( "It reports as invalid" )
        {
            REQUIRE_FALSE( expression.isValid() );
        }
    }

    GIVEN( "Invalid boolean pattern — no quotes at all" )
    {
        RegularExpression expression( makeBoolean( "error | warn" ), TestEngine );

        THEN( "It reports as invalid" )
        {
            REQUIRE_FALSE( expression.isValid() );
        }
    }
}

SCENARIO( "The matching engine is the caller's choice", "[regex][engine]" )
{
    // No Configuration::get(), no persistable bootstrap: both engines are
    // constructible side by side in the same binary, which is exactly what
    // moving the choice out of the regex module bought.
    GIVEN( "the same pattern compiled for both engines" )
    {
        RegularExpression vectorscan( makeRegex( "err(or)?" ), RegexpEngine::Vectorscan );
        RegularExpression qt( makeRegex( "err(or)?" ), RegexpEngine::QRegularExpression );

        REQUIRE( vectorscan.isValid() );
        REQUIRE( qt.isValid() );

        WHEN( "both matchers run over the same lines" )
        {
            auto vectorscanMatcher = vectorscan.createMatcher();
            auto qtMatcher = qt.createMatcher();

            THEN( "they agree on every line" )
            {
                for ( const auto* line : { "2026-01-01 error: broke", "2026-01-01 err: broke",
                                           "2026-01-01 info: all good", "" } ) {
                    REQUIRE( vectorscanMatcher->hasMatch( line ) == qtMatcher->hasMatch( line ) );
                }
            }
        }
    }

    GIVEN( "an inverse pattern compiled for both engines" )
    {
        auto pat = makeRegex( "error" );
        pat.isExclude = true;

        RegularExpression vectorscan( pat, RegexpEngine::Vectorscan );
        RegularExpression qt( pat, RegexpEngine::QRegularExpression );

        WHEN( "both matchers run over the same lines" )
        {
            auto vectorscanMatcher = vectorscan.createMatcher();
            auto qtMatcher = qt.createMatcher();

            THEN( "they agree on every line" )
            {
                REQUIRE( vectorscanMatcher->hasMatch( "all good" ) );
                REQUIRE( qtMatcher->hasMatch( "all good" ) );
                REQUIRE_FALSE( vectorscanMatcher->hasMatch( "an error here" ) );
                REQUIRE_FALSE( qtMatcher->hasMatch( "an error here" ) );
            }
        }
    }
}

SCENARIO( "Patterns Vectorscan rejects still match like the Qt engine", "[regex][prefilter]" )
{
    // Vectorscan cannot compile lookaround; such a Search runs
    // Vectorscan as a prefilter and confirms each candidate Log Line with
    // QRegularExpression (#279).
    const auto lines = { "2026-01-01 INFO started",
                         "2026-01-01 DEBUG cache warm",
                         "2026-01-01 ERROR größe überschritten",
                         "ERROR ERROR repeated",
                         "",
                         "debug in lower case" };

    GIVEN( "a lookahead pattern compiled for both engines" )
    {
        RegularExpression vectorscan( makeRegex( "^(?!.*DEBUG)" ), RegexpEngine::Vectorscan );
        RegularExpression qt( makeRegex( "^(?!.*DEBUG)" ), RegexpEngine::QRegularExpression );
        REQUIRE( vectorscan.isValid() );
        REQUIRE( qt.isValid() );

        auto vectorscanMatcher = vectorscan.createMatcher();
        auto qtMatcher = qt.createMatcher();

        THEN( "every Log Line without DEBUG matches, on both engines" )
        {
            for ( const auto* line : lines ) {
                const bool expected
                    = std::string_view{ line }.find( "DEBUG" ) == std::string_view::npos;
                REQUIRE( vectorscanMatcher->hasMatch( line ) == expected );
                REQUIRE( qtMatcher->hasMatch( line ) == expected );
            }
        }
    }

    GIVEN( "a lookbehind pattern" )
    {
        RegularExpression expression( makeRegex( "(?<=ERROR) ERROR" ), TestEngine );
        REQUIRE( expression.isValid() );
        auto matcher = expression.createMatcher();

        THEN( "only ERROR after ERROR matches, again and again" )
        {
            for ( int round = 0; round < 3; ++round ) {
                REQUIRE( matcher->hasMatch( "ERROR ERROR repeated" ) );
                REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 ERROR once" ) );
            }
        }
    }

    GIVEN( "a boolean expression of several sub-patterns, one with a lookahead" )
    {
        const auto pattern = RegularExpressionPattern(
            "(\"^(?!.*DEBUG)\") and ((\"größe\") or (\"started\")) and not (\"cache\")", true,
            false, true, false );
        RegularExpression vectorscan( pattern, RegexpEngine::Vectorscan );
        RegularExpression qt( pattern, RegexpEngine::QRegularExpression );
        REQUIRE( vectorscan.isValid() );
        REQUIRE( qt.isValid() );

        auto vectorscanMatcher = vectorscan.createMatcher();
        auto qtMatcher = qt.createMatcher();

        THEN( "both engines select the same Log Lines" )
        {
            for ( const auto* line : { "2026-01-01 INFO started", "2026-01-01 DEBUG started",
                                       "2026-01-01 ERROR größe überschritten",
                                       "2026-01-01 INFO cache started", "nothing here" } ) {
                const std::string_view view{ line };
                const bool expected = view.find( "DEBUG" ) == std::string_view::npos
                                      && view.find( "cache" ) == std::string_view::npos
                                      && ( view.find( "größe" ) != std::string_view::npos
                                           || view.find( "started" ) != std::string_view::npos );
                REQUIRE( vectorscanMatcher->hasMatch( line ) == expected );
                REQUIRE( qtMatcher->hasMatch( line ) == expected );
            }
        }
    }

    GIVEN( "matchers of one lookahead Search running on several threads at once" )
    {
        for ( auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
            RegularExpression expression( makeRegex( "^(?!.*DEBUG).*ERROR" ), engine );
            REQUIRE( expression.isValid() );

            constexpr int Threads = 4;
            logsquirl::vector<std::unique_ptr<PatternMatcher>> matchers;
            for ( int i = 0; i < Threads; ++i ) {
                matchers.push_back( expression.createMatcher() );
            }

            std::atomic<int> wrongResults = 0;
            logsquirl::vector<std::thread> threads;
            for ( int i = 0; i < Threads; ++i ) {
                threads.emplace_back(
                    [ &matcher = *matchers[ static_cast<size_t>( i ) ], &wrongResults ] {
                        for ( int n = 0; n < 2000; ++n ) {
                            if ( !matcher.hasMatch( "2026-01-01 ERROR failed" )
                                 || matcher.hasMatch( "2026-01-01 DEBUG ERROR ignored" ) ) {
                                ++wrongResults;
                            }
                        }
                    } );
            }
            for ( auto& thread : threads ) {
                thread.join();
            }

            THEN( "every thread gets the right result" )
            {
                REQUIRE( wrongResults == 0 );
            }
        }
    }
}

SCENARIO( "Highlighter patterns Vectorscan rejects", "[regex][prefilter]" )
{
    GIVEN( "several patterns, one with a lookahead" )
    {
        MultiRegularExpression expression( { makeRegex( "^(?!.*DEBUG)" ), makeRegex( "ERROR" ),
                                             makeRegex( "(?<!DEBUG) ERROR" ) } );
        auto matcher = expression.createMatcher();

        THEN( "each pattern reports its own match" )
        {
            const auto result = matcher->match( "DEBUG ERROR" );
            REQUIRE( result.size() == 3 );
            REQUIRE_FALSE( result[ 0 ].second );
            REQUIRE( result[ 1 ].second );
            REQUIRE_FALSE( result[ 2 ].second );

            const auto second = matcher->match( "INFO ERROR" );
            REQUIRE( second[ 0 ].second );
            REQUIRE( second[ 1 ].second );
            REQUIRE( second[ 2 ].second );

            const auto other = matcher->match( "INFO fine" );
            REQUIRE( other[ 0 ].second );
            REQUIRE_FALSE( other[ 1 ].second );
            REQUIRE_FALSE( other[ 2 ].second );
        }
    }
}

// Vectorscan takes no backreference, in prefilter mode no more than in its
// normal one, so with that engine chosen the expression falls back to
// QRegularExpression whole. Both engines are still exercised here: the
// acceptance is that the Search matches whichever engine the user picked, and
// the log of a run shows Vectorscan refusing the pattern twice before the
// fallback takes it (#336).
SCENARIO( "A Search pattern with a backreference matches on both engines", "[regex][backref]" )
{
    GIVEN( "a numbered backreference on each engine" )
    {
        for ( auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
            RegularExpression expression( makeRegex( "(ERROR) \\1" ), engine );
            REQUIRE( expression.isValid() );

            auto matcher = expression.createMatcher();

            THEN( "a Log Line that repeats the captured text matches" )
            {
                REQUIRE( matcher->hasMatch( "2026-01-01 ERROR ERROR twice" ) );
            }

            THEN( "a Log Line that does not repeat it does not match" )
            {
                REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 ERROR WARNING once" ) );
                REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 all quiet" ) );
            }
        }
    }

    GIVEN( "a named backreference on each engine" )
    {
        for ( auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
            RegularExpression expression( makeRegex( "(?<level>ERROR|WARN) \\k<level>" ), engine );
            REQUIRE( expression.isValid() );

            auto matcher = expression.createMatcher();

            THEN( "the Log Line matches only where the same name comes back" )
            {
                REQUIRE( matcher->hasMatch( "WARN WARN here" ) );
                REQUIRE_FALSE( matcher->hasMatch( "WARN ERROR here" ) );
            }
        }
    }

    GIVEN( "a case-insensitive backreference on each engine" )
    {
        for ( auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
            RegularExpression expression( makeRegex( "(error) \\1", false ), engine );
            REQUIRE( expression.isValid() );

            auto matcher = expression.createMatcher();

            THEN( "case is ignored in the group and in what refers back to it" )
            {
                REQUIRE( matcher->hasMatch( "ERROR Error again" ) );
                REQUIRE_FALSE( matcher->hasMatch( "ERROR warning" ) );
            }
        }
    }

    GIVEN( "an escaped backslash before a digit, which is no backreference" )
    {
        for ( auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
            RegularExpression expression( makeRegex( "path\\\\1" ), engine );
            REQUIRE( expression.isValid() );

            auto matcher = expression.createMatcher();

            THEN( "it matches the literal text it spells out" )
            {
                REQUIRE( matcher->hasMatch( "path\\1 taken" ) );
                REQUIRE_FALSE( matcher->hasMatch( "path1 taken" ) );
            }
        }
    }
}

SCENARIO( "A Highlighter pattern with a backreference matches", "[regex][backref][prefilter]" )
{
    GIVEN( "several patterns, one of them a backreference" )
    {
        MultiRegularExpression expression( { makeRegex( "(ERROR) \\1" ), makeRegex( "ERROR" ),
                                             makeRegex( "(?<word>\\w+) \\k<word>" ) } );
        REQUIRE( expression.isValid() );

        auto matcher = expression.createMatcher();

        THEN( "each pattern reports its own match" )
        {
            const auto repeated = matcher->match( "ERROR ERROR" );
            REQUIRE( repeated.size() == 3 );
            REQUIRE( repeated[ 0 ].second );
            REQUIRE( repeated[ 1 ].second );
            REQUIRE( repeated[ 2 ].second );

            const auto single = matcher->match( "ERROR once" );
            REQUIRE_FALSE( single[ 0 ].second );
            REQUIRE( single[ 1 ].second );
            REQUIRE_FALSE( single[ 2 ].second );
        }
    }
}

SCENARIO( "A MultiRegularExpression says whether its patterns compiled", "[regex][highlight]" )
{
    GIVEN( "patterns that all compile" )
    {
        MultiRegularExpression expression( { makeRegex( "ERROR" ), makeRegex( "WARN" ) } );

        THEN( "it is valid" )
        {
            REQUIRE( expression.isValid() );
        }
    }

    GIVEN( "a pattern no engine can compile" )
    {
        MultiRegularExpression expression( { makeRegex( "ERROR" ), makeRegex( "(unclosed" ) } );

        THEN( "it is invalid and says why" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE_FALSE( expression.errorString().isEmpty() );
        }
    }

    GIVEN( "a backreference to a group that does not exist" )
    {
        MultiRegularExpression expression( { makeRegex( "(?<level>ERROR) \\k<other>" ) } );

        THEN( "it is invalid instead of quietly matching nothing" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE_FALSE( expression.errorString().isEmpty() );
        }
    }
}

// Filter frequency charts each sub-pattern of a logical combination on its
// own; it takes them from the parser the Search uses, so the chart counts the
// sub-patterns the Search matches (#410).
SCENARIO( "The sub-patterns of a logical combination are read as the Search reads them",
          "[regex][boolean]" )
{
    GIVEN( "sub-patterns with an escaped quote and with the word or inside" )
    {
        THEN( "each is read unescaped and whole" )
        {
            REQUIRE( logicalSubPatterns( R"("say \"hi\"" or "a or b")" )
                     == QStringList{ R"(say "hi")", "a or b" } );
        }
    }

    GIVEN( "a sub-pattern ending in a backslash and a regexp class" )
    {
        THEN( "the backslashes before the closing quote are read two for one, the others as "
              "written" )
        {
            REQUIRE( logicalSubPatterns( R"("C:\temp\\" | "\d+")" )
                     == QStringList{ R"(C:\temp\)", R"(\d+)" } );
        }
    }

    GIVEN( "sub-patterns joined with and, and not()" )
    {
        THEN( "every sub-pattern is read, in the order written, the negated one included" )
        {
            REQUIRE( logicalSubPatterns( R"(("error" and "disk") and not("debug"))" )
                     == QStringList{ "error", "disk", "debug" } );
        }
    }

    GIVEN( "patterns that are no valid logical combination" )
    {
        THEN( "there are no sub-patterns" )
        {
            REQUIRE( logicalSubPatterns( R"("error" | "warn)" ).isEmpty() );
            REQUIRE( logicalSubPatterns( "error | warn" ).isEmpty() );
            REQUIRE( logicalSubPatterns( R"("error" | warn)" ).isEmpty() );
        }
    }
}

// The Search Line writes a sub-pattern the way the logical expression parser
// reads it back (#398, #405).
SCENARIO( "A quoted sub-pattern is read back as it was", "[regex]" )
{
    const auto word
        = GENERATE( as<QString>{}, "error", R"(say "hi")", R"(")", R"("")", R"(C:\temp\)", R"(\)",
                    R"(\\\)", R"(a\"b)", R"(a\\"b)", R"(\\\"x\\)", R"(\d+)", "a or b" );

    WHEN( "the sub-pattern " << word.toStdString() << " is quoted" )
    {
        const auto quoted = quoteSubPattern( word );

        THEN( "a logical combination of it reads it back, alone and among others" )
        {
            INFO( "quoted: " << quoted.toStdString() );
            REQUIRE( logicalSubPatterns( quoted ) == QStringList{ word } );
            REQUIRE( logicalSubPatterns( quoted + " or not(" + quoteSubPattern( word ) + ")" )
                     == QStringList{ word, word } );
        }
    }

    GIVEN( "quotes and backslashes" )
    {
        THEN( "a quote is written \\\", a run of backslashes before a quote or at the end "
              "doubled, any other as it is" )
        {
            REQUIRE( quoteSubPattern( R"(say "hi")" ) == R"("say \"hi\"")" );
            REQUIRE( quoteSubPattern( R"(C:\temp\)" ) == R"("C:\temp\\")" );
            REQUIRE( quoteSubPattern( R"(a\"b)" ) == R"("a\\\"b")" );
            REQUIRE( quoteSubPattern( R"(\d+)" ) == R"("\d+")" );
        }
    }
}

// Filter frequency charts each alternative of a regexp Search on its own; only
// the top-level alternatives are split off, so that each stays a valid regexp
// (#411).
SCENARIO( "The alternatives of a regexp are split only at the top level", "[regex]" )
{
    GIVEN( "alternatives at the top level" )
    {
        THEN( "each is an alternative of its own, empty ones left out" )
        {
            REQUIRE( regexpAlternatives( "x|y" ) == QStringList{ "x", "y" } );
            REQUIRE( regexpAlternatives( "x||y|" ) == QStringList{ "x", "y" } );
            REQUIRE( regexpAlternatives( "x.y" ) == QStringList{ "x.y" } );
        }
    }

    GIVEN( "a | inside a group" )
    {
        THEN( "the group is not split" )
        {
            REQUIRE( regexpAlternatives( "(a|b)c" ) == QStringList{ "(a|b)c" } );
            REQUIRE( regexpAlternatives( "(?:a|(b|c))d|e" ) == QStringList{ "(?:a|(b|c))d", "e" } );
        }
    }

    GIVEN( "a | inside a character class" )
    {
        THEN( "the class is not split" )
        {
            REQUIRE( regexpAlternatives( "[|]x|y" ) == QStringList{ "[|]x", "y" } );
            REQUIRE( regexpAlternatives( "[]|(]x|y" ) == QStringList{ "[]|(]x", "y" } );
            REQUIRE( regexpAlternatives( "[^]|]x|y" ) == QStringList{ "[^]|]x", "y" } );
            REQUIRE( regexpAlternatives( R"([\]|]x|y)" ) == QStringList{ R"([\]|]x)", "y" } );
            REQUIRE( regexpAlternatives( "[[:alpha:]|]x|y" )
                     == QStringList{ "[[:alpha:]|]x", "y" } );
        }
    }

    GIVEN( "an escaped |, parenthesis or bracket, or a quoted part" )
    {
        THEN( "it is read as the character it stands for" )
        {
            REQUIRE( regexpAlternatives( R"(a\|b)" ) == QStringList{ R"(a\|b)" } );
            REQUIRE( regexpAlternatives( R"(\(a|b\))" ) == QStringList{ R"(\(a)", R"(b\))" } );
            REQUIRE( regexpAlternatives( R"(\[a|b)" ) == QStringList{ R"(\[a)", "b" } );
            REQUIRE( regexpAlternatives( R"(a\\|b)" ) == QStringList{ R"(a\\)", "b" } );
            REQUIRE( regexpAlternatives( R"(\Q(a|b\E|c)" ) == QStringList{ R"(\Q(a|b\E)", "c" } );
            REQUIRE( regexpAlternatives( R"(\Q(a|b)" ) == QStringList{ R"(\Q(a|b)" } );
        }
    }
}
