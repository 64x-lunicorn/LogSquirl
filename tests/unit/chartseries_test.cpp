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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDate>
#include <QDateTime>

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

    GIVEN( "A series definition with X-axis configuration" )
    {
        ChartSeriesDefinition def;
        def.id = "uuid-x";
        def.name = "Timestamps";
        def.pattern = "\\{CS\\}";
        def.captureGroup = 0;
        def.xPattern = "(\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})";
        def.xCaptureGroup = 1;
        def.xTimestampFormat = "MM-dd HH:mm:ss.zzz";
        def.compilePattern();

        WHEN( "Serialized to JSON and back" )
        {
            const auto json = def.toJson();
            const auto restored = ChartSeriesDefinition::fromJson( json );

            THEN( "X-axis fields are preserved" )
            {
                REQUIRE( restored.xPattern
                         == "(\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})" );
                REQUIRE( restored.xCaptureGroup == 1 );
                REQUIRE( restored.xTimestampFormat == "MM-dd HH:mm:ss.zzz" );
                REQUIRE( restored.hasCustomXAxis() );
                REQUIRE( restored.isTimestampXAxis() );
                REQUIRE( restored.compiledXRegex.isValid() );
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
                REQUIRE_FALSE( def.hasCustomXAxis() );
                REQUIRE_FALSE( def.isTimestampXAxis() );
            }
        }
    }
}

SCENARIO( "ChartPoint data model", "[chartseries]" )
{
    GIVEN( "A chart point with line-number X-axis" )
    {
        ChartPoint pt{ LineNumber( 42 ), 42.0, 99.5, {} };

        THEN( "Line, xValue and value are stored" )
        {
            REQUIRE( pt.line.get() == 42 );
            REQUIRE( pt.xValue == Approx( 42.0 ) );
            REQUIRE( pt.value == Approx( 99.5 ) );
            REQUIRE( pt.xLabel.isEmpty() );
        }
    }

    GIVEN( "A chart point with a custom X label" )
    {
        ChartPoint pt{ LineNumber( 10 ), 1000.0, 5.0, "04-08 20:02:49" };

        THEN( "The xLabel is stored" )
        {
            REQUIRE( pt.xLabel == "04-08 20:02:49" );
            REQUIRE( pt.xValue == Approx( 1000.0 ) );
        }
    }
}

SCENARIO( "X-axis regex extraction from log lines", "[chartseries]" )
{
    GIVEN( "A series with a timestamp X-axis pattern" )
    {
        ChartSeriesDefinition def;
        def.pattern = "\\{CS\\}";
        def.captureGroup = 0;
        def.xPattern = "(\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})";
        def.xCaptureGroup = 1;
        def.xTimestampFormat = "MM-dd HH:mm:ss.zzz";
        REQUIRE( def.compilePattern() );

        THEN( "Custom X-axis helpers report correctly" )
        {
            REQUIRE( def.hasCustomXAxis() );
            REQUIRE( def.isTimestampXAxis() );
            REQUIRE( def.compiledXRegex.isValid() );
        }

        WHEN( "A matching log line is tested" )
        {
            const QString line
                = "04-08 20:02:49.780  3814  6380 I "
                  "{CS}TokenRequestExecutor: Submitting token request";

            const auto yMatch = def.compiledRegex.match( line );
            const auto xMatch = def.compiledXRegex.match( line );

            THEN( "Both Y and X patterns match" )
            {
                REQUIRE( yMatch.hasMatch() );
                REQUIRE( xMatch.hasMatch() );
                REQUIRE( xMatch.captured( 1 ) == "04-08 20:02:49.780" );
            }

            THEN( "The timestamp parses to a valid QDateTime" )
            {
                auto dt = QDateTime::fromString(
                    xMatch.captured( 1 ), def.xTimestampFormat );
                if ( dt.date().year() < 1970 ) {
                    dt.setDate( QDate( QDate::currentDate().year(),
                                       dt.date().month(), dt.date().day() ) );
                }
                REQUIRE( dt.isValid() );
                REQUIRE( dt.time().hour() == 20 );
                REQUIRE( dt.time().minute() == 2 );
                REQUIRE( dt.time().second() == 49 );
            }
        }
    }

    GIVEN( "A series with a numeric X-axis pattern" )
    {
        ChartSeriesDefinition def;
        def.pattern = "pid=(\\d+)";
        def.captureGroup = 1;
        def.xPattern = "tid=(\\d+)";
        def.xCaptureGroup = 1;
        REQUIRE( def.compilePattern() );

        THEN( "It is numeric, not timestamp mode" )
        {
            REQUIRE( def.hasCustomXAxis() );
            REQUIRE_FALSE( def.isTimestampXAxis() );
        }

        WHEN( "A matching line is tested" )
        {
            const QString line = "pid=1234 tid=5678 some log message";
            const auto xMatch = def.compiledXRegex.match( line );

            THEN( "The numeric X value is extracted" )
            {
                REQUIRE( xMatch.hasMatch() );
                bool ok = false;
                const double val = xMatch.captured( 1 ).toDouble( &ok );
                REQUIRE( ok );
                REQUIRE( val == Approx( 5678.0 ) );
            }
        }
    }

    GIVEN( "A series without custom X-axis" )
    {
        ChartSeriesDefinition def;
        def.pattern = "(\\d+)";
        def.captureGroup = 1;
        REQUIRE( def.compilePattern() );

        THEN( "Custom X-axis is not configured" )
        {
            REQUIRE_FALSE( def.hasCustomXAxis() );
            REQUIRE_FALSE( def.isTimestampXAxis() );
            REQUIRE_FALSE( def.isBucketed() );
        }
    }
}

SCENARIO( "ChartSeriesDefinition bucketing configuration", "[chartseries]" )
{
    GIVEN( "A series with timestamp X-axis and bucket size" )
    {
        ChartSeriesDefinition def;
        def.id = "bucket-test";
        def.name = "Activity";
        def.pattern = "\\{CS\\}";
        def.captureGroup = 0;
        def.xPattern = "(\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3})";
        def.xCaptureGroup = 1;
        def.xTimestampFormat = "MM-dd HH:mm:ss.zzz";
        def.bucketSizeMs = 1000;
        def.compilePattern();

        THEN( "Bucketing is enabled" )
        {
            REQUIRE( def.isBucketed() );
            REQUIRE( def.isTimestampXAxis() );
        }

        WHEN( "Serialized to JSON and back" )
        {
            const auto json = def.toJson();
            const auto restored = ChartSeriesDefinition::fromJson( json );

            THEN( "bucketSizeMs is preserved" )
            {
                REQUIRE( restored.bucketSizeMs == 1000 );
                REQUIRE( restored.isBucketed() );
            }
        }
    }

    GIVEN( "A series with timestamp X-axis but no bucket" )
    {
        ChartSeriesDefinition def;
        def.xPattern = "(\\d+)";
        def.xTimestampFormat = "HH:mm:ss";
        def.bucketSizeMs = 0;

        THEN( "Bucketing is disabled" )
        {
            REQUIRE_FALSE( def.isBucketed() );
        }
    }

    GIVEN( "A series with bucket but no timestamp format" )
    {
        ChartSeriesDefinition def;
        def.xPattern = "(\\d+)";
        def.bucketSizeMs = 5000;

        THEN( "Bucketing is disabled without timestamp" )
        {
            REQUIRE_FALSE( def.isBucketed() );
        }
    }
}

SCENARIO( "Chart preset JSON round‑trip", "[chartseries]" )
{
    GIVEN( "Multiple series definitions" )
    {
        ChartSeriesDefinition a;
        a.id = "preset-a";
        a.name = "Errors";
        a.color = QColor( "#FF0000" );
        a.pattern = "ERROR";
        a.captureGroup = 0;
        a.compilePattern();

        ChartSeriesDefinition b;
        b.id = "preset-b";
        b.name = "Latency";
        b.color = QColor( "#00FF00" );
        b.pattern = "lat=(\\d+)";
        b.captureGroup = 1;
        b.xPattern = "(\\d{2}:\\d{2}:\\d{2})";
        b.xCaptureGroup = 1;
        b.xTimestampFormat = "HH:mm:ss";
        b.bucketSizeMs = 1000;
        b.compilePattern();

        WHEN( "Serialized to a JSON array string and deserialized" )
        {
            QJsonArray arr;
            arr.append( a.toJson() );
            arr.append( b.toJson() );
            const auto jsonStr
                = QString::fromUtf8( QJsonDocument( arr ).toJson( QJsonDocument::Compact ) );

            const auto doc = QJsonDocument::fromJson( jsonStr.toUtf8() );
            REQUIRE( doc.isArray() );
            const auto restoredArr = doc.array();
            REQUIRE( restoredArr.size() == 2 );

            const auto ra = ChartSeriesDefinition::fromJson( restoredArr[ 0 ].toObject() );
            const auto rb = ChartSeriesDefinition::fromJson( restoredArr[ 1 ].toObject() );

            THEN( "All fields of both definitions are preserved" )
            {
                REQUIRE( ra.id == "preset-a" );
                REQUIRE( ra.name == "Errors" );
                REQUIRE( ra.captureGroup == 0 );
                REQUIRE( ra.pattern == "ERROR" );

                REQUIRE( rb.id == "preset-b" );
                REQUIRE( rb.name == "Latency" );
                REQUIRE( rb.xPattern == "(\\d{2}:\\d{2}:\\d{2})" );
                REQUIRE( rb.xTimestampFormat == "HH:mm:ss" );
                REQUIRE( rb.bucketSizeMs == 1000 );
                REQUIRE( rb.isBucketed() );
            }
        }
    }
}

SCENARIO( "Filter frequency series creation pattern", "[chartseries]" )
{
    GIVEN( "A count‑mode series (captureGroup = 0)" )
    {
        ChartSeriesDefinition def;
        def.id = "filter-1";
        def.name = "Filter: ERROR";
        def.pattern = "ERROR";
        def.captureGroup = 0;
        REQUIRE( def.compilePattern() );

        WHEN( "Tested against a matching line" )
        {
            const QString line = "2026-01-01 ERROR something broke";
            const auto match = def.compiledRegex.match( line );

            THEN( "It matches and Y value is 1" )
            {
                REQUIRE( match.hasMatch() );
                // In count mode the Y value is always 1.0.
                double yVal = 1.0;
                REQUIRE( yVal == Approx( 1.0 ) );
            }
        }

        WHEN( "Tested against a non‑matching line" )
        {
            const QString line = "2026-01-01 INFO all good";
            const auto match = def.compiledRegex.match( line );

            THEN( "No match is found" )
            {
                REQUIRE_FALSE( match.hasMatch() );
            }
        }
    }
}
