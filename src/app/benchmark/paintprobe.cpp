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

#include "paintprobe.h"

#include <utility>

#include <QCoreApplication>
#include <QEvent>
#include <QWidget>

namespace logsquirl::benchmark {

PaintProbe::PaintProbe( QWidget* widget, Painted painted, QObject* parent )
    : QObject( parent )
    , widget_( widget )
    , painted_( std::move( painted ) )
{
    widget_->installEventFilter( this );
}

PaintProbe::~PaintProbe()
{
    if ( widget_ ) {
        widget_->removeEventFilter( this );
    }
}

bool PaintProbe::eventFilter( QObject* watched, QEvent* event )
{
    if ( painting_ || watched != widget_ || event->type() != QEvent::Paint ) {
        return false;
    }

    // The filter installed last runs first, so this one runs before the
    // widget's own filters -- the scroll area's, which paints the viewport,
    // among them. Handing the event on from here runs all of them and the
    // widget's handler, inside the paint the widget is in; it is then not
    // handed on a second time.
    painting_ = true;
    const auto started = Clock::now();
    QCoreApplication::sendEvent( watched, event );
    const auto ended = Clock::now();
    painting_ = false;

    painted_( started, ended );
    return true;
}

} // namespace logsquirl::benchmark
