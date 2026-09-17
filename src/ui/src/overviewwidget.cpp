/*
 * Copyright (C) 2011, 2012, 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements OverviewWidget.  This class is responsable for
// managing and painting the matches overview widget.

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

#include "log.h"

#include "overviewwidget.h"

#include "decorationsetup.h"

#include "overview.h"

#define HIGHLIGHT_XPM_WIDTH 27
#define HIGHLIGHT_XPM_HEIGHT 9

#define S( x ) #x
#define SX( x ) S( x )

// width height colours char/pixel
// Colours
#define HIGHLIGHT_XPM_LEAD_LINE                                                                    \
    SX( HIGHLIGHT_XPM_WIDTH )                                                                      \
    " " SX( HIGHLIGHT_XPM_HEIGHT ) " 2 1", "  s mask c none", "x c #572F80"

const char* const highlight_xpm[][ 14 ] = {
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        "                           ",
        "                           ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xx                 xx   ",
        "   xx                 xx   ",
        "   xx                 xx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "                           ",
        "                           ",
    },
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        "                           ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxx                 xxx  ",
        "  xxx                 xxx  ",
        "  xxx                 xxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "                           ",
    },
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxx                 xxxx ",
        " xxxx                 xxxx ",
        " xxxx                 xxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
        " xxxxxxxxxxxxxxxxxxxxxxxxx ",
    },
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        "                           ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxx                 xxx  ",
        "  xxx                 xxx  ",
        "  xxx                 xxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "  xxxxxxxxxxxxxxxxxxxxxxx  ",
        "                           ",
    },
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        "                           ",
        "                           ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xx                 xx   ",
        "   xx                 xx   ",
        "   xx                 xx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "   xxxxxxxxxxxxxxxxxxxxx   ",
        "                           ",
        "                           ",
    },
    {
        HIGHLIGHT_XPM_LEAD_LINE,
        "                           ",
        "                           ",
        "                           ",
        "    xxxxxxxxxxxxxxxxxxx    ",
        "    x                 x    ",
        "    x                 x    ",
        "    x                 x    ",
        "    xxxxxxxxxxxxxxxxxxx    ",
        "                           ",
        "                           ",
        "                           ",
    },
};

namespace {

// The WCAG relative luminance of color.
double relativeLuminance( const QColor& color )
{
    const auto linear = []( double channel ) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * linear( static_cast<double>( color.redF() ) )
           + 0.7152 * linear( static_cast<double>( color.greenF() ) )
           + 0.0722 * linear( static_cast<double>( color.blueF() ) );
}

// The WCAG contrast ratio between two colors, from 1 to 21.
double contrastRatio( const QColor& first, const QColor& second )
{
    const auto firstLuminance = relativeLuminance( first );
    const auto secondLuminance = relativeLuminance( second );
    const auto darker = std::min( firstLuminance, secondLuminance );
    const auto lighter = std::max( firstLuminance, secondLuminance );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

// color mixed into background by amount: 0 is background, 255 is color.
QColor mixed( const QColor& color, const QColor& background, int amount )
{
    const auto channel = [ amount ]( int from, int to ) {
        return ( from * ( 255 - amount ) + to * amount + 127 ) / 255;
    };
    return QColor( channel( background.red(), color.red() ),
                   channel( background.green(), color.green() ),
                   channel( background.blue(), color.blue() ) );
}

} // namespace

QColor OverviewWidget::lineColor( const QColor& statusColor, const QColor& background, int weight )
{
    const auto lastWeight = Overview::WeightedLine::WEIGHT_STEPS - 1;
    weight = std::clamp( weight, 0, lastWeight );

    // The least mix from which on every mix up to the Match or Mark color
    // itself stands out against the background.
    int leastAmount = 255;
    while ( leastAmount > 0
            && contrastRatio( mixed( statusColor, background, leastAmount - 1 ), background )
                   >= MinimumLineContrast ) {
        --leastAmount;
    }

    return mixed( statusColor, background,
                  leastAmount + ( 255 - leastAmount ) * weight / lastWeight );
}

OverviewWidget::OverviewWidget( QWidget* parent )
    : QWidget( parent )
    , highlightTimer_()
{
    setBackgroundRole( QPalette::Window );

    // We should be hidden by default (e.g. for the FilteredView)
    hide();
}

void OverviewWidget::paintEvent( QPaintEvent* /* paintEvent */ )
{
    // The same colors the Presentations show a Match and a Mark in, from the
    // one place that defines them.
    static const QColor match_color( LineStatusColors::match() );
    static const QColor mark_color( LineStatusColors::mark() );

    static const QPixmap highlight_pixmap[] = {
        QPixmap( highlight_xpm[ 0 ] ), QPixmap( highlight_xpm[ 1 ] ), QPixmap( highlight_xpm[ 2 ] ),
        QPixmap( highlight_xpm[ 3 ] ), QPixmap( highlight_xpm[ 4 ] ), QPixmap( highlight_xpm[ 5 ] ),
    };

    // We must be hidden until we have an Overview
    assert( overview_ != nullptr );

    overview_->updateView( static_cast<unsigned>( height() ) );

    {
        QPainter painter( this );

        painter.fillRect( painter.viewport(), painter.background() );

        // The line separating from the main view
        painter.setPen( palette().color( QPalette::Text ) );
        painter.drawLine( 0, 0, 0, height() );

        // The 'match' and 'mark' lines. A line standing for more Log Lines is
        // drawn stronger, and even the lightest stands out against the
        // background in every Theme (#255).
        const auto background = painter.background().color();
        const auto drawLines = [ & ]( const QColor& statusColor,
                                      const logsquirl::vector<Overview::WeightedLine>& lines ) {
            std::array<QColor, Overview::WeightedLine::WEIGHT_STEPS> colors;
            for ( int weight = 0; weight < Overview::WeightedLine::WEIGHT_STEPS; ++weight ) {
                colors[ static_cast<std::size_t>( weight ) ]
                    = lineColor( statusColor, background, weight );
            }
            for ( const auto& line : lines ) {
                painter.setPen( colors[ static_cast<std::size_t>( line.weight() ) ] );
                painter.drawLine( 1 + LINE_MARGIN, line.position(), width() - LINE_MARGIN - 1,
                                  line.position() );
            }
        };
        drawLines( match_color, *( overview_->getMatchLines() ) );
        drawLines( mark_color, *( overview_->getMarkLines() ) );

        // The 'view' lines
        painter.setPen( palette().color( QPalette::Text ) );
        std::pair<int, int> viewLines = overview_->getViewLines();
        painter.drawLine( 1, viewLines.first, width(), viewLines.first );
        painter.drawLine( 1, viewLines.second, width(), viewLines.second );

        // The highlight
        if ( highlightedLine_ ) {
            /*
            QPen highlight_pen( palette().color(QPalette::Text) );
            highlight_pen.setWidth( 4 - highlightedTTL_ );
            painter.setOpacity( 1 );
            painter.setPen( highlight_pen );
            painter.drawRect( 2, position - 2, width() - 2 - 2, 4 );
            */
            int position = overview_->yFromFileLine( *highlightedLine_ );
            int pixmapY = std::clamp( position - ( HIGHLIGHT_XPM_HEIGHT / 2 ), 0,
                                      height() - HIGHLIGHT_XPM_HEIGHT );
            painter.drawPixmap( ( width() - HIGHLIGHT_XPM_WIDTH ) / 2, pixmapY,
                                highlight_pixmap[ INITIAL_TTL_VALUE - highlightedTTL_ ] );
        }
    }
}

void OverviewWidget::mousePressEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->button() == Qt::LeftButton )
        handleMousePress( mouseEvent->pos().y() );
}

void OverviewWidget::mouseMoveEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->buttons().testFlag( Qt::LeftButton ) )
        handleMousePress( mouseEvent->pos().y() );
}

void OverviewWidget::handleMousePress( int position )
{
    const auto line = overview_->fileLineFromY( position );
    LOG_DEBUG << "OverviewWidget::handleMousePress y=" << position << " line=" << line;
    Q_EMIT lineClicked( line );
}

void OverviewWidget::highlightLine( LineNumber line )
{
    highlightTimer_.stop();

    highlightedLine_ = line;
    highlightedTTL_ = INITIAL_TTL_VALUE;

    update();
    highlightTimer_.start( STEP_DURATION_MS, this );
}

void OverviewWidget::removeHighlight()
{
    highlightTimer_.stop();

    highlightedLine_.reset();
    update();
}

void OverviewWidget::timerEvent( QTimerEvent* event )
{
    if ( event->timerId() == highlightTimer_.timerId() ) {
        LOG_DEBUG << "OverviewWidget::timerEvent";
        if ( highlightedTTL_ > 0 ) {
            --highlightedTTL_;
            update();
        }
        else {
            highlightTimer_.stop();
        }
    }
    else {
        QObject::timerEvent( event );
    }
}
