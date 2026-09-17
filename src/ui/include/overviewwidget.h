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

#ifndef OVERVIEWWIDGET_H
#define OVERVIEWWIDGET_H

#include <QBasicTimer>
#include <QColor>
#include <QWidget>

#include "linetypes.h"

class Overview;

class OverviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit OverviewWidget( QWidget* parent = nullptr );

    // Associate the widget with an Overview object.
    void setOverview( Overview* overview )
    {
        overview_ = overview;
    }

    // The contrast every Match and Mark line reaches against the overview
    // background, as long as the Match or Mark color reaches it by itself.
    static constexpr double MinimumLineContrast = 3.0;

    // The color a Match or Mark line of the given weight (0 to
    // Overview::WeightedLine::WEIGHT_STEPS - 1) is drawn in over background.
    // A heavier line stands for more Log Lines: the lightest is statusColor
    // mixed into background just as far as MinimumLineContrast needs, the
    // heaviest is statusColor itself. A statusColor that does not reach
    // MinimumLineContrast by itself is drawn unmixed at every weight.
    static QColor lineColor( const QColor& statusColor, const QColor& background, int weight );

public Q_SLOTS:
    // Sent when a match at the line passed must be highlighted in
    // the overview
    void highlightLine( LineNumber line );
    void removeHighlight();

protected:
    void paintEvent( QPaintEvent* paintEvent ) override;
    void mousePressEvent( QMouseEvent* mouseEvent ) override;
    void mouseMoveEvent( QMouseEvent* mouseEvent ) override;
    void timerEvent( QTimerEvent* event ) override;

Q_SIGNALS:
    // Sent when the user click on a line in the Overview.
    void lineClicked( LineNumber line );

private:
    // Constants
    static constexpr int LINE_MARGIN = 4;
    static constexpr int STEP_DURATION_MS = 30;
    static constexpr int INITIAL_TTL_VALUE = 5;

    Overview* overview_ = nullptr;

    // Highlight:
    // Which line is higlighted
    OptionalLineNumber highlightedLine_;
    // Number of step until the highlight become static
    int highlightedTTL_ = 0;

    QBasicTimer highlightTimer_;
    // Paints again when the Overview put off a recompute while a Search runs.
    QBasicTimer recomputeTimer_;

    void handleMousePress( int position );
};

#endif
