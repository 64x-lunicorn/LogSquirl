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

// Runner for the Open Log File's tests. It uses QCoreApplication on purpose:
// the Open Log File follows a Log File without any widget (#244). It
// registers no meta types either: the library registers the types its queued
// signals carry, so a host only opens a Log File (#394).

#include <QCoreApplication>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
