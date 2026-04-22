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

#pragma once

#include <QWidget>

#include "anchor.hpp"

class CrawlerWidget;

/// Narrow column drawn between the two CrawlerWidgets in the diff view.
/// Draws horizontal lines connecting anchor pairs across the two panes.
class GutterWidget : public QWidget {
    Q_OBJECT

  public:
    explicit GutterWidget( QWidget* parent = nullptr );

    /// Set the anchor set to visualize.
    void setAnchors( const AnchorSet* anchors );

    /// Set the two CrawlerWidgets so the gutter can query their scroll state.
    void setPanes( CrawlerWidget* left, CrawlerWidget* right );

    /// Schedule a repaint (call after anchor or scroll changes).
    void refresh();

  protected:
    void paintEvent( QPaintEvent* event ) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  private:
    const AnchorSet* anchors_ = nullptr;
    CrawlerWidget* leftPane_ = nullptr;
    CrawlerWidget* rightPane_ = nullptr;
};
