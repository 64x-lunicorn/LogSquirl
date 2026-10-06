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

// The test helper that shows a widget and waits until it is exposed (#754).

#include <catch2/catch_test_macros.hpp>

#include <QWidget>
#include <QWindow>

#include "shown_widget.h"

SCENARIO( "A widget shown until exposed is exposed when the helper returns", "[shownwidget]" )
{
    QWidget widget;
    widget.resize( 120, 80 );

    showUntilExposed( widget );

    REQUIRE( widget.isVisible() );
    REQUIRE( widget.windowHandle() != nullptr );
    REQUIRE( widget.windowHandle()->isExposed() );
}
