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
