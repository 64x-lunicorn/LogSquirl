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

// Runner for the update check's tests. It uses QCoreApplication on purpose:
// deciding an update offer needs no GUI (#306).

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include "persistentinfo.h"

// The version checker links the settings store. Its tests hand it settings of
// their own and never reach the store; should one ever do, it stays portable,
// beside the test binary, as in the other test runners (#389).
const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
