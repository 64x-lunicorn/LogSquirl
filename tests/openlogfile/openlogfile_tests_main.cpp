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
// the Open Log File follows a Log File without any widget (#244).

#include "linetypes.h"
#include "searchsession.h"

#include <QCoreApplication>
#include <QMetaType>

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );
    qRegisterMetaType<LineLength>( "LineLength" );
    qRegisterMetaType<SearchId>( "SearchId" );
    qRegisterMetaType<SearchSession::State>( "SearchSession::State" );

    return Catch::Session().run( argc, argv );
}
