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

#ifndef SHOWN_WIDGET_H
#define SHOWN_WIDGET_H

// Showing a widget in a test (#754): a test waits until a widget it shows is
// exposed; .github/scripts/check-shown-widget-wait.py holds the full rule.

#include <catch2/catch_test_macros.hpp>

#include <QTest>
#include <QWidget>

// Shows widget and waits until its window is exposed; fails the test if that
// does not happen within Qt Test's five seconds. A child widget waits for its
// window, which must be shown already.
inline void showUntilExposed( QWidget& widget )
{
    INFO( "the window of the shown " << widget.metaObject()->className() << " is not exposed" );
    widget.show();
    REQUIRE( QTest::qWaitForWindowExposed( &widget ) );
}

#endif // SHOWN_WIDGET_H
