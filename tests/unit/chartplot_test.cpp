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

// What a chart plots of a series (#299): only the visible range, reduced to a
// few points per pixel column when it has more points than pixel columns, and
// the hovered point found by binary search.

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include "chartplot.h"
#include "chartseries.h"

namespace {

constexpr int ImageWidth = 400;
constexpr int ImageHeight = 300;

// The plot area of a chart widget of the image's size.
QRectF plotArea()
{
    return QRectF( 60, 10, ImageWidth - 70, ImageHeight - 40 );
}

QVector<ChartPoint> pointsAt( const std::vector<std::pair<double, double>>& xy )
{
    QVector<ChartPoint> points;
    uint64_t line = 0;
    for ( const auto& [ x, y ] : xy ) {
        points.append( ChartPoint{ LineNumber{ line++ }, x, y, {} } );
    }
    return points;
}

QImage blankImage()
{
    QImage image( ImageWidth, ImageHeight, QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::white );
    return image;
}

// A series drawn as the chart drew every series before #299: one path through
// all points in Log Line order and a dot on every point, clipped to the plot
// area.
QImage drawnAsBefore( const QVector<ChartPoint>& points, const ChartViewport& viewport,
                      const QColor& color )
{
    QImage image = blankImage();
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setClipRect( viewport.area );

    const auto toPixel = [ &viewport ]( double x, double y ) {
        const QRectF area = viewport.area;
        const double xRatio = ( x - viewport.xMin ) / ( viewport.xMax - viewport.xMin );
        const double yRatio = ( y - viewport.yMin ) / ( viewport.yMax - viewport.yMin );
        return QPointF( area.left() + xRatio * area.width(),
                        area.bottom() - yRatio * area.height() );
    };

    painter.setPen( QPen( color, 1.5 ) );
    painter.setBrush( Qt::NoBrush );
    QPainterPath path;
    bool first = true;
    for ( const auto& pt : points ) {
        const QPointF px = toPixel( pt.xValue, pt.value );
        if ( first ) {
            path.moveTo( px );
            first = false;
        }
        else {
            path.lineTo( px );
        }
    }
    painter.drawPath( path );

    painter.setBrush( color );
    painter.setPen( Qt::NoPen );
    for ( const auto& pt : points ) {
        painter.drawEllipse( toPixel( pt.xValue, pt.value ), 3.0, 3.0 );
    }
    return image;
}

QImage drawnNow( const QVector<ChartPoint>& points, const ChartViewport& viewport,
                 const QColor& color )
{
    QImage image = blankImage();
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setClipRect( viewport.area );
    drawChartPlot( painter, plotChartSeries( points, ChartSeriesXOrder( points ), viewport ),
                   color );
    return image;
}

// A zig-zag of count points, one per Log Line, x from 0 up.
std::vector<std::pair<double, double>> zigZag( int count )
{
    std::vector<std::pair<double, double>> xy;
    for ( int i = 0; i < count; ++i ) {
        xy.emplace_back( i, ( i * 37 ) % 23 + ( i % 2 ) * 5.5 );
    }
    return xy;
}

} // namespace

TEST_CASE( "nearestPointIndex finds the x value nearest to a position", "[chartplot]" )
{
    const std::vector<double> xs = { 1.0, 3.0, 3.0, 10.0 };
    const auto xAt = [ &xs ]( qsizetype i ) { return xs[ static_cast<size_t>( i ) ]; };
    const auto count = static_cast<qsizetype>( xs.size() );

    CHECK( nearestPointIndex( 0, xAt, 5.0 ) == -1 );
    CHECK( nearestPointIndex( count, xAt, -100.0 ) == 0 );
    CHECK( nearestPointIndex( count, xAt, 1.9 ) == 0 );
    CHECK( nearestPointIndex( count, xAt, 2.1 ) == 1 );
    CHECK( nearestPointIndex( count, xAt, 3.0 ) == 1 );
    CHECK( nearestPointIndex( count, xAt, 6.4 ) == 2 );
    CHECK( nearestPointIndex( count, xAt, 6.6 ) == 3 );
    CHECK( nearestPointIndex( count, xAt, 100.0 ) == 3 );
}

TEST_CASE( "nearestPointIndex reads a logarithmic number of x values", "[chartplot]" )
{
    constexpr qsizetype Count = 4'000'000;
    qsizetype reads = 0;
    const auto xAt = [ &reads ]( qsizetype i ) {
        ++reads;
        return static_cast<double>( i ) * 0.5;
    };

    for ( const double x : { -1.0, 0.0, 12345.2, 999'999.9, 1'999'999.5, 5e6 } ) {
        reads = 0;
        const auto index = nearestPointIndex( Count, xAt, x );
        CHECK( index == std::clamp<qsizetype>( std::llround( x * 2.0 ), 0, Count - 1 ) );
        // log2(4,000,000) is about 22.
        CHECK( reads <= 25 );
    }
}

TEST_CASE( "A series with fewer points than pixel columns is drawn as before", "[chartplot]" )
{
    const QColor color( "#2196F3" );
    const auto points = pointsAt( zigZag( 120 ) );

    SECTION( "the whole series in view" )
    {
        const ChartViewport viewport{ plotArea(), -5.95, 124.95, -2.0, 32.0 };
        CHECK( drawnNow( points, viewport, color ) == drawnAsBefore( points, viewport, color ) );
    }

    SECTION( "zoomed in, points left and right of the view" )
    {
        const ChartViewport viewport{ plotArea(), 40.3, 71.7, 3.0, 20.0 };
        CHECK( drawnNow( points, viewport, color ) == drawnAsBefore( points, viewport, color ) );
    }

    SECTION( "panned past the last point" )
    {
        const ChartViewport viewport{ plotArea(), 118.5, 200.0, -2.0, 32.0 };
        CHECK( drawnNow( points, viewport, color ) == drawnAsBefore( points, viewport, color ) );
    }
}

TEST_CASE( "A series out of x order is plotted in x order", "[chartplot]" )
{
    const QColor color( "#E91E63" );
    auto xy = zigZag( 80 );
    const auto inOrder = pointsAt( xy );
    std::mt19937 random( 299 );
    std::shuffle( xy.begin(), xy.end(), random );
    const auto shuffled = pointsAt( xy );

    const ChartViewport viewport{ plotArea(), 10.2, 60.7, -2.0, 32.0 };
    CHECK( drawnNow( shuffled, viewport, color ) == drawnNow( inOrder, viewport, color ) );

    const auto plot = plotChartSeries( shuffled, ChartSeriesXOrder( shuffled ), viewport );
    REQUIRE( static_cast<size_t>( plot.line.size() ) == plot.pointIndexes.size() );
    for ( qsizetype i = 0; i < plot.line.size(); ++i ) {
        const auto& point = shuffled[ plot.pointIndexes[ static_cast<size_t>( i ) ] ];
        CHECK( plot.line[ i ] == viewport.toPixel( point.xValue, point.value ) );
    }
}

TEST_CASE( "A series with more points than pixel columns is reduced per pixel column",
           "[chartplot]" )
{
    // A million points; the viewport shows 1 % of them in 330 pixel columns.
    constexpr int Count = 1'000'000;
    std::vector<std::pair<double, double>> xy;
    xy.reserve( Count );
    std::mt19937 random( 299 );
    std::uniform_real_distribution<double> values( 0.0, 1000.0 );
    for ( int i = 0; i < Count; ++i ) {
        xy.emplace_back( i, values( random ) );
    }
    const auto points = pointsAt( xy );
    const ChartViewport viewport{ plotArea(), 500'000.0, 510'000.0, -10.0, 1010.0 };
    const auto plot = plotChartSeries( points, ChartSeriesXOrder( points ), viewport );

    SECTION( "at most four points per pixel column" )
    {
        CHECK( plot.reduced );
        CHECK( plot.line.size() <= 4 * ( 330 + 2 * 5 + 2 ) );
        CHECK( plot.dots.size() <= plot.line.size() );
    }

    SECTION( "every pixel column keeps its lowest and highest point" )
    {
        std::map<double, std::pair<double, double>> extremes;
        for ( const auto& point : points ) {
            const QPointF pixel = viewport.toPixel( point.xValue, point.value );
            // The pixel columns in the plot area.
            const double column = std::floor( pixel.x() );
            if ( column < viewport.area.left() || column >= viewport.area.right() ) {
                continue;
            }
            auto [ it, inserted ] = extremes.try_emplace( column, pixel.y(), pixel.y() );
            it->second.first = std::min( it->second.first, pixel.y() );
            it->second.second = std::max( it->second.second, pixel.y() );
        }
        std::map<double, std::pair<double, double>> plotted;
        for ( const QPointF& pixel : plot.line ) {
            auto [ it, inserted ]
                = plotted.try_emplace( std::floor( pixel.x() ), pixel.y(), pixel.y() );
            it->second.first = std::min( it->second.first, pixel.y() );
            it->second.second = std::max( it->second.second, pixel.y() );
        }
        for ( const auto& [ column, range ] : extremes ) {
            REQUIRE( plotted.count( column ) == 1 );
            CHECK( plotted[ column ] == range );
        }
    }

    SECTION( "the points outside the view make no difference" )
    {
        // The points in the view, 20 pixel columns to either side of it.
        const auto nearby = pointsAt(
            std::vector<std::pair<double, double>>( xy.begin() + 499'400, xy.begin() + 510'600 ) );
        const auto nearbyPlot = plotChartSeries( nearby, ChartSeriesXOrder( nearby ), viewport );
        CHECK( nearbyPlot.line == plot.line );
        CHECK( nearbyPlot.dots == plot.dots );
    }
}
