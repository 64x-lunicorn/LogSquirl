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

#pragma once

#include <vector>

#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <QtGlobal>

#include "chartseries.h"

class QColor;
class QPainter;

// The part of the data space a chart shows, and the plot area of the widget it
// shows it in.
struct ChartViewport {
    QRectF area;
    double xMin = 0.0;
    double xMax = 100.0;
    double yMin = 0.0;
    double yMax = 100.0;

    // The pixel a point in data space is drawn at.
    QPointF toPixel( double x, double y ) const;

    bool operator==( const ChartViewport& ) const = default;
};

// The points of a series in ascending x order, built once per change of its
// points. Points extracted with a line number X-axis are in that order already;
// a timestamp or numeric X-axis may be out of order.
class ChartSeriesXOrder {
public:
    ChartSeriesXOrder() = default;
    explicit ChartSeriesXOrder( const QVector<ChartPoint>& points );

    qsizetype size() const
    {
        return size_;
    }

    // The index into the points of the point at this position in x order.
    qsizetype pointIndex( qsizetype position ) const
    {
        return order_.empty() ? position : order_[ static_cast<size_t>( position ) ];
    }

private:
    qsizetype size_ = 0;
    // Empty when the points are in x order.
    std::vector<qsizetype> order_;
};

// What a chart draws of one series: its line and dots in pixels.
struct ChartPlot {
    // Connected in x order. The line's vertices are the points drawn.
    QPolygonF line;
    // The index into the series' points of each vertex of the line.
    std::vector<qsizetype> pointIndexes;
    // Where a dot is drawn.
    QPolygonF dots;
    // Whether pixel columns were reduced to a few points each.
    bool reduced = false;
};

// The radius of the dot drawn on a point.
constexpr double ChartDotRadius = 3.0;

// Plots the points of a series in the viewport: only the points in its x range,
// and the nearest point outside it on either side, so the line leaves the plot
// area. When there are more of them than pixel columns in the plot area, each
// pixel column keeps its first, lowest, highest and last point, and a dot that
// would mostly cover the dot before it is left out. Otherwise every point is
// kept. Takes O(log n) for the points outside the x range.
ChartPlot plotChartSeries( const QVector<ChartPoint>& points, const ChartSeriesXOrder& xOrder,
                           const ChartViewport& viewport );

// Draws a plot with the painter's clipping and render hints.
void drawChartPlot( QPainter& painter, const ChartPlot& plot, const QColor& color );

// The index of the x value nearest to x among count x values in ascending
// order, read through xAt( index ); the lower index on a tie, -1 when count is
// 0. Reads O(log count) x values.
template <typename XAt>
qsizetype nearestPointIndex( qsizetype count, XAt&& xAt, double x )
{
    if ( count <= 0 ) {
        return -1;
    }
    // The first index whose x value is not below x.
    qsizetype low = 0;
    qsizetype high = count;
    while ( low < high ) {
        const qsizetype middle = low + ( high - low ) / 2;
        if ( xAt( middle ) < x ) {
            low = middle + 1;
        }
        else {
            high = middle;
        }
    }
    if ( low == count ) {
        return count - 1;
    }
    if ( low == 0 ) {
        return 0;
    }
    return ( x - xAt( low - 1 ) <= xAt( low ) - x ) ? low - 1 : low;
}
