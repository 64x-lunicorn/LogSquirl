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

// SPDX-License-Identifier: GPL-3.0-or-later

#include "gutter_widget.hpp"

#include <QPainter>
#include <QPen>

#include "crawlerwidget.h"

GutterWidget::GutterWidget( QWidget* parent )
    : QWidget( parent )
{
    setFixedWidth( 24 );
    setMinimumHeight( 0 );
}

void GutterWidget::setAnchors( const AnchorSet* anchors )
{
    anchors_ = anchors;
    update();
}

void GutterWidget::setPanes( CrawlerWidget* left, CrawlerWidget* right )
{
    leftPane_ = left;
    rightPane_ = right;
}

void GutterWidget::refresh()
{
    update();
}

void GutterWidget::paintEvent( QPaintEvent* /*event*/ )
{
    if ( !anchors_ || anchors_->empty() || !leftPane_ || !rightPane_ ) {
        return;
    }

    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );

    QPen pen( palette().color( QPalette::Highlight ), 1.5 );
    painter.setPen( pen );

    const auto leftTop = static_cast<int64_t>( leftPane_->getTopLine().get() );
    const auto rightTop = static_cast<int64_t>( rightPane_->getTopLine().get() );

    // Approximate line height from the widget height (rough estimate).
    // We use the gutter's height and assume about 20 pixels per line.
    const auto gutterH = height();
    constexpr int lineHeight = 20;

    for ( std::size_t i = 0; i < anchors_->size(); ++i ) {
        const auto& anchor = anchors_->at( i );

        // Y positions relative to the visible viewport in each pane.
        const int leftY = static_cast<int>( anchor.lineA - leftTop ) * lineHeight;
        const int rightY = static_cast<int>( anchor.lineB - rightTop ) * lineHeight;

        // Only draw if at least one end is within the visible area.
        if ( leftY < -lineHeight && rightY < -lineHeight ) {
            continue;
        }
        if ( leftY > gutterH + lineHeight && rightY > gutterH + lineHeight ) {
            continue;
        }

        // Clamp Y to gutter bounds for drawing.
        const int clampedLeftY = std::clamp( leftY, 0, gutterH );
        const int clampedRightY = std::clamp( rightY, 0, gutterH );

        painter.drawLine( 0, clampedLeftY, width(), clampedRightY );
    }
}

QSize GutterWidget::sizeHint() const
{
    return { 24, 100 };
}

QSize GutterWidget::minimumSizeHint() const
{
    return { 24, 0 };
}
