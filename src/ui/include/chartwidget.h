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

#include <optional>
#include <vector>

#include <QWidget>

#include "chartplot.h"
#include "chartseries.h"

// Custom QPainter-based chart widget that renders line/scatter plots
// of extracted log data.  Supports zoom (mouse wheel), pan (middle-drag),
// click-to-navigate (left click selects the nearest point and emits
// lineSelected), and tooltip display on hover.
//
// Only the visible range of each series is drawn, reduced to a few points per
// pixel column (see chartplot.h). The plots are kept until the series or the
// view change, so hovering does not plot again, and the hovered point is found
// among them by binary search.
//
// While the Log File grows, the view is fitted to the data again on every
// update, so that the chart follows the Log File -- until the user zooms or
// pans. That view is then kept until the series change or the user asks for
// the view to be fitted.
//
// X-axis = line number in the log file.
// Y-axis = extracted numeric value from the capture group.
class ChartWidget : public QWidget {
    Q_OBJECT

public:
    // What changed about the series handed to setSeriesList().
    enum class Change {
        // The series themselves: one was added, edited or removed, or a preset
        // was loaded. The view is fitted to the data again and follows it
        // again, whatever the user zoomed or panned to before.
        Series,
        // Only their points, extracted from the Log Lines appended to a
        // growing Log File. A view the user zoomed or panned to is kept; a
        // view the user never touched keeps following the data.
        AppendedPoints,
    };

    explicit ChartWidget( QWidget* parent = nullptr );

    // Set the full list of series definitions (with pre-populated points).
    void setSeriesList( const QVector<ChartSeriesDefinition>& series, Change change );

    // Reset zoom/pan to fit all data, and follow the data again.
    void fitView();

    // The view in data space and the plot area it is drawn in.
    ChartViewport viewport() const;

Q_SIGNALS:
    // Emitted when the user clicks near a data point; the main view
    // should scroll to this line.
    void lineSelected( LineNumber line );

protected:
    void paintEvent( QPaintEvent* event ) override;
    void wheelEvent( QWheelEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;
    void resizeEvent( QResizeEvent* event ) override;

private:
    // Compute the plot area rectangle (excluding axis labels).
    QRectF plotArea() const;

    // Map data coordinates to widget pixel coordinates.
    QPointF dataToPixel( double lineNum, double value ) const;

    // Map widget pixel coordinates to data coordinates.
    QPointF pixelToData( const QPointF& pixel ) const;

    // Draw grid lines and axis labels.
    void drawAxes( QPainter& painter, const QRectF& area ) const;

    // The plot of every series in the current view, one per series (empty for
    // a hidden one); plotted again only when the series or the view changed.
    const std::vector<ChartPlot>& plots() const;

    // Draw a tooltip near the hovered point.
    void drawTooltip( QPainter& painter ) const;

    // Find the nearest plotted data point to a pixel position.
    // Returns {seriesIndex, pointIndex} or {-1, -1} if none close enough.
    std::pair<int, int> findNearestPoint( const QPointF& pixelPos, double maxDistPx = 12.0 ) const;

    // Whether the view is still the automatic one, fitted to the data, rather
    // than one the user zoomed or panned to. An automatic view is fitted again
    // on every update, so that it follows a growing Log File; the user's view
    // is kept until the series change or the user asks for it to be fitted.
    bool viewIsAutomatic_ = true;

    // Current view bounds in data space.
    double xMin_ = 0.0;
    double xMax_ = 100.0;
    double yMin_ = 0.0;
    double yMax_ = 100.0;

    // All series to render.
    QVector<ChartSeriesDefinition> series_;
    // The points of each series in x order.
    std::vector<ChartSeriesXOrder> xOrders_;

    // The plots of the series in plottedViewport_, if they are up to date.
    mutable std::vector<ChartPlot> plots_;
    mutable std::optional<ChartViewport> plottedViewport_;

    // Pan state.
    bool panning_ = false;
    QPointF panStart_;
    double panXMin_ = 0.0;
    double panYMin_ = 0.0;
    double panXMax_ = 0.0;
    double panYMax_ = 0.0;

    // Hover tooltip state.
    int hoveredSeries_ = -1;
    int hoveredPoint_ = -1;
    QPoint hoverPos_;

    // Whether any visible series uses timestamp X-axis.
    bool xAxisIsTimestamp_ = false;

    // Axis margin constants.
    static constexpr int LeftMargin = 60;
    static constexpr int BottomMargin = 30;
    static constexpr int TopMargin = 10;
    static constexpr int RightMargin = 10;
};
