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

#include <catch2/catch.hpp>

#include "applicationplugins.h"

#include <QCoreApplication>
#include <QObject>
#include <QTest>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

SCENARIO( "The Application Plugins load once, after the events already queued",
          "[applicationplugins][plugins]" )
{
    GIVEN( "Application Plugins whose loading is counted" )
    {
        int loads = 0;
        ApplicationPlugins plugins( [ &loads ]( PluginCatalog&, PluginHost& ) { ++loads; } );

        THEN( "nothing is loaded before loading is asked for" )
        {
            QCoreApplication::processEvents();
            REQUIRE( loads == 0 );
            REQUIRE_FALSE( plugins.isLoaded() );
        }

        WHEN( "loading is asked for" )
        {
            plugins.loadSoon();

            THEN( "the plugins are not loaded right away" )
            {
                REQUIRE( loads == 0 );
                REQUIRE_FALSE( plugins.isLoaded() );
            }

            AND_WHEN( "the events are processed" )
            {
                QCoreApplication::processEvents();

                THEN( "the plugins are loaded once" )
                {
                    REQUIRE( loads == 1 );
                    REQUIRE( plugins.isLoaded() );
                }
            }
        }

        WHEN( "loading is asked for again, before and after the plugins loaded" )
        {
            plugins.loadSoon();
            plugins.loadSoon();
            QCoreApplication::processEvents();
            plugins.loadSoon();
            QCoreApplication::processEvents();

            THEN( "the plugins are loaded only once" )
            {
                REQUIRE( loads == 1 );
            }
        }
    }
}

SCENARIO( "Work waiting for the Application Plugins runs once they are loaded",
          "[applicationplugins][plugins]" )
{
    GIVEN( "Application Plugins not loaded yet" )
    {
        int loads = 0;
        ApplicationPlugins plugins( [ &loads ]( PluginCatalog&, PluginHost& ) { ++loads; } );
        QObject context;

        WHEN( "work waits for them" )
        {
            int loadsSeenByWork = -1;
            int runs = 0;
            plugins.whenLoaded( &context, [ & ] {
                loadsSeenByWork = loads;
                ++runs;
            } );

            THEN( "it does not run before they are loaded" )
            {
                QCoreApplication::processEvents();
                REQUIRE( runs == 0 );
            }

            AND_WHEN( "they are loaded" )
            {
                plugins.loadSoon();
                QCoreApplication::processEvents();

                THEN( "the work runs once, after loading" )
                {
                    REQUIRE( runs == 1 );
                    REQUIRE( loadsSeenByWork == 1 );

                    plugins.loadSoon();
                    QCoreApplication::processEvents();
                    REQUIRE( runs == 1 );
                }
            }
        }

        WHEN( "work waits for them and its context is gone before they load" )
        {
            int runs = 0;
            {
                QObject shortLived;
                plugins.whenLoaded( &shortLived, [ &runs ] { ++runs; } );
            }
            plugins.loadSoon();
            QCoreApplication::processEvents();

            THEN( "the work does not run" )
            {
                REQUIRE( runs == 0 );
            }
        }
    }

    GIVEN( "Application Plugins already loaded" )
    {
        ApplicationPlugins plugins( []( PluginCatalog&, PluginHost& ) {} );
        plugins.loadSoon();
        QCoreApplication::processEvents();
        REQUIRE( plugins.isLoaded() );

        WHEN( "work waits for them" )
        {
            QObject context;
            int runs = 0;
            plugins.whenLoaded( &context, [ &runs ] { ++runs; } );

            THEN( "it runs right away" )
            {
                REQUIRE( runs == 1 );
            }
        }
    }
}
