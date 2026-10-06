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

#ifndef PLACED_CURSOR_H
#define PLACED_CURSOR_H

// Placing the mouse cursor in a test (#753): every test that needs the cursor
// at a point places it through placeCursorOrSkip(), never QCursor::setPos().

#include <catch2/catch_test_macros.hpp>

#include <QCursor>
#include <QPoint>

// Places the mouse cursor at globalPos, in screen coordinates. A platform that
// does not let the test place it (a Mac without the permission to) leaves it
// elsewhere; the test then cannot run, so it is skipped, not failed. A skip in
// a section ends only that run of the test case; its other sections still run.
inline void placeCursorOrSkip( const QPoint& globalPos )
{
    QCursor::setPos( globalPos );
    if ( QCursor::pos() != globalPos ) {
        SKIP( "This platform does not let the test place the cursor" );
    }
}

#endif // PLACED_CURSOR_H
