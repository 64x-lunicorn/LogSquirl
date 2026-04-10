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

#include "chartwidget.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

ChartWidget::ChartWidget( QWidget* parent )
    : QWidget( parent )
{
    setMouseTracking( true );
    setMinimumHeight( 120 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setFocusPolicy( Qt::ClickFocus );
}

void ChartWidget::setSeriesList( const QVector<ChartSeriesDefinition>& series )
{
    series_ = series;

    // Detect if any visible series uses timestamp X-axis.
    xAxisIsTimestamp_ = false;
    for ( const auto& s : series_ ) {
        if ( s.visible && s.isTimestampXAxis() ) {
            xAxisIsTimestamp_ = true;
            break;
        }
    }

    fitView();
}

void ChartWidget::fitView()
{
    // Compute data bounds across all visible series.
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    bool hasData = false;
    for ( const auto& s : series_ ) {
        if ( !s.visible ) {
            continue;
        }
        for ( const auto& pt : s.points ) {
            const double x = pt.xValue;
            minX = std::min( minX, x );
            maxX = std::max( maxX, x );
            minY = std::min( minY, pt.value );
            maxY = std::max( maxY, pt.value );
            hasData = true;
        }
    }

    if ( !hasData ) {
        xMin_ = 0.0;
        xMax_ = 100.0;
        yMin_ = 0.0;
        yMax_ = 100.0;
    }
    else {
        // Add 5 % padding around the data range.
        const double xPad = std::max( ( maxX - minX ) * 0.05, 1.0 );
        const double yPad = std::max( ( maxY - minY ) * 0.05, 1.0 );
        xMin_ = minX - xPad;
        xMax_ = maxX + xPad;
        yMin_ = minY - yPad;
        yMax_ = maxY + yPad;
    }

    hoveredSeries_ = -1;
    hoveredPoint_ = -1;
    update();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ChartWidget::paintEvent( QPaintEvent* /*event*/ )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing, true );

    // Background.
    painter.fillRect( rect(), palette().color( QPalette::Base ) );

    const QRectF area = plotArea();
    if ( area.width() <= 0 || area.height() <= 0 ) {
        return;
    }

    // Clip to the plot area for series drawing.
    painter.save();
    painter.setClipRect( area );

    for ( const auto& s : series_ ) {
        if ( s.visible && !s.points.isEmpty() ) {
            drawSeries( painter, area, s );
        }
    }

    painter.restore();

    drawAxes( painter, area );

    if ( hoveredSeries_ >= 0 && hoveredPoint_ >= 0 ) {
        drawTooltip( painter );
    }
}

QRectF ChartWidget::plotArea() const
{
    return QRectF( LeftMargin, TopMargin, width() - LeftMargin - RightMargin,
                   height() - TopMargin - BottomMargin );
}

QPointF ChartWidget::dataToPixel( double lineNum, double value ) const
{
    const QRectF area = plotArea();
    const double xRatio = ( lineNum - xMin_ ) / ( xMax_ - xMin_ );
    const double yRatio = ( value - yMin_ ) / ( yMax_ - yMin_ );
    return { area.left() + xRatio * area.width(),
             area.bottom() - yRatio * area.height() };
}

QPointF ChartWidget::pixelToData( const QPointF& pixel ) const
{
    const QRectF area = plotArea();
    const double xRatio = ( pixel.x() - area.left() ) / area.width();
    const double yRatio = ( area.bottom() - pixel.y() ) / area.height();
    return { xMin_ + xRatio * ( xMax_ - xMin_ ), yMin_ + yRatio * ( yMax_ - yMin_ ) };
}

void ChartWidget::drawAxes( QPainter& painter, const QRectF& area ) const
{
    QPen axisPen( palette().color( QPalette::WindowText ), 1.0 );
    painter.setPen( axisPen );

    // Axes lines.
    painter.drawLine( QPointF( area.left(), area.top() ),
                      QPointF( area.left(), area.bottom() ) );
    painter.drawLine( QPointF( area.left(), area.bottom() ),
                      QPointF( area.right(), area.bottom() ) );

    // Grid lines and labels — aim for ~5 ticks per axis.
    const QFont smallFont = painter.font();
    painter.setFont( smallFont );

    QPen gridPen( palette().color( QPalette::Mid ), 0.5, Qt::DotLine );

    auto niceStep = []( double range, int targetTicks ) -> double {
        const double rough = range / targetTicks;
        const double mag = std::pow( 10.0, std::floor( std::log10( rough ) ) );
        const double frac = rough / mag;
        if ( frac <= 1.5 )
            return mag;
        if ( frac <= 3.5 )
            return 2.0 * mag;
        if ( frac <= 7.5 )
            return 5.0 * mag;
        return 10.0 * mag;
    };

    const int targetTicks = 5;

    // X-axis ticks (line numbers).
    {
        const double step = niceStep( xMax_ - xMin_, targetTicks );
        const double first = std::ceil( xMin_ / step ) * step;
        for ( double v = first; v <= xMax_; v += step ) {
            const QPointF p = dataToPixel( v, yMin_ );
            painter.setPen( gridPen );
            painter.drawLine( QPointF( p.x(), area.top() ), QPointF( p.x(), area.bottom() ) );
            painter.setPen( axisPen );
            painter.drawText( QRectF( p.x() - 30, area.bottom() + 2, 60, BottomMargin - 2 ),
                              Qt::AlignHCenter | Qt::AlignTop,
                              xAxisIsTimestamp_
                                  ? QDateTime::fromMSecsSinceEpoch(
                                        static_cast<qint64>( v ) )
                                        .toString( "HH:mm:ss" )
                                  : QString::number( static_cast<qint64>( v ) ) );
        }
    }

    // Y-axis ticks (values).
    {
        const double step = niceStep( yMax_ - yMin_, targetTicks );
        const double first = std::ceil( yMin_ / step ) * step;
        for ( double v = first; v <= yMax_; v += step ) {
            const QPointF p = dataToPixel( xMin_, v );
            painter.setPen( gridPen );
            painter.drawLine( QPointF( area.left(), p.y() ), QPointF( area.right(), p.y() ) );
            painter.setPen( axisPen );
            painter.drawText( QRectF( 2, p.y() - 8, LeftMargin - 4, 16 ),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number( v, 'g', 4 ) );
        }
    }
}

void ChartWidget::drawSeries( QPainter& painter, const QRectF& /*area*/,
                               const ChartSeriesDefinition& series ) const
{
    if ( series.points.isEmpty() ) {
        return;
    }

    QPen linePen( series.color, 1.5 );
    painter.setPen( linePen );
    painter.setBrush( Qt::NoBrush );

    // Draw connected line segments.
    QPainterPath path;
    bool first = true;
    for ( const auto& pt : series.points ) {
        const QPointF px = dataToPixel( pt.xValue, pt.value );
        if ( first ) {
            path.moveTo( px );
            first = false;
        }
        else {
            path.lineTo( px );
        }
    }
    painter.drawPath( path );

    // Draw small dot markers.
    painter.setBrush( series.color );
    painter.setPen( Qt::NoPen );
    constexpr double dotRadius = 3.0;
    for ( const auto& pt : series.points ) {
        const QPointF px = dataToPixel( pt.xValue, pt.value );
        painter.drawEllipse( px, dotRadius, dotRadius );
    }
}

void ChartWidget::drawTooltip( QPainter& painter ) const
{
    if ( hoveredSeries_ < 0 || hoveredSeries_ >= series_.size() ) {
        return;
    }
    const auto& s = series_[ hoveredSeries_ ];
    if ( hoveredPoint_ < 0 || hoveredPoint_ >= s.points.size() ) {
        return;
    }

    const auto& pt = s.points[ hoveredPoint_ ];
    const QPointF px = dataToPixel( pt.xValue, pt.value );

    const QString xText = pt.xLabel.isEmpty()
                              ? QString( "Line %1" ).arg( pt.line.get() + 1 )
                              : pt.xLabel;
    const QString text
        = QString( "%1\n%2: %3" ).arg( s.name, xText ).arg( pt.value );

    QFont tooltipFont = painter.font();
    tooltipFont.setPointSize( tooltipFont.pointSize() - 1 );
    painter.setFont( tooltipFont );

    const QRectF textRect = painter.fontMetrics().boundingRect(
        QRect( 0, 0, 200, 200 ), Qt::AlignLeft | Qt::TextWordWrap, text );
    QRectF bgRect = textRect.adjusted( -4, -2, 4, 2 );
    bgRect.moveTopLeft( QPointF( px.x() + 10, px.y() - bgRect.height() - 5 ) );

    // Keep tooltip within widget bounds.
    if ( bgRect.right() > width() - 4 ) {
        bgRect.moveRight( px.x() - 10 );
    }
    if ( bgRect.top() < 4 ) {
        bgRect.moveTop( px.y() + 10 );
    }

    painter.setPen( Qt::NoPen );
    painter.setBrush( QColor( 255, 255, 225, 230 ) );
    painter.drawRoundedRect( bgRect, 3, 3 );

    painter.setPen( Qt::black );
    painter.drawText( bgRect.adjusted( 4, 2, -4, -2 ), Qt::AlignLeft | Qt::TextWordWrap, text );
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

std::pair<int, int> ChartWidget::findNearestPoint( const QPointF& pixelPos,
                                                    double maxDistPx ) const
{
    int bestSeries = -1;
    int bestPoint = -1;
    double bestDist = maxDistPx;

    for ( int si = 0; si < series_.size(); ++si ) {
        const auto& s = series_[ si ];
        if ( !s.visible ) {
            continue;
        }
        for ( int pi = 0; pi < s.points.size(); ++pi ) {
            const auto& pt = s.points[ pi ];
            const QPointF px = dataToPixel( pt.xValue, pt.value );
            const double dist = QLineF( pixelPos, px ).length();
            if ( dist < bestDist ) {
                bestDist = dist;
                bestSeries = si;
                bestPoint = pi;
            }
        }
    }

    return { bestSeries, bestPoint };
}

void ChartWidget::wheelEvent( QWheelEvent* event )
{
    const double factor = ( event->angleDelta().y() > 0 ) ? 0.8 : 1.25;

    // Zoom around the cursor position in data space.
    const QPointF dataPos = pixelToData( event->position() );

    xMin_ = dataPos.x() - ( dataPos.x() - xMin_ ) * factor;
    xMax_ = dataPos.x() + ( xMax_ - dataPos.x() ) * factor;
    yMin_ = dataPos.y() - ( dataPos.y() - yMin_ ) * factor;
    yMax_ = dataPos.y() + ( yMax_ - dataPos.y() ) * factor;

    update();
    event->accept();
}

void ChartWidget::mousePressEvent( QMouseEvent* event )
{
    if ( event->button() == Qt::MiddleButton || event->button() == Qt::RightButton ) {
        panning_ = true;
        panStart_ = event->position();
        panXMin_ = xMin_;
        panYMin_ = yMin_;
        panXMax_ = xMax_;
        panYMax_ = yMax_;
        setCursor( Qt::ClosedHandCursor );
        event->accept();
    }
    else if ( event->button() == Qt::LeftButton ) {
        const auto [si, pi] = findNearestPoint( event->position() );
        if ( si >= 0 && pi >= 0 ) {
            Q_EMIT lineSelected( series_[ si ].points[ pi ].line );
        }
        event->accept();
    }
}

void ChartWidget::mouseMoveEvent( QMouseEvent* event )
{
    if ( panning_ ) {
        const QPointF delta = event->position() - panStart_;
        const QRectF area = plotArea();
        const double dxData = -delta.x() / area.width() * ( panXMax_ - panXMin_ );
        const double dyData = delta.y() / area.height() * ( panYMax_ - panYMin_ );
        xMin_ = panXMin_ + dxData;
        xMax_ = panXMax_ + dxData;
        yMin_ = panYMin_ + dyData;
        yMax_ = panYMax_ + dyData;
        update();
    }
    else {
        const auto [si, pi] = findNearestPoint( event->position() );
        if ( si != hoveredSeries_ || pi != hoveredPoint_ ) {
            hoveredSeries_ = si;
            hoveredPoint_ = pi;
            hoverPos_ = event->pos();
            update();
        }
        setCursor( ( si >= 0 ) ? Qt::PointingHandCursor : Qt::ArrowCursor );
    }
    event->accept();
}

void ChartWidget::mouseReleaseEvent( QMouseEvent* event )
{
    if ( panning_ ) {
        panning_ = false;
        setCursor( Qt::ArrowCursor );
        event->accept();
    }
}

void ChartWidget::resizeEvent( QResizeEvent* event )
{
    QWidget::resizeEvent( event );
    update();
}
