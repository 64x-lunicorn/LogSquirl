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

#include <functional>

#include <QObject>
#include <QPointer>

#include "processclock.h"

class QWidget;

namespace logsquirl::benchmark {

// Times every paint event one widget handles, from the moment the event
// reaches the widget to the moment its handler returns (#666). For a view of
// Log Lines that widget is the viewport of its scroll area: the paint of the
// Viewport, where the Text View and the Table View paint what is shown.
//
// Nothing in the widget changes: the probe is an event filter that hands the
// paint event on itself and takes the time around it.
class PaintProbe : public QObject {
public:
    // Called after each paint with the moments it started and ended.
    using Painted = std::function<void( Clock::time_point started, Clock::time_point ended )>;

    PaintProbe( QWidget* widget, Painted painted, QObject* parent = nullptr );
    ~PaintProbe() override;

    PaintProbe( const PaintProbe& ) = delete;
    PaintProbe& operator=( const PaintProbe& ) = delete;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    QPointer<QWidget> widget_;
    Painted painted_;
    bool painting_ = false;
};

} // namespace logsquirl::benchmark
