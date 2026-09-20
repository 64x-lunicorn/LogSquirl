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

// The colors the overview draws Match and Mark lines in: every line stands
// out against the overview background in every Theme, and a line standing
// for more Log Lines is drawn stronger (#255).

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>

#include "linedecorator.h"
#include "overview.h"
#include "overviewwidget.h"
#include "theme.h"

namespace {

constexpr int LastWeight = Overview::WeightedLine::WEIGHT_STEPS - 1;

// The WCAG contrast ratio, computed here independently of the widget.
double contrast( const QColor& first, const QColor& second )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( double c ) {
            return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( static_cast<double>( color.redF() ) )
               + 0.7152 * linear( static_cast<double>( color.greenF() ) )
               + 0.0722 * linear( static_cast<double>( color.blueF() ) );
    };
    const auto darker = std::min( luminance( first ), luminance( second ) );
    const auto lighter = std::max( luminance( first ), luminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

// The background the overview is painted on in a Theme: its Window role.
QColor overviewBackground( const QString& themeName )
{
    return Theme::fromName( themeName, Qt::ColorScheme::Light ).color( ColorToken::Window );
}

} // namespace

SCENARIO( "Match and Mark lines stand out in the overview of every Theme", "[overview][theme]" )
{
    const auto themeName = GENERATE( from_range( Theme::builtInThemes() ) );
    const auto statusColor = GENERATE( LineStatusColors::match(), LineStatusColors::mark() );
    const auto background = overviewBackground( themeName );
    INFO( themeName.toStdString() << " " << statusColor.name().toStdString() );

    THEN( "a line standing for a single Log Line reaches 3:1 against the background" )
    {
        REQUIRE( contrast( OverviewWidget::lineColor( statusColor, background, 0 ), background )
                 >= 3.0 );
    }

    THEN( "a line standing for more Log Lines contrasts at least as much" )
    {
        for ( int weight = 0; weight < LastWeight; ++weight ) {
            INFO( "weight " << weight );
            REQUIRE( contrast( OverviewWidget::lineColor( statusColor, background, weight + 1 ),
                               background )
                     >= contrast( OverviewWidget::lineColor( statusColor, background, weight ),
                                  background ) );
        }
    }

    THEN( "a line of the heaviest weight is the Match or Mark color itself" )
    {
        REQUIRE( OverviewWidget::lineColor( statusColor, background, LastWeight ) == statusColor );
    }
}

SCENARIO( "Match and Mark lines in the overview stay distinguishable", "[overview][theme]" )
{
    const auto themeName = GENERATE( from_range( Theme::builtInThemes() ) );
    const auto background = overviewBackground( themeName );
    const auto weight = GENERATE( range( 0, LastWeight + 1 ) );
    INFO( themeName.toStdString() << " weight " << weight );

    const auto match = OverviewWidget::lineColor( LineStatusColors::match(), background, weight );
    const auto mark = OverviewWidget::lineColor( LineStatusColors::mark(), background, weight );

    // Mixing into a gray background keeps the hue: a Match line stays red,
    // a Mark line blue.
    REQUIRE( match.red() > match.blue() );
    REQUIRE( mark.blue() > mark.red() );
}

SCENARIO( "A line color that cannot reach 3:1 is drawn unmixed", "[overview]" )
{
    const QColor background( "#808080" );
    const QColor faint( "#909090" );
    REQUIRE( contrast( faint, background ) < 3.0 );

    for ( int weight = 0; weight <= LastWeight; ++weight ) {
        REQUIRE( OverviewWidget::lineColor( faint, background, weight ) == faint );
    }
}
