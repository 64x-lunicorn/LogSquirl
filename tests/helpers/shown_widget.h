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

// Showing a widget in a test (#754). A widget that is shown is not painted and
// laid out for its window until that window is exposed, which happens later
// and on some platforms only after a round trip to the window system. A test
// that measures the painting or the geometry right after show() then reads
// what Qt has deferred, and fails now and then (#750). So a test shows its
// widget with showUntilExposed(), and the CI check
// .github/scripts/check-shown-widget-wait.py rejects a show() that no wait
// for exposure follows.

#include <catch2/catch_test_macros.hpp>

#include <QTest>
#include <QWidget>

// Shows widget and waits until its window is exposed; fails the test if that
// does not happen within timeoutMs. A child widget waits for its window, which
// must be shown already.
inline void showUntilExposed( QWidget& widget, int timeoutMs = 5000 )
{
    INFO( "the window of the shown " << widget.metaObject()->className() << " is not exposed after "
                                     << timeoutMs << " ms" );
    widget.show();
    REQUIRE( QTest::qWaitForWindowExposed( &widget, timeoutMs ) );
}

#endif // SHOWN_WIDGET_H
