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
        RegularExpression expression( RegularExpressionPattern(
            "(\"error\") and not (\"debug\")", false, false, true, false ), TestEngine );
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
