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

// The Search Line and Field Terms (#798): a cell's value joins the pattern
// as a Field Term in a logical combination, and a Predefined Filter with
// Field Terms is combined as the logical combination it is.

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QString>

#include "predefinedfilter.h"
#include "regularexpression.h"
#include "searchline.h"

namespace {

QuickFindPolicy startingState( bool regexp, bool logical )
{
    QuickFindPolicy policy;
    policy.mainRegexpType
        = regexp ? SearchRegexpType::ExtendedRegexp : SearchRegexpType::FixedString;
    policy.searchLogicalCombiningDefault = logical;
    policy.autoRunSearchOnPatternChange = false;
    return policy;
}

} // namespace

SCENARIO( "A cell's value joins the Search Line as a Field Term", "[searchline][fieldterm]" )
{
    GIVEN( "a plain Search for alpha, not a logical combination" )
    {
        SearchLine line{ startingState( false, false ) };
        line.setPattern( "alpha" );

        WHEN( "the value DNS of the column Protocol is searched" )
        {
            line.addFieldTerm( "Protocol", "DNS" );

            THEN( "the pattern is a logical combination of the Search and the Field Term" )
            {
                REQUIRE( line.flags().booleanCombination );
                REQUIRE( line.pattern() == R"("alpha" and Protocol:"DNS")" );
                REQUIRE( logicalFieldTerms( line.pattern() )
                         == QStringList{ QString{}, QStringLiteral( "Protocol" ) } );
            }
        }
    }

    GIVEN( "an empty Search Line read as a regexp" )
    {
        SearchLine line{ startingState( true, false ) };

        WHEN( "a value with regexp characters is searched in a column" )
        {
            line.addFieldTerm( "Source", "10.0.0.5" );

            THEN( "the Field Term is all the pattern says, the value escaped" )
            {
                REQUIRE( line.flags().booleanCombination );
                REQUIRE( line.pattern() == R"(Source:"10\.0\.0\.5")" );
                REQUIRE( logicalSubPatterns( line.pattern() )
                         == QStringList{ QStringLiteral( R"(10\.0\.0\.5)" ) } );
            }
        }
    }

    GIVEN( "a logical combination with a Field Term already" )
    {
        SearchLine line{ startingState( false, true ) };
        line.setPattern( R"(Protocol:"DNS")" );

        WHEN( "another value is searched in a column, with a quote in it" )
        {
            line.addFieldTerm( "body", R"(say "hi")" );

            THEN( "it is appended with and, the quote escaped as a sub-pattern writes it" )
            {
                REQUIRE( line.pattern() == R"(Protocol:"DNS" and body:"say \"hi\"")" );
                REQUIRE(
                    logicalSubPatterns( line.pattern() )
                    == QStringList{ QStringLiteral( "DNS" ), QStringLiteral( R"(say "hi")" ) } );
            }
        }
    }
}

SCENARIO( "A Predefined Filter with Field Terms is combined as a logical combination",
          "[searchline][fieldterm]" )
{
    const PredefinedFilter fieldFilter{ "dns", R"(Protocol:"DNS" and not("retry"))", true };
    const PredefinedFilter plainFilter{ "beta", "beta", false };

    GIVEN( "a plain Search Line" )
    {
        SearchLine line{ startingState( false, false ) };

        WHEN( "the Field Term filter alone is used" )
        {
            line.useFilters( { fieldFilter } );

            THEN( "the logical combination is on and the filter is the pattern, in parentheses" )
            {
                REQUIRE( line.flags().booleanCombination );
                REQUIRE( line.pattern() == R"((Protocol:"DNS" and not("retry")))" );
                REQUIRE( logicalFieldTerms( line.pattern() )
                         == QStringList{ QStringLiteral( "Protocol" ), QString{} } );
            }
        }

        WHEN( "the Field Term filter and a plain one are used" )
        {
            line.useFilters( { plainFilter, fieldFilter } );

            THEN( "they are alternatives of a logical combination, the plain one quoted" )
            {
                REQUIRE( line.flags().booleanCombination );
                REQUIRE( line.pattern() == R"("beta" or (Protocol:"DNS" and not("retry")))" );
            }
        }

        WHEN( "only plain filters are used" )
        {
            line.useFilters( { plainFilter } );

            THEN( "the pattern stays a fixed string, as before" )
            {
                REQUIRE_FALSE( line.flags().booleanCombination );
                REQUIRE( line.pattern() == "beta" );
            }
        }
    }
}
