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

// Runner for the text view scrolling tests. It uses QCoreApplication on
// purpose: scrolling knows no widget (#246). The elastic hook runs a timer.

#include <QCoreApplication>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
