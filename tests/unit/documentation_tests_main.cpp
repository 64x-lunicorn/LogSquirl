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

// Runner for the tests of the user guide against the application (#599). It
// needs a QGuiApplication: the default keys that follow the platform, such as
// the one for Reload, come from its platform theme.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QGuiApplication>

#include "persistentinfo.h"

// The shortcut defaults live in the settings library, which links the
// settings store. These tests never reach the store; should one ever do, it
// stays portable, beside the test binary, as in the other test runners (#389).
const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
    QGuiApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
