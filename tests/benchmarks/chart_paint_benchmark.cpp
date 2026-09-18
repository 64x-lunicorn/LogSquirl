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

// Painting and hovering a chart of millions of points (#299): a chart widget
// of 800 x 400 px with one series of a point per Log Line, painted into an
// image, fitted to all points and zoomed in to 1 % of them, and the mouse
// moving over it. LOGSQUIRL_BENCHMARK_CHART_POINTS sets another number of
// points.
//
// Uses only what the chart widget offered before #299 -- setSeriesList(),
// render() and the mouse events it handles -- so the same file measures both
// sides of an A/B comparison. See tests/benchmarks/README.md.

#include "chartseries.h"
#include "chartwidget.h"
#include "persistentinfo.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cstdint>
#include <random>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

constexpr int ChartWidth = 800;
constexpr int ChartHeight = 400;

// A duration extracted from every Log Line: x the line number, the value
// between 0 and 1000.
ChartSeriesDefinition durationSeries( int count )
{
    ChartSeriesDefinition series;
    series.id = "duration";
    series.name = "Duration";
    series.color = QColor( "#2196F3" );
    series.points.reserve( count );
    std::mt19937 random( 299 );
    std::uniform_real_distribution<double> values( 0.0, 1000.0 );
    for ( int i = 0; i < count; ++i ) {
        const auto line = static_cast<uint64_t>( i );
        series.points.append(
            ChartPoint{ LineNumber{ line }, static_cast<double>( i ), values( random ), {} } );
    }
    return series;
}

class Chart {
public:
    explicit Chart( int count )
        : image_( ChartWidth, ChartHeight, QImage::Format_ARGB32_Premultiplied )
    {
        widget_.resize( ChartWidth, ChartHeight );
        widget_.setSeriesList( { durationSeries( count ) }, ChartWidget::Change::Series );
    }

    // The middle of the plot area.
    QPointF middle() const
    {
        return { ( 60.0 + ChartWidth - 10.0 ) / 2.0, ( 10.0 + ChartHeight - 30.0 ) / 2.0 };
    }

    void paint()
    {
        widget_.render( &image_ );
    }

    // Zoomed in around the middle of the plot area: 21 steps of 0.8 leave
    // about 1 % of the x range in view.
    void zoomToOnePercent()
    {
        for ( int i = 0; i < 21; ++i ) {
            QWheelEvent wheel( middle(), widget_.mapToGlobal( middle() ), QPoint(),
                               QPoint( 0, 120 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false );
            QCoreApplication::sendEvent( &widget_, &wheel );
        }
    }

    // Drags the view dx pixels with the right mouse button.
    void pan( double dx )
    {
        send( QEvent::MouseButtonPress, middle(), Qt::RightButton, Qt::RightButton );
        send( QEvent::MouseMove, middle() + QPointF( dx, 0.0 ), Qt::NoButton, Qt::RightButton );
        send( QEvent::MouseButtonRelease, middle() + QPointF( dx, 0.0 ), Qt::RightButton,
              Qt::NoButton );
    }

    void hover( QPointF pos )
    {
        send( QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton );
    }

private:
    void send( QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons )
    {
        QMouseEvent event( type, pos, widget_.mapToGlobal( pos ), button, buttons, Qt::NoModifier );
        QCoreApplication::sendEvent( &widget_, &event );
    }

    ChartWidget widget_;
    QImage image_;
};

} // namespace

TEST_CASE( "Painting and hovering a chart of millions of points", "[chart-paint-benchmark]" )
{
    // Before #299 the chart stroked one path through every point, which takes
    // seconds for 10,000 points and far longer for millions: the millions are
    // measured only where the chart plots the visible range (chartplot.h).
    const int count = qEnvironmentVariableIsEmpty( "LOGSQUIRL_BENCHMARK_CHART_POINTS" )
#if __has_include( "chartplot.h" )
                          ? GENERATE( 10'000, 1'000'000, 5'000'000 )
#else
                          ? GENERATE( 10'000 )
#endif
                          : qEnvironmentVariableIntValue( "LOGSQUIRL_BENCHMARK_CHART_POINTS" );
    const auto points = count % 1'000'000 == 0 ? std::to_string( count / 1'000'000 ) + "M"
                        : count % 1'000 == 0   ? std::to_string( count / 1'000 ) + "k"
                                               : std::to_string( count );

    Chart chart( count );
    chart.paint();

    BENCHMARK( "paint, all " + points )
    {
        chart.paint();
    };

    // A different view each time, so nothing plotted before can be reused.
    double direction = 1.0;
    BENCHMARK( "pan 2 px, paint, all " + points )
    {
        chart.pan( 2.0 * direction );
        direction = -direction;
        chart.paint();
    };

    chart.zoomToOnePercent();
    chart.paint();

    BENCHMARK( "pan 2 px, paint, 1 % of " + points )
    {
        chart.pan( 2.0 * direction );
        direction = -direction;
        chart.paint();
    };

    // The mouse moving between two positions 3 px apart, with and without the
    // repaint it asks for.
    const QPointF left = chart.middle();
    const QPointF right = left + QPointF( 3.0, 0.0 );
    bool atLeft = false;
    BENCHMARK( "hover, paint, 1 % of " + points )
    {
        chart.hover( atLeft ? right : left );
        atLeft = !atLeft;
        chart.paint();
    };

    BENCHMARK( "hover, 1 % of " + points )
    {
        chart.hover( atLeft ? right : left );
        atLeft = !atLeft;
    };
}

int main( int argc, char* argv[] )
{
    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
