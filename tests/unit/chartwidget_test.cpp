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

// Selecting a point of a chart with the mouse (#299): the chart finds the point
// under the mouse among what it plotted, by binary search, on series of a
// million points, in x order or not, and after zooming in.

#include <catch2/catch.hpp>

#include <algorithm>
#include <optional>
#include <random>

#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include "chartplot.h"
#include "chartseries.h"
#include "chartwidget.h"

namespace {

constexpr int WidgetWidth = 400;
constexpr int WidgetHeight = 300;
constexpr int Count = 1'000'000;
constexpr uint64_t SpikeLine = 612'345;

// A series of a million points, one per Log Line, all at 0 but for one at 100
// on SpikeLine. The x values are the line numbers, or the same values in
// another order.
ChartSeriesDefinition spikeSeries( bool inXOrder )
{
    std::vector<double> xs( Count );
    for ( int i = 0; i < Count; ++i ) {
        xs[ static_cast<size_t>( i ) ] = i;
    }
    if ( !inXOrder ) {
        std::mt19937 random( 299 );
        std::shuffle( xs.begin(), xs.end(), random );
    }

    ChartSeriesDefinition series;
    series.name = "Spike";
    series.color = QColor( "#2196F3" );
    series.points.reserve( Count );
    for ( int i = 0; i < Count; ++i ) {
        const auto line = static_cast<uint64_t>( i );
        series.points.append( ChartPoint{ LineNumber{ line },
                                          xs[ static_cast<size_t>( i ) ],
                                          line == SpikeLine ? 100.0 : 0.0,
                                          {} } );
    }
    return series;
}

// Where the widget, fitted to the series, shows a point: the view has 5 %
// padding around the data.
QPointF fittedPixel( const ChartPoint& point )
{
    const double xPad = ( Count - 1 ) * 0.05;
    const ChartViewport viewport{ QRectF( 60, 10, WidgetWidth - 70, WidgetHeight - 40 ), -xPad,
                                  Count - 1 + xPad, -5.0, 105.0 };
    return viewport.toPixel( point.xValue, point.value );
}

std::optional<LineNumber> clickedLine( ChartWidget& chart, QPointF pos )
{
    std::optional<LineNumber> selected;
    const auto connection = QObject::connect(
        &chart, &ChartWidget::lineSelected, [ &selected ]( LineNumber line ) { selected = line; } );
    QMouseEvent press( QEvent::MouseButtonPress, pos, chart.mapToGlobal( pos ), Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( &chart, &press );
    QObject::disconnect( connection );
    return selected;
}

void zoomIn( ChartWidget& chart, QPointF pos, int steps )
{
    for ( int i = 0; i < steps; ++i ) {
        QWheelEvent wheel( pos, chart.mapToGlobal( pos ), QPoint(), QPoint( 0, 120 ), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false );
        QCoreApplication::sendEvent( &chart, &wheel );
    }
}

} // namespace

TEST_CASE( "Clicking a point of a chart of a million points selects its Log Line", "[chartwidget]" )
{
    const bool inXOrder = GENERATE( true, false );
    CAPTURE( inXOrder );

    const auto series = spikeSeries( inXOrder );
    ChartWidget chart;
    chart.resize( WidgetWidth, WidgetHeight );
    chart.setSeriesList( { series } );

    const QPointF spike = fittedPixel( series.points[ SpikeLine ] );

    SECTION( "on the point" )
    {
        CHECK( clickedLine( chart, spike ) == LineNumber{ SpikeLine } );
    }

    SECTION( "a few pixels off the point" )
    {
        CHECK( clickedLine( chart, spike + QPointF( 0.0, 6.0 ) ) == LineNumber{ SpikeLine } );
    }

    SECTION( "far from any point" )
    {
        CHECK( clickedLine( chart, spike + QPointF( 0.0, 40.0 ) ) == std::nullopt );
    }

    SECTION( "zoomed in around the point" )
    {
        zoomIn( chart, spike, 25 );
        CHECK( clickedLine( chart, spike ) == LineNumber{ SpikeLine } );
        CHECK( clickedLine( chart, spike + QPointF( 0.0, 40.0 ) ) == std::nullopt );
    }
}
