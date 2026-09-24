/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include <logger.h>

#include "configuration.h"
#include "highlighterset.h"
#include "isolated_settings.h"
#include <persistentinfo.h>

const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
    // The test cases run beside a settings file of this process's own, so
    // that no test binary reads or writes the one in the build directory and
    // no case inherits what an earlier one left behind (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

    QApplication a( argc, argv );

    logging::enableLogging();

    Configuration::getSynced();
    HighlighterSetCollection::getSynced();

    return Catch::Session().run( argc, argv );
}
