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

#include "digestcoverage.h"

#include <QDateTime>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// The one rule that says how much of a Log File is read to tell whether its
// Index still fits it (#277, #337), on its own: no Log File, no modification
// time on disk, just what the rule is asked with.

namespace {

IndexingPolicy fastModificationDetection( bool enabled )
{
    IndexingPolicy policy;
    policy.fastModificationDetection = enabled;
    return policy;
}

const QDateTime IndexedAt = QDateTime::fromSecsSinceEpoch( 1'700'000'000 );
const QDateTime ModifiedLater = IndexedAt.addSecs( 1 );

} // namespace

SCENARIO( "Following a Log File checks a cached Index by its header and tail", "[digestcoverage]" )
{
    GIVEN( "a cached Index asked for when a Log File is opened or followed" )
    {
        const auto fast = GENERATE( false, true );
        const auto indexedTime = GENERATE( QDateTime{}, IndexedAt );
        const auto modificationTime = GENERATE( IndexedAt, ModifiedLater );

        THEN( "only the header and tail are read, whatever the setting and the times" )
        {
            REQUIRE( digestCoverage( IndexFitRequest::OpenOrFollow,
                                     fastModificationDetection( fast ), indexedTime,
                                     modificationTime )
                     == DigestCoverage::HeaderAndTail );
        }
    }
}

SCENARIO( "Reloading checks a cached Index by every byte it was built from, unless told to be fast",
          "[digestcoverage]" )
{
    const auto indexedTime = GENERATE( QDateTime{}, IndexedAt );
    const auto modificationTime = GENERATE( IndexedAt, ModifiedLater );

    GIVEN( "Fast modification detection off" )
    {
        THEN( "the full digest is checked unless the Log File grew, even with the same "
              "modification time (#337)" )
        {
            REQUIRE( digestCoverage( IndexFitRequest::Reload, fastModificationDetection( false ),
                                     indexedTime, modificationTime )
                     == DigestCoverage::FullUnlessGrown );
        }
    }

    GIVEN( "Fast modification detection on" )
    {
        THEN( "only the header and tail are read: there is no full digest to check against" )
        {
            REQUIRE( digestCoverage( IndexFitRequest::Reload, fastModificationDetection( true ),
                                     indexedTime, modificationTime )
                     == DigestCoverage::HeaderAndTail );
        }
    }
}

SCENARIO( "Checking a followed Log File for changes reads all of it only when it was modified",
          "[digestcoverage]" )
{
    GIVEN( "Fast modification detection on" )
    {
        const auto indexedTime = GENERATE( QDateTime{}, IndexedAt );
        const auto modificationTime = GENERATE( IndexedAt, ModifiedLater );

        THEN( "only the header and tail are read, whatever the times" )
        {
            REQUIRE( digestCoverage( IndexFitRequest::CheckForChanges,
                                     fastModificationDetection( true ), indexedTime,
                                     modificationTime )
                     == DigestCoverage::HeaderAndTail );
        }
    }

    GIVEN( "Fast modification detection off" )
    {
        const auto policy = fastModificationDetection( false );

        WHEN( "the modification time is the one the Index was last checked at" )
        {
            THEN( "only the header and tail are read (#277)" )
            {
                REQUIRE(
                    digestCoverage( IndexFitRequest::CheckForChanges, policy, IndexedAt, IndexedAt )
                    == DigestCoverage::HeaderAndTail );
            }
        }

        WHEN( "the Log File was modified since" )
        {
            THEN( "the full digest is checked unless it grew" )
            {
                REQUIRE( digestCoverage( IndexFitRequest::CheckForChanges, policy, IndexedAt,
                                         ModifiedLater )
                         == DigestCoverage::FullUnlessGrown );
            }
        }

        WHEN( "the time the Index was last checked at is unknown" )
        {
            const auto modificationTime = GENERATE( QDateTime{}, IndexedAt );

            THEN( "the full digest is checked unless it grew" )
            {
                REQUIRE( digestCoverage( IndexFitRequest::CheckForChanges, policy, QDateTime{},
                                         modificationTime )
                         == DigestCoverage::FullUnlessGrown );
            }
        }
    }
}

TEST_CASE( "What asked for a Log File to be indexed in full is the request a cached Index is "
           "checked for",
           "[digestcoverage]" )
{
    REQUIRE( indexFitRequest( FullIndexRequest::Automatic ) == IndexFitRequest::OpenOrFollow );
    REQUIRE( indexFitRequest( FullIndexRequest::ExplicitReload ) == IndexFitRequest::Reload );
}
