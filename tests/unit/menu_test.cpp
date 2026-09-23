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

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>

#include "menu.h"

namespace {

QIcon anIcon()
{
    QPixmap pixmap( 16, 16 );
    pixmap.fill( Qt::white );
    return QIcon( pixmap );
}

} // namespace

SCENARIO( "The menus of a menu bar can be stripped of their icons", "[ui]" )
{
    GIVEN( "a menu bar whose menu and submenu carry icons" )
    {
        QMenuBar bar;
        QMenu* tools = bar.addMenu( "Tools" );
        QAction* scratchpad = tools->addAction( anIcon(), "Scratchpad" );
        QMenu* more = tools->addMenu( "More" );
        QAction* deeper = more->addAction( anIcon(), "Deeper" );

        REQUIRE( scratchpad->isIconVisibleInMenu() );
        REQUIRE( deeper->isIconVisibleInMenu() );

        WHEN( "its icons are hidden" )
        {
            hideIconsInMenus( &bar );

            THEN( "no action of a menu or a submenu shows its icon there" )
            {
                REQUIRE_FALSE( scratchpad->isIconVisibleInMenu() );
                REQUIRE_FALSE( deeper->isIconVisibleInMenu() );
            }

            THEN( "the actions keep their icons, so a toolbar still shows them" )
            {
                REQUIRE_FALSE( scratchpad->icon().isNull() );
                REQUIRE_FALSE( deeper->icon().isNull() );
            }
        }
    }

    GIVEN( "nothing to strip" )
    {
        THEN( "a null menu is no error" )
        {
            REQUIRE_NOTHROW( hideIconsInMenus( nullptr ) );
        }
    }
}
