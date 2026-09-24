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

// The Chart Panel's series and its Value Count tabs, without a Log File (#444).

#include <catch2/catch_test_macros.hpp>

#include <QRegularExpression>
#include <QTabWidget>

#include "chartpanel.h"
#include "chartseries.h"

namespace {

ChartSeriesDefinition seriesNamed( const QString& id, const QString& name )
{
    ChartSeriesDefinition def;
    def.id = id;
    def.name = name;
    def.pattern = R"(took (\d+) ms)";
    def.captureGroup = 1;
    def.compilePattern();
    return def;
}

QTabWidget* tabsOf( ChartPanel& panel )
{
    auto* tabs = panel.findChild<QTabWidget*>();
    REQUIRE( tabs != nullptr );
    return tabs;
}

} // namespace

TEST_CASE( "A Chart Panel starts with no series and gives back the ones it was given",
           "[chartpanel]" )
{
    ChartPanel panel;
    CHECK( panel.seriesDefinitions().isEmpty() );

    panel.setSeriesDefinitions( { seriesNamed( "a", "First" ), seriesNamed( "b", "Second" ) } );
    const auto definitions = panel.seriesDefinitions();
    REQUIRE( definitions.size() == 2 );
    CHECK( definitions[ 0 ].id == "a" );
    CHECK( definitions[ 0 ].name == "First" );
    CHECK( definitions[ 1 ].id == "b" );
    CHECK( definitions[ 1 ].pattern == R"(took (\d+) ms)" );

    panel.setSeriesDefinitions( {} );
    CHECK( panel.seriesDefinitions().isEmpty() );
}

TEST_CASE( "A Filter frequency series counts each pattern with the Search's Match case",
           "[chartpanel]" )
{
    ChartPanel panel;

    panel.addFilterFrequencySeries( { "error", "warn" }, false );
    const auto definitions = panel.seriesDefinitions();
    REQUIRE( definitions.size() == 2 );
    CHECK( definitions[ 0 ].pattern.contains( "error" ) );
    CHECK( definitions[ 1 ].pattern.contains( "warn" ) );
    for ( const auto& definition : definitions ) {
        CHECK_FALSE( definition.matchCase );
        // A count, not a value: the whole match.
        CHECK( definition.captureGroup == 0 );
    }
}

TEST_CASE( "A Value Count needs a Log File, and a Log Format for a field", "[chartpanel]" )
{
    ChartPanel panel;
    const auto tabsBefore = tabsOf( panel )->count();

    panel.countFieldValues( "level" );
    panel.countCaptureGroupValues( QRegularExpression( "(\\w+)" ), 1, "group 1" );

    CHECK( tabsOf( panel )->count() == tabsBefore );
}
