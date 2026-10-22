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

#include <chrono>
#include <memory>

#include <QStringList>
#include <QTest>

#include "in_memory_block_source.h"
#include "logfiltereddataworker.h"
#include "regularexpression.h"
#include "test_policies.h"

// A Search whose block read outlasts the shutdown timeout of its Background
// Run is left running when its owners go away: it must then read and write
// only what it holds itself (#832).

namespace {

QStringList numberedLines( int count )
{
    QStringList lines;
    for ( auto number = 0; number < count; ++number ) {
        lines.append( QString( "line %1" ).arg( number ) );
    }
    return lines;
}

} // namespace

SCENARIO( "A Search left running past the shutdown timeout outlives its owners on its own",
          "[search][backgroundrun]" )
{
    using namespace std::chrono_literals;

    const auto policies = testSettingsPolicies();
    const auto expression = std::make_shared<const RegularExpression>(
        RegularExpressionPattern( "line" ), policies.search.regexpEngine );

    GIVEN( "a Search held in a block read" )
    {
        auto blockSource = std::make_shared<InMemoryBlockSource>( numberedLines( 10 ) );
        const std::weak_ptr<InMemoryBlockSource> heldByTheRun = blockSource;
        blockSource->holdReading();

        auto worker
            = std::make_unique<LogFilteredDataWorker>( blockSource, policies.search, 100ms );
        worker->search( expression, 0_lnum, LineNumber( 10 ) );
        blockSource->waitUntilReadingHeld();

        WHEN( "its owners are destroyed, and the read is released only after that" )
        {
            blockSource.reset();
            const auto shutdownStart = std::chrono::steady_clock::now();
            worker.reset();
            const auto shutdownTime = std::chrono::steady_clock::now() - shutdownStart;

            THEN( "shutdown gave up after its timeout, the run kept its block source, and "
                  "it ends once the read is released" )
            {
                REQUIRE( shutdownTime >= 100ms );
                REQUIRE( shutdownTime < 5s );

                auto stillRead = heldByTheRun.lock();
                REQUIRE( stillRead );
                REQUIRE( stillRead->attachedReaders() == 1 );
                stillRead->releaseReading();
                stillRead.reset();

                REQUIRE( QTest::qWaitFor( [ &heldByTheRun ] { return heldByTheRun.expired(); },
                                          10000 ) );
            }
        }
    }
}
