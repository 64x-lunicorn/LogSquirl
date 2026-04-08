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

#include "chartseries.h"

#include <QJsonObject>
#include <QRegularExpression>

SCENARIO( "ChartSeriesDefinition regex extraction", "[chartseries]" )
{
    GIVEN( "A series with a simple numeric capture pattern" )
    {
        ChartSeriesDefinition def;
        def.id = "test-1";
        def.name = "Duration";
        def.color = QColor( "#FF0000" );
        def.pattern = "duration=(\\d+\\.?\\d*)ms";
        def.captureGroup = 1;
        def.visible = true;
        REQUIRE( def.compilePattern() );

        WHEN( "A matching line is tested" )
        {
            const QString line = "2026-04-08 INFO request completed duration=123.45ms";
            const auto match = def.compiledRegex.match( line );

            THEN( "The captured value is numeric" )
            {
                REQUIRE( match.hasMatch() );
                REQUIRE( match.lastCapturedIndex() >= 1 );
                bool ok = false;
                const double val = match.captured( 1 ).toDouble( &ok );
                REQUIRE( ok );
                REQUIRE( val == Approx( 123.45 ) );
            }
        }

        WHEN( "A non-matching line is tested" )
        {
            const QString line = "2026-04-08 INFO server started on port 8080";
            const auto match = def.compiledRegex.match( line );

            THEN( "No match is found" )
            {
                REQUIRE_FALSE( match.hasMatch() );
            }
        }
    }

    GIVEN( "A series with an invalid regex pattern" )
    {
        ChartSeriesDefinition def;
        def.pattern = "[invalid(";

        WHEN( "The pattern is compiled" )
        {
            (void)def.compilePattern();

            THEN( "Compilation reports failure" )
            {
                // QRegularExpression doesn't fail on compile, but isValid().
                REQUIRE_FALSE( def.compiledRegex.isValid() );
            }
        }
    }

    GIVEN( "A series with multiple capture groups" )
    {
        ChartSeriesDefinition def;
        def.pattern = "(\\w+)=(\\d+)";
        def.captureGroup = 2;
        REQUIRE( def.compilePattern() );

        WHEN( "A matching line is tested with captureGroup=2" )
        {
            const QString line = "cpu=85 mem=1024";
            const auto match = def.compiledRegex.match( line );

            THEN( "The second capture group is used" )
            {
                REQUIRE( match.hasMatch() );
                REQUIRE( match.lastCapturedIndex() >= 2 );
                bool ok = false;
                const double val = match.captured( 2 ).toDouble( &ok );
                REQUIRE( ok );
                REQUIRE( val == Approx( 85.0 ) );
            }
        }
    }
}

SCENARIO( "ChartSeriesDefinition JSON serialization", "[chartseries]" )
{
    GIVEN( "A fully populated series definition" )
    {
        ChartSeriesDefinition def;
        def.id = "uuid-123";
        def.name = "Latency";
        def.color = QColor( "#2196F3" );
        def.pattern = "lat=(\\d+)";
        def.captureGroup = 1;
        def.visible = true;

        WHEN( "Serialized to JSON and back" )
        {
            const auto json = def.toJson();
            const auto restored = ChartSeriesDefinition::fromJson( json );

            THEN( "All fields are preserved" )
            {
                REQUIRE( restored.id == "uuid-123" );
                REQUIRE( restored.name == "Latency" );
                REQUIRE( restored.color == QColor( "#2196F3" ) );
                REQUIRE( restored.pattern == "lat=(\\d+)" );
                REQUIRE( restored.captureGroup == 1 );
                REQUIRE( restored.visible );
                REQUIRE( restored.compiledRegex.isValid() );
            }
        }
    }

    GIVEN( "A JSON object with missing fields" )
    {
        QJsonObject obj;
        obj[ "name" ] = "Minimal";
        obj[ "pattern" ] = "(\\d+)";

        WHEN( "Deserialized" )
        {
            const auto def = ChartSeriesDefinition::fromJson( obj );

            THEN( "Defaults are applied" )
            {
                REQUIRE_FALSE( def.id.isEmpty() );
                REQUIRE( def.name == "Minimal" );
                REQUIRE( def.captureGroup == 1 );
                REQUIRE( def.visible );
                REQUIRE( def.color == QColor( "#2196F3" ) );
            }
        }
    }
}

SCENARIO( "ChartPoint data model", "[chartseries]" )
{
    GIVEN( "A chart point" )
    {
        ChartPoint pt{ LineNumber( 42 ), 99.5 };

        THEN( "Line and value are stored" )
        {
            REQUIRE( pt.line.get() == 42 );
            REQUIRE( pt.value == Approx( 99.5 ) );
        }
    }
}
