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

#ifndef LOGSQUIRL_FRONTTABBINDING_H
#define LOGSQUIRL_FRONTTABBINDING_H

#include <vector>

#include <QMetaObject>
#include <QPointer>

#include "crawlerwidget.h"

class MainWindow;

// What ties a window to the tab in front, and to it alone (#635, #946): the
// connections between the window and that tab's CrawlerWidget, its place in
// the QuickFind mux, and the window's actions as frontTabActions() has them
// for its state.
class FrontTabBinding {
public:
    explicit FrontTabBinding( MainWindow& window );
    ~FrontTabBinding();

    FrontTabBinding( const FrontTabBinding& ) = delete;
    FrontTabBinding& operator=( const FrontTabBinding& ) = delete;

    // Binds the tab now in front, none for one that holds no Log File, such
    // as the Welcome Dashboard: the connections to the tab bound before go,
    // the new ones are made, QuickFind searches the new tab, and the actions
    // show its state.
    void bind( CrawlerWidget* crawler );

    // Shows the state of the tab bound, as it is now, in the window's
    // actions: the one place they are written from the tab in front.
    void apply();

    CrawlerWidget* crawler() const;

private:
    void disconnectAll();

    MainWindow& window_;
    QPointer<CrawlerWidget> crawler_;
    std::vector<QMetaObject::Connection> connections_;
};

#endif
