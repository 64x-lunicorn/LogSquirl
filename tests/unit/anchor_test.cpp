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

// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch.hpp>

#include "anchor.hpp"

#include <QJsonArray>

SCENARIO( "AnchorSet manages anchor pairs", "[anchor]" )
{
    GIVEN( "An empty AnchorSet" )
    {
        AnchorSet set;

        THEN( "it reports empty" )
        {
            REQUIRE( set.empty() );
            REQUIRE( set.size() == 0 );
        }

        THEN( "mapAtoB returns the input unchanged" )
        {
            REQUIRE( set.mapAtoB( 42 ) == 42 );
        }

        THEN( "mapBtoA returns the input unchanged" )
        {
            REQUIRE( set.mapBtoA( 42 ) == 42 );
        }

        WHEN( "a single anchor is added" )
        {
            set.add( 10, 20 );

            THEN( "size is 1" )
            {
                REQUIRE( set.size() == 1 );
                REQUIRE_FALSE( set.empty() );
            }

            THEN( "the anchor values are correct" )
            {
                REQUIRE( set.at( 0 ).lineA == 10 );
                REQUIRE( set.at( 0 ).lineB == 20 );
            }

            THEN( "mapAtoB applies the offset" )
            {
                // With one anchor (10→20), offset is +10
                REQUIRE( set.mapAtoB( 0 ) == 10 );
                REQUIRE( set.mapAtoB( 10 ) == 20 );
                REQUIRE( set.mapAtoB( 15 ) == 25 );
            }

            THEN( "mapBtoA applies the inverse offset" )
            {
                // With one anchor (10→20), B→A offset is -10
                REQUIRE( set.mapBtoA( 20 ) == 10 );
                REQUIRE( set.mapBtoA( 30 ) == 20 );
            }
        }

        WHEN( "two anchors are added out of order" )
        {
            set.add( 100, 200 );
            set.add( 10, 20 );

            THEN( "they are sorted by lineA" )
            {
                REQUIRE( set.at( 0 ).lineA == 10 );
                REQUIRE( set.at( 1 ).lineA == 100 );
            }
        }
    }
}

SCENARIO( "AnchorSet interpolation with two anchors", "[anchor]" )
{
    GIVEN( "Two anchors: (10,20) and (110,220)" )
    {
        AnchorSet set;
        set.add( 10, 20 );
        set.add( 110, 220 );

        THEN( "exact anchor lines map correctly" )
        {
            REQUIRE( set.mapAtoB( 10 ) == 20 );
            REQUIRE( set.mapAtoB( 110 ) == 220 );
        }

        THEN( "midpoint interpolates correctly" )
        {
            // lineA=60 is halfway → should map to 120 (halfway between 20 and 220)
            REQUIRE( set.mapAtoB( 60 ) == 120 );
        }

        THEN( "before first anchor uses first anchor offset" )
        {
            // offset from first anchor: +10
            REQUIRE( set.mapAtoB( 0 ) == 10 );
        }

        THEN( "after last anchor uses last anchor offset" )
        {
            // offset from last anchor: +110
            REQUIRE( set.mapAtoB( 200 ) == 310 );
        }

        THEN( "mapBtoA reverses correctly" )
        {
            REQUIRE( set.mapBtoA( 20 ) == 10 );
            REQUIRE( set.mapBtoA( 220 ) == 110 );
            REQUIRE( set.mapBtoA( 120 ) == 60 );
        }
    }
}

SCENARIO( "AnchorSet remove and clear", "[anchor]" )
{
    GIVEN( "An AnchorSet with three anchors" )
    {
        AnchorSet set;
        set.add( 10, 100 );
        set.add( 20, 200 );
        set.add( 30, 300 );

        WHEN( "the middle anchor is removed" )
        {
            set.remove( 1 );

            THEN( "size decreases" )
            {
                REQUIRE( set.size() == 2 );
            }

            THEN( "remaining anchors are correct" )
            {
                REQUIRE( set.at( 0 ).lineA == 10 );
                REQUIRE( set.at( 1 ).lineA == 30 );
            }
        }

        WHEN( "clear is called" )
        {
            set.clear();

            THEN( "the set is empty" )
            {
                REQUIRE( set.empty() );
            }
        }

        WHEN( "removing an out-of-range index" )
        {
            THEN( "it throws" )
            {
                REQUIRE_THROWS_AS( set.remove( 99 ), std::out_of_range );
            }
        }
    }
}

SCENARIO( "AnchorSet swapPanes", "[anchor]" )
{
    GIVEN( "An AnchorSet with anchors (10,100) and (50,200)" )
    {
        AnchorSet set;
        set.add( 10, 100 );
        set.add( 50, 200 );

        WHEN( "swapPanes is called" )
        {
            set.swapPanes();

            THEN( "A and B are swapped and re-sorted by new lineA" )
            {
                REQUIRE( set.at( 0 ).lineA == 100 );
                REQUIRE( set.at( 0 ).lineB == 10 );
                REQUIRE( set.at( 1 ).lineA == 200 );
                REQUIRE( set.at( 1 ).lineB == 50 );
            }
        }
    }
}

SCENARIO( "AnchorSet JSON round-trip", "[anchor]" )
{
    GIVEN( "An AnchorSet with two anchors" )
    {
        AnchorSet original;
        original.add( 5, 50 );
        original.add( 15, 150 );

        WHEN( "serialized to JSON and deserialized" )
        {
            const auto json = original.toJson();
            AnchorSet restored;
            restored.fromJson( json );

            THEN( "the restored set matches the original" )
            {
                REQUIRE( restored.size() == original.size() );
                REQUIRE( restored.at( 0 ) == original.at( 0 ) );
                REQUIRE( restored.at( 1 ) == original.at( 1 ) );
            }
        }
    }
}
