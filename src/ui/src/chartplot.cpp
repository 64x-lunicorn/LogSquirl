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

#include "chartplot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

#include <QColor>
#include <QPainter>
#include <QPainterPath>

namespace {

// How far outside the x range of the viewport, in pixels, a point is still
// plotted: a dot there reaches into the plot area.
constexpr double PlottedMarginPx = ChartDotRadius + 2.0;

// How many segments of a reduced plot's line are stroked at a time.
constexpr qsizetype ReducedLineChunk = 8;

// The first position in x order, in [0, size], whose x value is not below x
// (or, with orEqual, above x).
qsizetype firstPositionAfter( const QVector<ChartPoint>& points, const ChartSeriesXOrder& xOrder,
                              double x, bool orEqual )
{
    qsizetype low = 0;
    qsizetype high = xOrder.size();
    while ( low < high ) {
        const qsizetype middle = low + ( high - low ) / 2;
        const double middleX = points[ xOrder.pointIndex( middle ) ].xValue;
        if ( middleX < x || ( orEqual && middleX == x ) ) {
            low = middle + 1;
        }
        else {
            high = middle;
        }
    }
    return low;
}

void appendVertex( ChartPlot& plot, const QPointF& pixel, qsizetype pointIndex )
{
    plot.line.append( pixel );
    plot.pointIndexes.push_back( pointIndex );
}

} // namespace

QPointF ChartViewport::toPixel( double x, double y ) const
{
    const double xRatio = ( x - xMin ) / ( xMax - xMin );
    const double yRatio = ( y - yMin ) / ( yMax - yMin );
    return { area.left() + xRatio * area.width(), area.bottom() - yRatio * area.height() };
}

ChartSeriesXOrder::ChartSeriesXOrder( const QVector<ChartPoint>& points )
    : size_( points.size() )
{
    const auto byX = []( const ChartPoint& a, const ChartPoint& b ) { return a.xValue < b.xValue; };
    if ( std::is_sorted( points.cbegin(), points.cend(), byX ) ) {
        return;
    }
    order_.resize( static_cast<size_t>( size_ ) );
    std::iota( order_.begin(), order_.end(), qsizetype{ 0 } );
    std::stable_sort( order_.begin(), order_.end(), [ &points ]( qsizetype a, qsizetype b ) {
        return points[ a ].xValue < points[ b ].xValue;
    } );
}

ChartPlot plotChartSeries( const QVector<ChartPoint>& points, const ChartSeriesXOrder& xOrder,
                           const ChartViewport& viewport )
{
    ChartPlot plot;
    const QRectF& area = viewport.area;
    if ( xOrder.size() == 0 || xOrder.size() != points.size() || area.width() <= 0 ) {
        return plot;
    }

    // The points in the x range and the margin, and one more on either side.
    const double margin = PlottedMarginPx * ( viewport.xMax - viewport.xMin ) / area.width();
    qsizetype first = firstPositionAfter( points, xOrder, viewport.xMin - margin, false );
    qsizetype last = firstPositionAfter( points, xOrder, viewport.xMax + margin, true );
    first = std::max<qsizetype>( first - 1, 0 );
    last = std::min( last + 1, xOrder.size() );
    const qsizetype count = last - first;

    if ( static_cast<double>( count ) <= area.width() ) {
        plot.line.reserve( count );
        plot.pointIndexes.reserve( static_cast<size_t>( count ) );
        for ( qsizetype position = first; position < last; ++position ) {
            const qsizetype index = xOrder.pointIndex( position );
            const auto& point = points[ index ];
            appendVertex( plot, viewport.toPixel( point.xValue, point.value ), index );
        }
        plot.dots = plot.line;
        return plot;
    }

    plot.reduced = true;
    // Each pixel column: its first, lowest, highest and last point, in x order.
    qsizetype position = first;
    while ( position < last ) {
        const qsizetype columnFirst = position;
        const double column = std::floor(
            viewport.toPixel( points[ xOrder.pointIndex( position ) ].xValue, 0.0 ).x() );
        qsizetype lowest = position;
        qsizetype highest = position;
        ++position;
        while ( position < last ) {
            const auto& point = points[ xOrder.pointIndex( position ) ];
            if ( std::floor( viewport.toPixel( point.xValue, 0.0 ).x() ) != column ) {
                break;
            }
            if ( point.value < points[ xOrder.pointIndex( lowest ) ].value ) {
                lowest = position;
            }
            if ( point.value > points[ xOrder.pointIndex( highest ) ].value ) {
                highest = position;
            }
            ++position;
        }
        const qsizetype columnLast = position - 1;

        std::array<qsizetype, 4> kept = { columnFirst, lowest, highest, columnLast };
        std::sort( kept.begin(), kept.end() );
        const auto keptEnd = std::unique( kept.begin(), kept.end() );
        for ( auto it = kept.begin(); it != keptEnd; ++it ) {
            const qsizetype index = xOrder.pointIndex( *it );
            const auto& point = points[ index ];
            appendVertex( plot, viewport.toPixel( point.xValue, point.value ), index );
        }
    }

    // A dot closer than its radius to the last dot drawn would mostly cover it.
    for ( const QPointF& vertex : std::as_const( plot.line ) ) {
        if ( plot.dots.isEmpty()
             || QLineF( plot.dots.constLast(), vertex ).length() >= ChartDotRadius ) {
            plot.dots.append( vertex );
        }
    }
    return plot;
}

void drawChartPlot( QPainter& painter, const ChartPlot& plot, const QColor& color )
{
    if ( plot.line.isEmpty() ) {
        return;
    }

    painter.setPen( QPen( color, 1.5 ) );
    painter.setBrush( Qt::NoBrush );
    // Stroking one path fills its whole outline at once, which takes much
    // longer than stroking its segments a few at a time where the line
    // zig-zags up and down pixel columns. A reduced plot is stroked in pieces;
    // any other plot as one path, as the chart always drew it.
    const qsizetype last = plot.line.size() - 1;
    const qsizetype chunk = plot.reduced ? ReducedLineChunk : std::max<qsizetype>( last, 1 );
    qsizetype start = 0;
    do {
        const qsizetype end = std::min( start + chunk, last );
        QPainterPath path;
        path.moveTo( plot.line[ start ] );
        for ( qsizetype i = start + 1; i <= end; ++i ) {
            path.lineTo( plot.line[ i ] );
        }
        painter.drawPath( path );
        start = end;
    } while ( start < last );

    painter.setBrush( color );
    painter.setPen( Qt::NoPen );
    for ( const QPointF& dot : plot.dots ) {
        painter.drawEllipse( dot, ChartDotRadius, ChartDotRadius );
    }
}
