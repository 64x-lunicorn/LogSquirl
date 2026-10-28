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

#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginPattern;
using logsquirl::plugins::PluginRegexLabAnswer;
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
    /// The menu path each of menuActions was added with.
    QStringList menuActionPaths;
    /// The user data each of menuActions was added with.
    std::vector<void*> menuActionUserData;
    /// How often a menu action was asked to go.
    int menuActionRemovals = 0;
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
    void addMenuAction( const QString&, const QString& menuPath, const QString& label,
                        PluginCallbackFn, void* userData ) override
    {
        menuActions.append( label );
        menuActionPaths.append( menuPath );
        menuActionUserData.push_back( userData );
    }
    void removeMenuAction( const QString&, void* userData ) override
    {
        const auto it = std::ranges::find( menuActionUserData, userData );
        if ( it != menuActionUserData.end() ) {
            menuActions.removeAt( it - menuActionUserData.begin() );
            menuActionPaths.removeAt( it - menuActionUserData.begin() );
            menuActionUserData.erase( it );
        }
        ++menuActionRemovals;
    }
    void removeContributions( const QString& pluginId ) override
    {
        menuActions.clear();
        menuActionPaths.clear();
        menuActionUserData.clear();
        std::erase_if( statusWidgets, [ & ]( const auto& w ) { return w.first == pluginId; } );
        sidebarTabs.clear();
        footerWidgets.clear();
    }
    PluginWidgetHandle configurationParent() override
    {
        return parent;
    }
    bool openRegexLab( const QString&, const PluginPattern& pattern, QObject*,
                       PluginRegexLabAnswer ) override
    {
        regexLabs.append( pattern.pattern );
        return true;
    }

    QStringList regexLabs;

    /// The Encoding of the tab in front, and of each Log File a tab shows.
    std::optional<QString> encodingInFront;
    std::map<QString, QString> encodingOf;

    std::optional<QString> logFileEncoding( const QString& logFile ) override
    {
        if ( logFile.isEmpty() ) {
            return encodingInFront;
        }
        const auto tab = encodingOf.find( logFile );
        return tab != encodingOf.end() ? std::optional( tab->second ) : std::nullopt;
    }

    /// The Marks of each Log File a tab of the window shows (#866).
    std::map<QString, std::vector<std::uint64_t>> marksOf;

    void addMarks( const QString& logFile, const std::vector<std::uint64_t>& logLines ) override
    {
        const auto tab = marksOf.find( logFile );
        if ( tab == marksOf.end() ) {
            return;
        }
        tab->second.insert( tab->second.end(), logLines.begin(), logLines.end() );
        std::ranges::sort( tab->second );
    }
    void removeMarks( const QString& logFile, const std::vector<std::uint64_t>& logLines ) override
    {
        const auto tab = marksOf.find( logFile );
        if ( tab == marksOf.end() ) {
            return;
        }
        std::erase_if( tab->second, [ & ]( std::uint64_t line ) {
            return std::ranges::find( logLines, line ) != logLines.end();
        } );
    }
    std::optional<std::vector<std::uint64_t>> marks( const QString& logFile ) override
    {
        const auto tab = marksOf.find( logFile );
        return tab != marksOf.end() ? std::optional( tab->second ) : std::nullopt;
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

SCENARIO( "A plugin's menu action goes from every window when it is removed",
          "[sharedpluginuiport][plugins]" )
{
    GIVEN( "Two windows and a plugin with two menu actions" )
    {
        SharedPluginUiPort port;
        FakeWindowPort first;
        FakeWindowPort second;
        port.addWindow( &first );
        port.addWindow( &second );
        int stays = 0;
        int goes = 0;
        port.addMenuAction( PluginId, QString(), QStringLiteral( "Stays" ), noAction, &stays );
        port.addMenuAction( PluginId, QString(), QStringLiteral( "Goes" ), noAction, &goes );

        WHEN( "the plugin removes one of them (#857)" )
        {
            port.removeMenuAction( PluginId, &goes );

            THEN( "both windows show only the other one, and so does a window added later" )
            {
                REQUIRE( first.menuActions == QStringList{ QStringLiteral( "Stays" ) } );
                REQUIRE( second.menuActions == QStringList{ QStringLiteral( "Stays" ) } );
                FakeWindowPort later;
                port.addWindow( &later );
                REQUIRE( later.menuActions == QStringList{ QStringLiteral( "Stays" ) } );
                port.removeWindow( &later );
            }

            AND_WHEN( "it removes the same one again, or one it never added" )
            {
                int never = 0;
                port.removeMenuAction( PluginId, &goes );
                port.removeMenuAction( PluginId, &never );
                port.removeMenuAction( QStringLiteral( "com.test.other" ), &stays );

                THEN( "nothing changes, and no window is asked" )
                {
                    REQUIRE( first.menuActions == QStringList{ QStringLiteral( "Stays" ) } );
                    REQUIRE( second.menuActions == QStringList{ QStringLiteral( "Stays" ) } );
                    REQUIRE( first.menuActionRemovals == 1 );
                    REQUIRE( second.menuActionRemovals == 1 );
                }
            }
        }

        port.removeWindow( &first );
        port.removeWindow( &second );
    }
}

SCENARIO( "A plugin's menu actions of one label in different submenus each show",
          "[sharedpluginuiport][plugins]" )
{
    GIVEN( "A window and a plugin with an action of one label in two submenus (#870)" )
    {
        SharedPluginUiPort port;
        FakeWindowPort window;
        port.addWindow( &window );
        int toolsExport = 0;
        int reportExport = 0;
        port.addMenuAction( PluginId, QStringLiteral( "Tools" ), QStringLiteral( "Export" ),
                            noAction, &toolsExport );
        port.addMenuAction( PluginId, QStringLiteral( "Report" ), QStringLiteral( "Export" ),
                            noAction, &reportExport );

        THEN( "the window shows both, each with what it was added with" )
        {
            REQUIRE( window.menuActions
                     == QStringList{ QStringLiteral( "Export" ), QStringLiteral( "Export" ) } );
            REQUIRE( window.menuActionPaths
                     == QStringList{ QStringLiteral( "Tools" ), QStringLiteral( "Report" ) } );
            REQUIRE( window.menuActionUserData
                     == std::vector<void*>{ &toolsExport, &reportExport } );
        }

        THEN( "a window added later shows both as well" )
        {
            FakeWindowPort later;
            port.addWindow( &later );
            REQUIRE( later.menuActions.size() == 2 );
            port.removeWindow( &later );
        }

        WHEN( "the plugin adds one of them again, as when it is enabled again, also with the "
              "Plugins menu named in its path" )
        {
            int again = 0;
            port.addMenuAction( PluginId, QStringLiteral( "Tools" ), QStringLiteral( "Export" ),
                                noAction, &again );
            port.addMenuAction( PluginId, QStringLiteral( "Plugins/ Tools /" ),
                                QStringLiteral( "Export" ), noAction, &again );

            THEN( "the window still shows each once" )
            {
                REQUIRE( window.menuActions.size() == 2 );
                REQUIRE( window.menuActionUserData
                         == std::vector<void*>{ &toolsExport, &reportExport } );
            }
        }

        port.removeWindow( &window );
    }
}

SCENARIO( "A plugin's Regex Lab opens in the most recently active window",
          "[sharedpluginuiport][pluginregexlab][plugins]" )
{
    GIVEN( "No window" )
    {
        SharedPluginUiPort port;
        QObject context;

        THEN( "No Lab opens" )
        {
            REQUIRE_FALSE( port.openRegexLab(
                PluginId, PluginPattern{ .pattern = QStringLiteral( "a" ), .matchesCase = true },
                &context, []( auto ) {} ) );
        }

        AND_GIVEN( "Two windows, the second the most recently active" )
        {
            FakeWindowPort first;
            FakeWindowPort second;
            port.addWindow( &first );
            port.addWindow( &second );
            port.activateWindow( &first );
            port.activateWindow( &second );

            WHEN( "a plugin opens the Regex Lab" )
            {
                const auto opened = port.openRegexLab(
                    PluginId,
                    PluginPattern{ .pattern = QStringLiteral( "ERROR" ), .matchesCase = true },
                    &context, []( auto ) {} );

                THEN( "it opens in that window only" )
                {
                    REQUIRE( opened );
                    REQUIRE( first.regexLabs.isEmpty() );
                    REQUIRE( second.regexLabs == QStringList{ QStringLiteral( "ERROR" ) } );
                }
            }

            port.removeWindow( &first );
            port.removeWindow( &second );
        }
    }
}

SCENARIO( "A plugin reads the Encoding of a Log File in any window",
          "[sharedpluginuiport][pluginencoding][plugins]" )
{
    GIVEN( "No window" )
    {
        SharedPluginUiPort port;

        THEN( "No Log File is open, in front or by its path" )
        {
            REQUIRE_FALSE( port.logFileEncoding( {} ).has_value() );
            REQUIRE_FALSE( port.logFileEncoding( QStringLiteral( "/logs/a.log" ) ).has_value() );
        }

        AND_GIVEN( "Two windows, the second the most recently active, both showing a.log" )
        {
            FakeWindowPort first;
            FakeWindowPort second;
            first.encodingInFront = QStringLiteral( "UTF-8" );
            first.encodingOf
                = { { QStringLiteral( "/logs/a.log" ), QStringLiteral( "UTF-8" ) },
                    { QStringLiteral( "/logs/b.log" ), QStringLiteral( "UTF-16LE" ) } };
            second.encodingInFront = QStringLiteral( "windows-1252" );
            second.encodingOf
                = { { QStringLiteral( "/logs/a.log" ), QStringLiteral( "windows-1252" ) } };
            port.addWindow( &first );
            port.addWindow( &second );
            port.activateWindow( &first );
            port.activateWindow( &second );

            THEN( "the tab in front is that of the most recently active window" )
            {
                REQUIRE( port.logFileEncoding( {} ) == QStringLiteral( "windows-1252" ) );
            }

            THEN( "a Log File both show is read as the most recently active window reads it" )
            {
                REQUIRE( port.logFileEncoding( QStringLiteral( "/logs/a.log" ) )
                         == QStringLiteral( "windows-1252" ) );
            }

            THEN( "a Log File only the other window shows is found there" )
            {
                REQUIRE( port.logFileEncoding( QStringLiteral( "/logs/b.log" ) )
                         == QStringLiteral( "UTF-16LE" ) );
            }

            THEN( "a Log File no window shows is none" )
            {
                REQUIRE_FALSE(
                    port.logFileEncoding( QStringLiteral( "/logs/c.log" ) ).has_value() );
            }

            port.removeWindow( &first );
            port.removeWindow( &second );
        }
    }
}

SCENARIO( "A plugin sets, removes and reads the Marks of a Log File in every window",
          "[sharedpluginuiport][pluginmarks][plugins]" )
{
    GIVEN( "No window" )
    {
        SharedPluginUiPort port;

        THEN( "no Log File has Marks, and setting some does nothing" )
        {
            port.addMarks( QStringLiteral( "/logs/a.log" ), { 1 } );
            REQUIRE_FALSE( port.marks( QStringLiteral( "/logs/a.log" ) ).has_value() );
        }
    }

    GIVEN( "Two windows that both show a.log, with Marks of their own, and one that shows b.log" )
    {
        SharedPluginUiPort port;
        FakeWindowPort first;
        FakeWindowPort second;
        first.marksOf[ QStringLiteral( "/logs/a.log" ) ] = { 2, 7 };
        second.marksOf[ QStringLiteral( "/logs/a.log" ) ] = { 7, 9 };
        second.marksOf[ QStringLiteral( "/logs/b.log" ) ] = {};
        port.addWindow( &first );
        port.addWindow( &second );

        THEN( "the Marks of a.log are those of both windows, in order and each once" )
        {
            REQUIRE( port.marks( QStringLiteral( "/logs/a.log" ) )
                     == std::vector<std::uint64_t>{ 2, 7, 9 } );
        }

        THEN( "b.log has no Marks, and a Log File no window shows has none at all" )
        {
            REQUIRE( port.marks( QStringLiteral( "/logs/b.log" ) )
                     == std::vector<std::uint64_t>{} );
            REQUIRE_FALSE( port.marks( QStringLiteral( "/logs/c.log" ) ).has_value() );
        }

        WHEN( "a plugin sets Marks on a.log, and removes one" )
        {
            port.addMarks( QStringLiteral( "/logs/a.log" ), { 4 } );
            port.removeMarks( QStringLiteral( "/logs/a.log" ), { 7 } );

            THEN( "both windows set and remove them" )
            {
                REQUIRE( first.marksOf[ QStringLiteral( "/logs/a.log" ) ]
                         == std::vector<std::uint64_t>{ 2, 4 } );
                REQUIRE( second.marksOf[ QStringLiteral( "/logs/a.log" ) ]
                         == std::vector<std::uint64_t>{ 4, 9 } );
                REQUIRE( port.marks( QStringLiteral( "/logs/a.log" ) )
                         == std::vector<std::uint64_t>{ 2, 4, 9 } );
            }
        }

        port.removeWindow( &first );
        port.removeWindow( &second );
    }
}

namespace {

void noContextMenuAction( void* /* userData */,
                          const logsquirl::plugins::PluginContextMenuContext& /* context */ )
{
}

/// The labels of the context menu items, in their order.
QStringList contextMenuLabels( const SharedPluginUiPort& port )
{
    QStringList labels;
    for ( const auto& action : port.contextMenuActions() ) {
        labels.append( action.label );
    }
    return labels;
}

} // namespace

SCENARIO( "The plugins' context menu items are kept in the order they were added",
          "[sharedpluginuiport][plugincontextmenu][plugins]" )
{
    GIVEN( "A window and two plugins that added context menu items in turn" )
    {
        SharedPluginUiPort port;
        FakeWindowPort window;
        port.addWindow( &window );
        const auto otherId = QStringLiteral( "com.test.other" );
        int first = 0;
        int second = 0;
        int third = 0;
        port.addContextMenuAction( PluginId, QStringLiteral( "Mine/Sub" ),
                                   QStringLiteral( "First" ), noContextMenuAction, &first );
        port.addContextMenuAction( otherId, QString(), QStringLiteral( "Second" ),
                                   noContextMenuAction, &second );
        port.addContextMenuAction( PluginId, QString(), QStringLiteral( "Third" ),
                                   noContextMenuAction, &third );

        THEN( "the views get them in that order, each with what it was added with and a serial "
              "of its own, and the window's port is not asked to show them" )
        {
            const auto actions = port.contextMenuActions();
            REQUIRE( contextMenuLabels( port )
                     == QStringList{ QStringLiteral( "First" ), QStringLiteral( "Second" ),
                                     QStringLiteral( "Third" ) } );
            REQUIRE( actions[ 0 ].pluginId == PluginId );
            REQUIRE( actions[ 0 ].menuPath == QStringLiteral( "Mine/Sub" ) );
            REQUIRE( actions[ 0 ].callback == &noContextMenuAction );
            REQUIRE( actions[ 0 ].userData == &first );
            REQUIRE( actions[ 1 ].pluginId == otherId );
            REQUIRE( actions[ 0 ].serial != actions[ 1 ].serial );
            REQUIRE( actions[ 1 ].serial != actions[ 2 ].serial );
            REQUIRE( window.menuActions.isEmpty() );
        }

        WHEN( "a plugin removes one, and one it never added" )
        {
            int never = 0;
            port.removeContextMenuAction( PluginId, &first );
            port.removeContextMenuAction( PluginId, &never );
            port.removeContextMenuAction( PluginId, &second );

            THEN( "only that one goes" )
            {
                REQUIRE( contextMenuLabels( port )
                         == QStringList{ QStringLiteral( "Second" ), QStringLiteral( "Third" ) } );
            }

            AND_WHEN( "it adds one again" )
            {
                port.addContextMenuAction( PluginId, QString(), QStringLiteral( "First" ),
                                           noContextMenuAction, &first );

                THEN( "it comes last, with a serial never given before" )
                {
                    const auto actions = port.contextMenuActions();
                    REQUIRE( actions.back().label == QStringLiteral( "First" ) );
                    REQUIRE( actions.back().serial > actions[ 0 ].serial );
                    REQUIRE( actions.back().serial > actions[ 1 ].serial );
                    REQUIRE( actions.back().serial > 3 );
                }
            }
        }

        WHEN( "a plugin's contributions are removed" )
        {
            port.removeContributions( PluginId );

            THEN( "its items go, the other plugin's stay" )
            {
                REQUIRE( contextMenuLabels( port ) == QStringList{ QStringLiteral( "Second" ) } );
            }
        }

        port.removeWindow( &window );
    }
}
