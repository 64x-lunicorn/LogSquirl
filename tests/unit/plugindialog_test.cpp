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

#include "plugindialog.h"
#include "pluginmanager.h"

#include <QCheckBox>
#include <QPushButton>

using logsquirl::plugins::PluginDialog;
using logsquirl::plugins::PluginManager;

SCENARIO( "PluginDialog footer contains expected buttons", "[plugindialog][plugins]" )
{
    GIVEN( "A freshly constructed PluginDialog" )
    {
        PluginManager manager;
        PluginDialog dialog( manager );

        THEN( "The dialog contains a 'Plugin Folder' button" )
        {
            const auto buttons = dialog.findChildren<QPushButton*>();
            bool found = false;
            for ( const auto* btn : buttons ) {
                if ( btn->text() == QObject::tr( "Plugin Folder" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }

        THEN( "The dialog contains a 'Close' button" )
        {
            const auto buttons = dialog.findChildren<QPushButton*>();
            bool found = false;
            for ( const auto* btn : buttons ) {
                if ( btn->text() == QObject::tr( "Close" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }

        THEN( "The dialog contains an auto-load checkbox" )
        {
            const auto checkboxes = dialog.findChildren<QCheckBox*>();
            bool found = false;
            for ( const auto* cb : checkboxes ) {
                if ( cb->text().contains( "Auto-load" ) ) {
                    found = true;
                    break;
                }
            }
            REQUIRE( found );
        }
    }
}
