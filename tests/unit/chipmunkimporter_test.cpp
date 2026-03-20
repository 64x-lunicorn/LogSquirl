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

#include "chipmunkimporter.h"

using namespace logsquirl::chipmunk;

// Helper to build a minimal Chipmunk JSON export with one group containing given filter entries.
// Each entry is specified as {pattern, foreColor, backColor, active, regexFlag, casesFlag}.
static QByteArray buildChipmunkJson( const QString& groupName,
                                     const QJsonArray& entries )
{
    // Build the inner "content" JSON string
    QJsonObject contentObj;
    contentObj[ "n" ] = groupName;
    contentObj[ "e" ] = entries;

    const auto contentStr
        = QString::fromUtf8( QJsonDocument( contentObj ).toJson( QJsonDocument::Compact ) );

    // Build a group item
    QJsonObject groupItem;
    groupItem[ "content" ] = contentStr;

    // Top-level object
    QJsonObject topObj;
    topObj[ "c" ] = QJsonArray{ groupItem };

    return QJsonDocument( topObj ).toJson( QJsonDocument::Compact );
}

// Helper to build a single entry's "filters" JSON string
static QJsonObject buildEntry( const QString& pattern, const QString& fgColor,
                               const QString& bgColor, bool active, bool regexFlag,
                               bool casesFlag )
{
    QJsonObject flagsObj;
    flagsObj[ "reg" ] = regexFlag;
    flagsObj[ "cases" ] = casesFlag;
    flagsObj[ "word" ] = false;

    QJsonObject innerFilter;
    innerFilter[ "filter" ] = pattern;
    innerFilter[ "flags" ] = flagsObj;

    QJsonObject colorsObj;
    colorsObj[ "color" ] = fgColor;
    colorsObj[ "background" ] = bgColor;

    QJsonObject filtersObj;
    filtersObj[ "filter" ] = innerFilter;
    filtersObj[ "active" ] = active;
    filtersObj[ "colors" ] = colorsObj;

    // The "filters" field is itself a JSON-encoded string
    const auto filtersStr
        = QString::fromUtf8( QJsonDocument( filtersObj ).toJson( QJsonDocument::Compact ) );

    QJsonObject entry;
    entry[ "filters" ] = filtersStr;
    return entry;
}

SCENARIO( "Parsing Chipmunk JSON export", "[chipmunkimporter]" )
{
    GIVEN( "A valid Chipmunk JSON with two filter entries" )
    {
        QJsonArray entries;
        entries.append( buildEntry( "ERROR", "#FF0000", "#000000", true, true, false ) );
        entries.append( buildEntry( "WARN", "#FFFF00", "#333333", false, true, true ) );

        const auto json = buildChipmunkJson( "LogLevels", entries );

        WHEN( "The JSON is parsed" )
        {
            const auto filters = parseChipmunkJson( json );

            THEN( "Two filters are extracted" )
            {
                REQUIRE( filters.size() == 2 );
            }

            THEN( "The first filter has correct fields" )
            {
                const auto& f = filters[ 0 ];
                REQUIRE( f.groupName == "LogLevels" );
                REQUIRE( f.pattern == "ERROR" );
                REQUIRE( f.useRegex == true );
                // cases=false means ignoreCase=true
                REQUIRE( f.ignoreCase == true );
                REQUIRE( f.active == true );
                REQUIRE( f.foreColor == QColor( "#FF0000" ) );
                REQUIRE( f.backColor == QColor( "#000000" ) );
            }

            THEN( "The second filter respects the cases flag" )
            {
                const auto& f = filters[ 1 ];
                REQUIRE( f.pattern == "WARN" );
                // cases=true means ignoreCase=false (case-sensitive)
                REQUIRE( f.ignoreCase == false );
                REQUIRE( f.active == false );
                REQUIRE( f.foreColor == QColor( "#FFFF00" ) );
                REQUIRE( f.backColor == QColor( "#333333" ) );
            }
        }
    }

    GIVEN( "A Chipmunk JSON with multiple groups" )
    {
        // Build two groups manually
        QJsonArray entries1;
        entries1.append( buildEntry( "GET /api", "#FFFFFF", "#0000FF", true, true, false ) );

        QJsonArray entries2;
        entries2.append( buildEntry( "POST /api", "#FFFFFF", "#00FF00", true, false, false ) );

        QJsonObject content1;
        content1[ "n" ] = "HTTP GET";
        content1[ "e" ] = entries1;

        QJsonObject content2;
        content2[ "n" ] = "HTTP POST";
        content2[ "e" ] = entries2;

        QJsonObject group1;
        group1[ "content" ]
            = QString::fromUtf8( QJsonDocument( content1 ).toJson( QJsonDocument::Compact ) );

        QJsonObject group2;
        group2[ "content" ]
            = QString::fromUtf8( QJsonDocument( content2 ).toJson( QJsonDocument::Compact ) );

        QJsonObject topObj;
        topObj[ "c" ] = QJsonArray{ group1, group2 };
        const auto json = QJsonDocument( topObj ).toJson( QJsonDocument::Compact );

        WHEN( "The JSON is parsed" )
        {
            const auto filters = parseChipmunkJson( json );

            THEN( "Filters from both groups are collected" )
            {
                REQUIRE( filters.size() == 2 );
                REQUIRE( filters[ 0 ].groupName == "HTTP GET" );
                REQUIRE( filters[ 0 ].pattern == "GET /api" );
                REQUIRE( filters[ 1 ].groupName == "HTTP POST" );
                REQUIRE( filters[ 1 ].pattern == "POST /api" );
                REQUIRE( filters[ 1 ].useRegex == false );
            }
        }
    }

    GIVEN( "A wrapped Chipmunk JSON (Format B: top-level array)" )
    {
        // Build inner content with one group containing two filters
        QJsonArray entries;
        entries.append( buildEntry( "{CS}Backend", "#FFFFFF", "#e17055", true, false, false ) );
        entries.append( buildEntry( "TokenManager", "#FFFFFF", "#8e44ad", false, false, false ) );

        QJsonObject groupContent;
        groupContent[ "n" ] = "MIB4_Filters";
        groupContent[ "e" ] = entries;

        const auto groupContentStr
            = QString::fromUtf8( QJsonDocument( groupContent ).toJson( QJsonDocument::Compact ) );

        // Wrap group in a "c" array inside the inner content object
        QJsonObject groupItem;
        groupItem[ "uuid" ] = "group-uuid-123";
        groupItem[ "content" ] = groupContentStr;

        QJsonObject innerContent;
        innerContent[ "c" ] = QJsonArray{ groupItem };
        innerContent[ "d" ] = QJsonArray{};

        const auto innerStr
            = QString::fromUtf8( QJsonDocument( innerContent ).toJson( QJsonDocument::Compact ) );

        // Outer wrapper: top-level array with one item
        QJsonObject outerWrapper;
        outerWrapper[ "uuid" ] = "outer-uuid-456";
        outerWrapper[ "content" ] = innerStr;

        const auto json
            = QJsonDocument( QJsonArray{ outerWrapper } ).toJson( QJsonDocument::Compact );

        WHEN( "The wrapped JSON is parsed" )
        {
            const auto filters = parseChipmunkJson( json );

            THEN( "Both filters are extracted from the wrapped format" )
            {
                REQUIRE( filters.size() == 2 );
                REQUIRE( filters[ 0 ].groupName == "MIB4_Filters" );
                REQUIRE( filters[ 0 ].pattern == "{CS}Backend" );
                REQUIRE( filters[ 0 ].active == true );
                REQUIRE( filters[ 0 ].foreColor == QColor( "#FFFFFF" ) );
                REQUIRE( filters[ 0 ].backColor == QColor( "#e17055" ) );

                REQUIRE( filters[ 1 ].pattern == "TokenManager" );
                REQUIRE( filters[ 1 ].active == false );
            }
        }
    }

    GIVEN( "Malformed JSON input" )
    {
        WHEN( "The input is empty" )
        {
            const auto filters = parseChipmunkJson( QByteArray{} );
            THEN( "An empty list is returned" )
            {
                REQUIRE( filters.isEmpty() );
            }
        }

        WHEN( "The input is not valid JSON" )
        {
            const auto filters = parseChipmunkJson( "not json at all" );
            THEN( "An empty list is returned" )
            {
                REQUIRE( filters.isEmpty() );
            }
        }

        WHEN( "The input is a JSON array without content fields" )
        {
            const auto filters = parseChipmunkJson( "[1,2,3]" );
            THEN( "An empty list is returned" )
            {
                REQUIRE( filters.isEmpty() );
            }
        }

        WHEN( "The 'c' field is missing" )
        {
            const auto filters = parseChipmunkJson( R"({"x": 1})" );
            THEN( "An empty list is returned" )
            {
                REQUIRE( filters.isEmpty() );
            }
        }

        WHEN( "An entry has an empty pattern" )
        {
            QJsonArray entries;
            entries.append( buildEntry( "", "#FF0000", "#000000", true, true, false ) );
            entries.append( buildEntry( "VALID", "#FF0000", "#000000", true, true, false ) );
            const auto json = buildChipmunkJson( "Test", entries );

            const auto filters = parseChipmunkJson( json );
            THEN( "Only the non-empty pattern is included" )
            {
                REQUIRE( filters.size() == 1 );
                REQUIRE( filters[ 0 ].pattern == "VALID" );
            }
        }
    }
}

SCENARIO( "Converting Chipmunk filters to PredefinedFilters", "[chipmunkimporter]" )
{
    GIVEN( "A list of parsed Chipmunk filters" )
    {
        QList<ChipmunkFilter> chipmunkFilters;

        ChipmunkFilter f1;
        f1.groupName = "Errors";
        f1.pattern = "ERROR";
        f1.useRegex = true;
        chipmunkFilters.append( f1 );

        ChipmunkFilter f2;
        f2.groupName = "Warnings";
        f2.pattern = "WARN";
        f2.useRegex = false;
        chipmunkFilters.append( f2 );

        WHEN( "Converted to PredefinedFilters" )
        {
            const auto filters = toFilters( chipmunkFilters );

            THEN( "The correct number of filters is produced" )
            {
                REQUIRE( filters.size() == 2 );
            }

            THEN( "Each filter name includes the group prefix" )
            {
                REQUIRE( filters[ 0 ].name == "Errors: ERROR" );
                REQUIRE( filters[ 1 ].name == "Warnings: WARN" );
            }

            THEN( "Pattern and regex flag are preserved" )
            {
                REQUIRE( filters[ 0 ].pattern == "ERROR" );
                REQUIRE( filters[ 0 ].useRegex == true );
                REQUIRE( filters[ 1 ].pattern == "WARN" );
                REQUIRE( filters[ 1 ].useRegex == false );
            }
        }
    }

    GIVEN( "A Chipmunk filter with an empty group name" )
    {
        QList<ChipmunkFilter> chipmunkFilters;
        ChipmunkFilter f;
        f.groupName = "";
        f.pattern = "DEBUG";
        f.useRegex = true;
        chipmunkFilters.append( f );

        WHEN( "Converted to PredefinedFilters" )
        {
            const auto filters = toFilters( chipmunkFilters );

            THEN( "The name falls back to the pattern" )
            {
                REQUIRE( filters[ 0 ].name == "DEBUG" );
            }
        }
    }
}

SCENARIO( "Converting Chipmunk filters to a HighlighterSet", "[chipmunkimporter]" )
{
    GIVEN( "A list of parsed Chipmunk filters with colors" )
    {
        QList<ChipmunkFilter> chipmunkFilters;

        ChipmunkFilter f1;
        f1.pattern = "ERROR";
        f1.useRegex = true;
        f1.ignoreCase = false;
        f1.foreColor = QColor( "#FF0000" );
        f1.backColor = QColor( "#000000" );
        chipmunkFilters.append( f1 );

        ChipmunkFilter f2;
        f2.pattern = "INFO";
        f2.useRegex = true;
        f2.ignoreCase = true;
        f2.foreColor = QColor( "#00FF00" );
        f2.backColor = QColor( "#FFFFFF" );
        chipmunkFilters.append( f2 );

        WHEN( "Converted to a HighlighterSet" )
        {
            const auto set = toHighlighterSet( chipmunkFilters, "Imported" );

            THEN( "The set has the given name" )
            {
                REQUIRE( set.name() == "Imported" );
            }

            THEN( "The set is not empty" )
            {
                REQUIRE_FALSE( set.isEmpty() );
            }
        }
    }
}
