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

// What plugins contribute shows in every window (#303).

#include <catch2/catch_test_macros.hpp>

#include "sharedpluginuiport.h"

#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginUiPort;
using logsquirl::plugins::PluginWidgetHandle;
using logsquirl::plugins::SharedPluginUiPort;

namespace {

const auto PluginId = QStringLiteral( "com.test.plugin" );

/// A window's Plugin UI Port that keeps what it shows.
class FakeWindowPort : public PluginUiPort {
public:
    std::vector<std::pair<QString, PluginWidgetHandle>> statusWidgets;
    std::vector<std::pair<QString, PluginWidgetHandle>> sidebarTabs;
    std::vector<PluginWidgetHandle> footerWidgets;
    QStringList menuActions;
    PluginWidgetHandle parent;

    void addStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        statusWidgets.emplace_back( pluginId, widget );
    }
    void removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override
    {
        std::erase( statusWidgets, std::pair{ pluginId, widget } );
    }
    void addSidebarTab( const QString&, const QString& label, PluginWidgetHandle widget ) override
    {
        sidebarTabs.emplace_back( label, widget );
    }
    void removeSidebarTab( const QString&, PluginWidgetHandle widget ) override
    {
        std::erase_if( sidebarTabs, [ & ]( const auto& tab ) { return tab.second == widget; } );
    }
    void addFooterWidget( const QString&, PluginWidgetHandle widget ) override
    {
        footerWidgets.push_back( widget );
    }
    void removeFooterWidget( const QString&, PluginWidgetHandle widget ) override
    {
        std::erase( footerWidgets, widget );
    }
    void addMenuAction( const QString&, const QString&, const QString& label, PluginCallbackFn,
                        void* ) override
    {
        menuActions.append( label );
    }
    void removeContributions( const QString& pluginId ) override
    {
        menuActions.clear();
        std::erase_if( statusWidgets, [ & ]( const auto& w ) { return w.first == pluginId; } );
        sidebarTabs.clear();
        footerWidgets.clear();
    }
    PluginWidgetHandle configurationParent() override
    {
        return parent;
    }

    bool showsAnything() const
    {
        return !statusWidgets.empty() || !sidebarTabs.empty() || !footerWidgets.empty()
               || !menuActions.isEmpty();
    }
};

int statusWidget = 0;
int sidebarWidget = 0;
int footerWidget = 0;
int firstParent = 0;
int secondParent = 0;

void noAction( void* ) {}

} // namespace

SCENARIO( "What plugins contribute shows in every window", "[sharedpluginuiport][plugins]" )
{
    GIVEN( "Two windows" )
    {
        SharedPluginUiPort port;
        FakeWindowPort first;
        FakeWindowPort second;
        first.parent = PluginWidgetHandle{ &firstParent };
        second.parent = PluginWidgetHandle{ &secondParent };
        port.addWindow( &first );
        port.addWindow( &second );
        port.activateWindow( &first );

        WHEN( "a plugin adds a menu action, a status widget, a sidebar tab and a footer widget" )
        {
            port.addMenuAction( PluginId, QStringLiteral( "Tools" ), QStringLiteral( "Convert" ),
                                noAction, nullptr );
            port.addStatusWidget( PluginId, PluginWidgetHandle{ &statusWidget } );
            port.addSidebarTab( PluginId, QStringLiteral( "Tab" ),
                                PluginWidgetHandle{ &sidebarWidget } );
            port.addFooterWidget( PluginId, PluginWidgetHandle{ &footerWidget } );

            THEN( "the menu action shows in both windows" )
            {
                REQUIRE( first.menuActions == QStringList{ QStringLiteral( "Convert" ) } );
                REQUIRE( second.menuActions == QStringList{ QStringLiteral( "Convert" ) } );
            }

            THEN( "each widget shows once, in the most recently active window" )
            {
                REQUIRE( first.statusWidgets.size() == 1 );
                REQUIRE( first.sidebarTabs.size() == 1 );
                REQUIRE( first.footerWidgets.size() == 1 );
                REQUIRE( second.statusWidgets.empty() );
                REQUIRE( second.sidebarTabs.empty() );
                REQUIRE( second.footerWidgets.empty() );
                REQUIRE( port.configurationParent() == first.parent );
            }

            AND_WHEN( "a third window is added" )
            {
                FakeWindowPort third;
                port.addWindow( &third );

                THEN( "it shows the menu action, but not the widgets shown elsewhere" )
                {
                    REQUIRE( third.menuActions == QStringList{ QStringLiteral( "Convert" ) } );
                    REQUIRE( third.statusWidgets.empty() );
                    REQUIRE( third.sidebarTabs.empty() );
                }
                port.removeWindow( &third );
            }

            AND_WHEN( "the window showing the widgets goes" )
            {
                port.removeWindow( &first );

                THEN( "it shows nothing any more" )
                {
                    REQUIRE_FALSE( first.showsAnything() );
                }

                THEN( "the widgets move to the remaining window, which keeps its menu action" )
                {
                    REQUIRE( second.menuActions == QStringList{ QStringLiteral( "Convert" ) } );
                    REQUIRE( second.statusWidgets.size() == 1 );
                    REQUIRE( second.sidebarTabs
                             == std::vector{ std::pair{ QStringLiteral( "Tab" ),
                                                        PluginWidgetHandle{ &sidebarWidget } } } );
                    REQUIRE( second.footerWidgets.size() == 1 );
                    REQUIRE( port.configurationParent() == second.parent );
                }

                AND_WHEN( "the last window goes and a new one is added" )
                {
                    port.removeWindow( &second );
                    FakeWindowPort next;
                    port.addWindow( &next );

                    THEN( "the new window shows everything" )
                    {
                        REQUIRE_FALSE( second.showsAnything() );
                        REQUIRE( next.menuActions.size() == 1 );
                        REQUIRE( next.statusWidgets.size() == 1 );
                        REQUIRE( next.sidebarTabs.size() == 1 );
                        REQUIRE( next.footerWidgets.size() == 1 );
                    }
                    port.removeWindow( &next );
                }
            }

            AND_WHEN( "the plugin removes its status widget" )
            {
                port.removeStatusWidget( PluginId, PluginWidgetHandle{ &statusWidget } );
                port.removeWindow( &first );

                THEN( "it does not come back in another window" )
                {
                    REQUIRE( second.statusWidgets.empty() );
                    REQUIRE( second.sidebarTabs.size() == 1 );
                }
            }

            AND_WHEN( "the plugin is unloaded" )
            {
                port.removeContributions( PluginId );

                THEN( "no window shows anything of it, nor does a window added later" )
                {
                    REQUIRE_FALSE( first.showsAnything() );
                    REQUIRE_FALSE( second.showsAnything() );
                    FakeWindowPort later;
                    port.addWindow( &later );
                    REQUIRE_FALSE( later.showsAnything() );
                    port.removeWindow( &later );
                }
            }
        }
    }
}
