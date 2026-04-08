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

#include <QWidget>

#include "chartseries.h"

// Custom QPainter-based chart widget that renders line/scatter plots
// of extracted log data.  Supports zoom (mouse wheel), pan (middle-drag),
// click-to-navigate (left click selects the nearest point and emits
// lineSelected), and tooltip display on hover.
//
// X-axis = line number in the log file.
// Y-axis = extracted numeric value from the capture group.
class ChartWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ChartWidget( QWidget* parent = nullptr );

    // Set the full list of series definitions (with pre-populated points).
    void setSeriesList( const QVector<ChartSeriesDefinition>& series );

    // Reset zoom/pan to fit all data.
    void fitView();

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

    // Draw a single series as connected line segments with point markers.
    void drawSeries( QPainter& painter, const QRectF& area,
                     const ChartSeriesDefinition& series ) const;

    // Draw a tooltip near the hovered point.
    void drawTooltip( QPainter& painter ) const;

    // Find the nearest data point to a pixel position.
    // Returns {seriesIndex, pointIndex} or {-1, -1} if none close enough.
    std::pair<int, int> findNearestPoint( const QPointF& pixelPos, double maxDistPx = 12.0 ) const;

    // Current view bounds in data space.
    double xMin_ = 0.0;
    double xMax_ = 100.0;
    double yMin_ = 0.0;
    double yMax_ = 100.0;

    // All series to render.
    QVector<ChartSeriesDefinition> series_;

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

    // Axis margin constants.
    static constexpr int LeftMargin = 60;
    static constexpr int BottomMargin = 30;
    static constexpr int TopMargin = 10;
    static constexpr int RightMargin = 10;
};
