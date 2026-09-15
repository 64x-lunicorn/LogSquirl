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

#pragma once

#include "pluginuiport.h"

#include <QAction>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <map>
#include <vector>

class QMainWindow;
class QMenu;
class QTabWidget;
class QToolBar;

/**
 * The main window's side of the Plugin UI Port (#175).
 *
 * Shows plugin status widgets in a toolbar at the top of the window, footer
 * widgets in a toolbar at the bottom, sidebar tabs in the sidebar and menu
 * actions at the top of the Plugins menu. It is the only place that turns a
 * PluginWidgetHandle back into a QWidget. Calls from another thread are
 * carried out on the window's thread.
 */
class PluginUiAdapter : public logsquirl::plugins::PluginUiPort {
public:
    /**
     * @param window         Window the toolbars are added to and dialogs open on.
     * @param pluginsMenu    Menu the plugin actions are added to.
     * @param menuSeparator  Action in pluginsMenu the plugin actions are inserted above.
     * @param sidebarTabs    Tab widget of the sidebar.
     */
    PluginUiAdapter( QMainWindow& window, QMenu& pluginsMenu, QAction* menuSeparator,
                     QTabWidget& sidebarTabs );

    PluginUiAdapter( const PluginUiAdapter& ) = delete;
    PluginUiAdapter& operator=( const PluginUiAdapter& ) = delete;

    void addStatusWidget( const QString& pluginId,
                          logsquirl::plugins::PluginWidgetHandle widget ) override;
    void removeStatusWidget( const QString& pluginId,
                             logsquirl::plugins::PluginWidgetHandle widget ) override;
    void addSidebarTab( const QString& pluginId, const QString& label,
                        logsquirl::plugins::PluginWidgetHandle widget ) override;
    void removeSidebarTab( const QString& pluginId,
                           logsquirl::plugins::PluginWidgetHandle widget ) override;
    void addFooterWidget( const QString& pluginId,
                          logsquirl::plugins::PluginWidgetHandle widget ) override;
    void removeFooterWidget( const QString& pluginId,
                             logsquirl::plugins::PluginWidgetHandle widget ) override;
    void addMenuAction( const QString& pluginId, const QString& menuPath, const QString& label,
                        logsquirl::plugins::PluginCallbackFn callback, void* userData ) override;
    void removeContributions( const QString& pluginId ) override;
    logsquirl::plugins::PluginWidgetHandle configurationParent() override;

private:
    /// A widget a plugin placed in the window, with the toolbar action holding it, if any.
    struct PlacedWidget {
        QString pluginId;
        QPointer<QWidget> widget;
        QPointer<QAction> toolBarAction;
    };

    /// A toolbar of the window that shows plugin widgets; created when the first one arrives.
    struct PluginToolBar {
        /// Untranslated title, in the "MainWindow" translation context.
        const char* title = nullptr;
        Qt::ToolBarArea area = Qt::TopToolBarArea;
        QPointer<QToolBar> toolBar{};
        std::vector<PlacedWidget> placed{};
    };

    /// Runs work now when called on the window's thread, otherwise queues it there.
    void onWindowThread( std::function<void()> work );

    /// Adds a widget of a plugin to a toolbar, creating the toolbar on first use.
    void placeInToolBar( PluginToolBar& bar, const QString& pluginId, QWidget* widget );

    /// Takes widgets of a plugin out of a toolbar; all of them when widget is null.
    static void removeFromToolBar( PluginToolBar& bar, const QString& pluginId,
                                   const QWidget* widget );

    /// Takes sidebar tabs of a plugin away; all of them when widget is null.
    void removeFromSidebar( const QString& pluginId, const QWidget* widget );

    QMainWindow& window_;
    QMenu& pluginsMenu_;
    QPointer<QAction> menuSeparator_;
    QTabWidget& sidebarTabs_;

    // Plugin status widgets, in a toolbar below the main toolbar.
    PluginToolBar statusToolBar_{ .title = QT_TRANSLATE_NOOP( "MainWindow", "Plugins" ),
                                  .area = Qt::TopToolBarArea };

    // Plugin footer widgets, in a toolbar at the bottom of the window.
    PluginToolBar footerToolBar_{ .title = QT_TRANSLATE_NOOP( "MainWindow", "Plugin Footer" ),
                                  .area = Qt::BottomToolBarArea };

    std::vector<PlacedWidget> sidebarWidgets_;

    // Tracks menu actions added by each plugin so they can be removed on unload.
    std::map<QString, std::vector<QPointer<QAction>>> menuActions_;
};
