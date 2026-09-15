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

#include "pluginuiadapter.h"

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QTabWidget>
#include <QTest>
#include <QToolBar>

#include <thread>

using logsquirl::plugins::PluginWidgetHandle;

namespace {

PluginWidgetHandle handleOf( QWidget* widget )
{
    return PluginWidgetHandle{ static_cast<void*>( widget ) };
}

/// The action the menu shows with the given text, or nullptr.
QAction* menuActionNamed( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

int triggeredCount = 0;

void countTrigger( void* userData )
{
    triggeredCount += *static_cast<int*>( userData );
}

} // namespace

SCENARIO( "The main window shows what plugins contribute through the Plugin UI Port",
          "[pluginuiadapter][plugins]" )
{
    GIVEN( "A main window with a Plugins menu, a sidebar and a Plugin UI adapter" )
    {
        QMainWindow window;
        QMenu pluginsMenu;
        auto* separator = pluginsMenu.addSeparator();
        pluginsMenu.addAction( QStringLiteral( "Manage Plugins" ) );
        QTabWidget sidebarTabs;
        PluginUiAdapter adapter( window, pluginsMenu, separator, sidebarTabs );

        const auto pluginId = QStringLiteral( "com.test.plugin" );
        QPointer<QLabel> widget = new QLabel( QStringLiteral( "plugin widget" ) );

        WHEN( "A plugin adds a status widget" )
        {
            adapter.addStatusWidget( pluginId, handleOf( widget ) );

            THEN( "The widget sits in a toolbar at the top of the window" )
            {
                auto* toolBar = qobject_cast<QToolBar*>( widget->parentWidget() );
                REQUIRE( toolBar != nullptr );
                REQUIRE( window.toolBarArea( toolBar ) == Qt::TopToolBarArea );
            }

            AND_WHEN( "The plugin removes it again" )
            {
                adapter.removeStatusWidget( pluginId, handleOf( widget ) );

                THEN( "The widget is no longer part of the window" )
                {
                    REQUIRE( widget->parentWidget() == nullptr );
                }
            }
        }

        WHEN( "A plugin adds a footer widget" )
        {
            adapter.addFooterWidget( pluginId, handleOf( widget ) );

            THEN( "The widget sits in a toolbar at the bottom of the window" )
            {
                auto* toolBar = qobject_cast<QToolBar*>( widget->parentWidget() );
                REQUIRE( toolBar != nullptr );
                REQUIRE( window.toolBarArea( toolBar ) == Qt::BottomToolBarArea );
            }

            AND_WHEN( "The plugin removes it again" )
            {
                adapter.removeFooterWidget( pluginId, handleOf( widget ) );

                THEN( "The widget is no longer part of the window" )
                {
                    REQUIRE( widget->parentWidget() == nullptr );
                }
            }
        }

        WHEN( "A plugin adds a sidebar tab" )
        {
            adapter.addSidebarTab( pluginId, QStringLiteral( "Plugin tab" ), handleOf( widget ) );

            THEN( "The sidebar shows the widget under the label" )
            {
                const auto index = sidebarTabs.indexOf( widget );
                REQUIRE( index >= 0 );
                REQUIRE( sidebarTabs.tabText( index ) == "Plugin tab" );
            }

            AND_WHEN( "The plugin removes it again" )
            {
                adapter.removeSidebarTab( pluginId, handleOf( widget ) );

                THEN( "The sidebar no longer shows the widget" )
                {
                    REQUIRE( sidebarTabs.indexOf( widget ) == -1 );
                    REQUIRE( widget->parentWidget() == nullptr );
                }
            }
        }

        WHEN( "A plugin adds the same menu action twice" )
        {
            int increment = 1;
            triggeredCount = 0;
            adapter.addMenuAction( pluginId, QStringLiteral( "Plugins" ),
                                   QStringLiteral( "Plugin action" ), &countTrigger, &increment );
            adapter.addMenuAction( pluginId, QStringLiteral( "Plugins" ),
                                   QStringLiteral( "Plugin action" ), &countTrigger, &increment );

            THEN( "The Plugins menu shows it once, above the separator" )
            {
                const auto actions = pluginsMenu.actions();
                REQUIRE( actions.size() == 3 );
                REQUIRE( actions[ 0 ]->text() == "Plugin action" );
                REQUIRE( actions[ 1 ] == separator );
            }

            AND_WHEN( "The user triggers it" )
            {
                menuActionNamed( pluginsMenu, QStringLiteral( "Plugin action" ) )->trigger();

                THEN( "The plugin's callback runs with its user data" )
                {
                    REQUIRE( triggeredCount == 1 );
                }
            }
        }

        WHEN( "All contributions of a plugin are removed" )
        {
            QPointer<QLabel> sidebarWidget = new QLabel( QStringLiteral( "sidebar" ) );
            adapter.addStatusWidget( pluginId, handleOf( widget ) );
            adapter.addSidebarTab( pluginId, QStringLiteral( "Plugin tab" ),
                                   handleOf( sidebarWidget ) );
            adapter.addMenuAction( pluginId, QStringLiteral( "Plugins" ),
                                   QStringLiteral( "Plugin action" ), nullptr, nullptr );
            adapter.addMenuAction( QStringLiteral( "com.test.other" ), QStringLiteral( "Plugins" ),
                                   QStringLiteral( "Other action" ), nullptr, nullptr );

            adapter.removeContributions( pluginId );

            THEN( "Its menu actions and the widgets it left behind are gone" )
            {
                REQUIRE( menuActionNamed( pluginsMenu, QStringLiteral( "Plugin action" ) )
                         == nullptr );
                REQUIRE( widget->parentWidget() == nullptr );
                REQUIRE( sidebarTabs.indexOf( sidebarWidget ) == -1 );
                REQUIRE( sidebarWidget->parentWidget() == nullptr );
            }

            THEN( "Other plugins keep their menu actions" )
            {
                REQUIRE( menuActionNamed( pluginsMenu, QStringLiteral( "Other action" ) )
                         != nullptr );
            }

            delete sidebarWidget;
        }

        WHEN( "A plugin adds a status widget from a thread of its own" )
        {
            std::thread worker(
                [ & ] { adapter.addStatusWidget( pluginId, handleOf( widget ) ); } );
            worker.join();

            THEN( "The widget reaches the toolbar on the window's thread" )
            {
                REQUIRE( QTest::qWaitFor( [ & ] { return widget->parentWidget() != nullptr; } ) );
            }
        }

        WHEN( "A plugin asks for a parent for its configuration dialog" )
        {
            THEN( "It gets the main window" )
            {
                REQUIRE( adapter.configurationParent().widget == static_cast<void*>( &window ) );
            }
        }

        delete widget;
    }
}
