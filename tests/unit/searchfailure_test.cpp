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

#include "in_memory_block_source.h"
#include "logfiltereddataworker.h"
#include "regularexpression.h"
#include "test_policies.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

namespace {

// A Search whose run fails part way, after it has found a match.
class FailingSearchOperation : public SearchOperation {
public:
    using SearchOperation::SearchOperation;

protected:
    void doRun( SearchData& result ) override
    {
        SearchResultArray matches;
        matches.add( uint64_t{ 3 } );
        result.addAll( 10_length, matches, 0_lnum, 5_lcount );
        throw std::runtime_error( "the Log File could not be read" );
    }
};

struct FinishedSearch {
    SearchId searchId;
    bool interrupted;
    QString failure;
};

} // namespace

SCENARIO( "A Search that fails reports the failure as how it finished", "[search]" )
{
    const auto policies = testSettingsPolicies();
    InMemoryBlockSource blockSource;

    const auto expression = std::make_shared<const RegularExpression>(
        RegularExpressionPattern( "match" ), policies.search.regexpEngine );

    std::atomic<uint64_t> activeSearchId{ 7 };
    FailingSearchOperation operation{ blockSource, SearchId( 7 ),   activeSearchId, expression,
                                      0_lnum,      LineNumber( 5 ), policies.search };

    std::optional<FinishedSearch> finished;
    QObject::connect(
        &operation, &SearchOperation::searchFinished,
        [ &finished ]( SearchId searchId, LineNumber, bool interrupted, const QString& failure ) {
            finished = FinishedSearch{ searchId, interrupted, failure };
        } );

    SearchData searchData;

    WHEN( "it runs" )
    {
        REQUIRE_NOTHROW( operation.run( searchData ) );

        THEN( "it finishes, not interrupted, with a description of the failure" )
        {
            REQUIRE( finished.has_value() );
            REQUIRE( finished->searchId == SearchId( 7 ) );
            REQUIRE_FALSE( finished->interrupted );
            REQUIRE( finished->failure.contains( "the Log File could not be read" ) );
        }

        THEN( "what it found before failing is not kept" )
        {
            const auto results = searchData.takeCurrentResults();
            REQUIRE( results.newMatches.isEmpty() );
            REQUIRE( results.processedLines == 0_lcount );
        }
    }
}
