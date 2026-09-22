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

#include <QJsonArray>
#include <QJsonObject>

#include "settingspolicies.h"
#include "viewstatecodec.h"

namespace {

QuickFindPolicy policyReadingSearchesAs( SearchRegexpType type )
{
    QuickFindPolicy policy;
    policy.mainRegexpType = type;
    return policy;
}

// A view state with every field set away from its default, so a field the
// codec dropped or mixed up with another shows in the comparison.
ViewState everyFieldSet()
{
    ViewState state;
    state.sizes = { 612, 187 };
    state.ignoreCase = true;
    state.autoRefresh = true;
    state.followFile = true;
    state.useRegexp = true;
    state.inverseRegexp = true;
    state.useBooleanCombination = true;
    state.marks = { 3, 17, 2048 };
    state.chartSeries = QJsonArray{
        QJsonObject{ { "name", "latency" }, { "pattern", "took (\\d+) ms" } },
        QJsonObject{ { "name", "errors" }, { "pattern", "ERROR" } },
    };
    state.chartVisible = true;
    return state;
}

// A view state as the release before the codec had its own module wrote it
// (#390): the Crawler Widget's serializer, byte for byte.
constexpr auto SavedByTheCurrentRelease
    = R"({"AR":true,"BC":false,"CS":"[{\"name\":\"latency\",\"pattern\":\"took (\\\\d+) ms\"}]",)"
      R"("CV":true,"FF":false,"IC":true,"IR":true,"M":[3,17,2048],"RE":false,"S":[400,125]})";

} // namespace

SCENARIO( "A tab's view state survives being saved and read back", "[viewstatecodec]" )
{
    GIVEN( "a view state with every field set" )
    {
        const auto state = everyFieldSet();

        WHEN( "it is encoded and decoded again" )
        {
            // A Policy that says the opposite of the saved regexp flag: the
            // saved value must win.
            const auto decoded
                = decodeViewState( encodeViewState( state ),
                                   policyReadingSearchesAs( SearchRegexpType::FixedString ) );

            THEN( "every field comes back as it was" )
            {
                REQUIRE( decoded.sizes == state.sizes );
                REQUIRE( decoded.ignoreCase == state.ignoreCase );
                REQUIRE( decoded.autoRefresh == state.autoRefresh );
                REQUIRE( decoded.followFile == state.followFile );
                REQUIRE( decoded.useRegexp == state.useRegexp );
                REQUIRE( decoded.inverseRegexp == state.inverseRegexp );
                REQUIRE( decoded.useBooleanCombination == state.useBooleanCombination );
                REQUIRE( decoded.marks == state.marks );
                REQUIRE( decoded.chartSeries == state.chartSeries );
                REQUIRE( decoded.chartVisible == state.chartVisible );
                REQUIRE( decoded == state );
            }
        }
    }

    GIVEN( "a view state with every flag cleared and nothing marked or charted" )
    {
        ViewState state;
        state.sizes = { 1, 0 };

        WHEN( "it is encoded and decoded again" )
        {
            const auto decoded
                = decodeViewState( encodeViewState( state ),
                                   policyReadingSearchesAs( SearchRegexpType::ExtendedRegexp ) );

            THEN( "it comes back as it was, the saved regexp flag winning over the Policy" )
            {
                REQUIRE( decoded == state );
            }
        }
    }
}

SCENARIO( "A view state saved by the current release reads back unchanged", "[viewstatecodec]" )
{
    GIVEN( "the text a Session holds for a tab" )
    {
        const QString saved = SavedByTheCurrentRelease;

        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState(
                saved, policyReadingSearchesAs( SearchRegexpType::ExtendedRegexp ) );

            THEN( "every field is what was saved" )
            {
                REQUIRE( state.sizes == QList<int>{ 400, 125 } );
                REQUIRE( state.ignoreCase );
                REQUIRE( state.autoRefresh );
                REQUIRE_FALSE( state.followFile );
                REQUIRE_FALSE( state.useRegexp );
                REQUIRE( state.inverseRegexp );
                REQUIRE_FALSE( state.useBooleanCombination );
                REQUIRE( state.marks == QList<LineNumber::UnderlyingType>{ 3, 17, 2048 } );
                REQUIRE( state.chartSeries
                         == QJsonArray{ QJsonObject{ { "name", "latency" },
                                                     { "pattern", "took (\\d+) ms" } } } );
                REQUIRE( state.chartVisible );
            }

            AND_THEN( "encoding it again gives the same text" )
            {
                REQUIRE( encodeViewState( state ) == saved );
            }
        }
    }

    GIVEN( "a view state saved before it recorded the regexp, inverse and combination flags" )
    {
        const QString saved = R"({"AR":false,"FF":true,"IC":false,"S":[300,200]})";

        WHEN( "it is decoded" )
        {
            THEN( "the Search line reads its pattern as the QuickFind Policy says" )
            {
                REQUIRE( decodeViewState(
                             saved, policyReadingSearchesAs( SearchRegexpType::ExtendedRegexp ) )
                             .useRegexp );
                REQUIRE_FALSE( decodeViewState(
                                   saved, policyReadingSearchesAs( SearchRegexpType::FixedString ) )
                                   .useRegexp );
            }

            AND_THEN( "the other fields are what was saved, the missing ones cleared" )
            {
                const auto state = decodeViewState(
                    saved, policyReadingSearchesAs( SearchRegexpType::ExtendedRegexp ) );
                REQUIRE( state.sizes == QList<int>{ 300, 200 } );
                REQUIRE_FALSE( state.ignoreCase );
                REQUIRE_FALSE( state.autoRefresh );
                REQUIRE( state.followFile );
                REQUIRE_FALSE( state.inverseRegexp );
                REQUIRE_FALSE( state.useBooleanCombination );
                REQUIRE( state.marks.isEmpty() );
                REQUIRE( state.chartSeries.isEmpty() );
                REQUIRE_FALSE( state.chartVisible );
            }
        }
    }
}

SCENARIO( "A view state in the legacy format reads back", "[viewstatecodec]" )
{
    GIVEN( "the colon-separated text glogg saved" )
    {
        const QString saved = "S512:88:IC1:AR0:FF1";

        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState(
                saved, policyReadingSearchesAs( SearchRegexpType::FixedString ) );

            THEN( "the sizes and flags it holds are read" )
            {
                REQUIRE( state.sizes == QList<int>{ 512, 88 } );
                REQUIRE( state.ignoreCase );
                REQUIRE_FALSE( state.autoRefresh );
                REQUIRE( state.followFile );
            }

            AND_THEN( "what it does not hold takes the Policy's word or is cleared" )
            {
                REQUIRE_FALSE( state.useRegexp );
                REQUIRE_FALSE( state.inverseRegexp );
                REQUIRE_FALSE( state.useBooleanCombination );
                REQUIRE( state.marks.isEmpty() );
                REQUIRE( state.chartSeries.isEmpty() );
                REQUIRE_FALSE( state.chartVisible );
            }
        }
    }
}

SCENARIO( "A malformed or empty view state reads as defaults", "[viewstatecodec]" )
{
    const auto policy = policyReadingSearchesAs( SearchRegexpType::ExtendedRegexp );

    GIVEN( "an empty entry" )
    {
        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState( QString(), policy );

            THEN( "the views get the default split and every flag cleared" )
            {
                REQUIRE( state.sizes == QList<int>{ 400, 100 } );
                REQUIRE_FALSE( state.ignoreCase );
                REQUIRE_FALSE( state.autoRefresh );
                REQUIRE_FALSE( state.followFile );
                REQUIRE( state.useRegexp );
                REQUIRE_FALSE( state.inverseRegexp );
                REQUIRE_FALSE( state.useBooleanCombination );
                REQUIRE( state.marks.isEmpty() );
                REQUIRE( state.chartSeries.isEmpty() );
                REQUIRE_FALSE( state.chartVisible );
            }
        }
    }

    GIVEN( "legacy text that holds none of the fields" )
    {
        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState( "garbage", policy );

            THEN( "it reads as an empty entry does" )
            {
                REQUIRE( state == decodeViewState( QString(), policy ) );
            }
        }
    }

    GIVEN( "text that starts as JSON but is not" )
    {
        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState( R"({"S":[400,)", policy );

            THEN( "no field is read: no sizes, flags cleared, the regexp flag from the Policy" )
            {
                REQUIRE( state.sizes.isEmpty() );
                REQUIRE_FALSE( state.ignoreCase );
                REQUIRE_FALSE( state.autoRefresh );
                REQUIRE_FALSE( state.followFile );
                REQUIRE( state.useRegexp );
                REQUIRE_FALSE( state.inverseRegexp );
                REQUIRE_FALSE( state.useBooleanCombination );
                REQUIRE( state.marks.isEmpty() );
                REQUIRE( state.chartSeries.isEmpty() );
                REQUIRE_FALSE( state.chartVisible );
            }
        }
    }

    GIVEN( "a saved chart that is not a JSON array" )
    {
        WHEN( "it is decoded" )
        {
            const auto state = decodeViewState( R"({"CS":"not json","S":[1,2]})", policy );

            THEN( "there is no chart, and the rest is read" )
            {
                REQUIRE( state.chartSeries.isEmpty() );
                REQUIRE( state.sizes == QList<int>{ 1, 2 } );
            }
        }
    }
}
