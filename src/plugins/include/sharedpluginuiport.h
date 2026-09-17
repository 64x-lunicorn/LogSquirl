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

#include <QString>

#include <mutex>
#include <vector>

namespace logsquirl::plugins {

/**
 * The Plugin UI Port of the Application Plugins: shows what plugins
 * contribute in every window (#303).
 *
 * Plugins contribute once, while they are loaded, and every window has a
 * Plugin UI Port of its own. This port remembers the contributions and hands
 * them on to the windows:
 *
 * - A menu action is shown in every window, including a window opened later.
 * - A widget exists once, so it is shown in one window: the most recently
 *   active one when the plugin registers it. When that window goes, the
 *   widget moves to the most recently active remaining window; with no
 *   window left, it is shown in the next window added.
 *
 * A window that goes only takes its own copies away; the plugins keep
 * contributing to the others.
 *
 * Windows are added, removed and activated on the main (GUI) thread. The
 * PluginUiPort calls may come from any thread, as the Plugin Host makes them.
 */
class SharedPluginUiPort : public PluginUiPort {
public:
    SharedPluginUiPort() = default;

    SharedPluginUiPort( const SharedPluginUiPort& ) = delete;
    SharedPluginUiPort& operator=( const SharedPluginUiPort& ) = delete;

    /**
     * Show the contributions in a window's port as well: every menu action,
     * and the widgets no window shows. The window counts as the most
     * recently active one until another is activated.
     */
    void addWindow( PluginUiPort* window );

    /**
     * Take the contributions out of a window's port, before the window goes.
     * Its widgets move to the most recently active remaining window.
     */
    void removeWindow( PluginUiPort* window );

    /** The window of this port has become the active one. */
    void activateWindow( PluginUiPort* window );

    void addStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override;
    void removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) override;
    void addSidebarTab( const QString& pluginId, const QString& label,
                        PluginWidgetHandle widget ) override;
    void removeSidebarTab( const QString& pluginId, PluginWidgetHandle widget ) override;
    void addFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) override;
    void removeFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) override;
    void addMenuAction( const QString& pluginId, const QString& menuPath, const QString& label,
                        PluginCallbackFn callback, void* userData ) override;
    void removeContributions( const QString& pluginId ) override;

    /** The most recently active window's, or a null handle without a window. */
    PluginWidgetHandle configurationParent() override;

private:
    enum class WidgetPlace { Status, Sidebar, Footer };

    struct Widget {
        WidgetPlace place;
        QString pluginId;
        QString label;
        PluginWidgetHandle handle;
        /// The window's port showing the widget, or nullptr while no window is there.
        PluginUiPort* shownIn = nullptr;
    };

    struct MenuAction {
        QString pluginId;
        QString menuPath;
        QString label;
        PluginCallbackFn callback;
        void* userData;
    };

    void addWidget( WidgetPlace place, const QString& pluginId, const QString& label,
                    PluginWidgetHandle handle );
    void removeWidget( WidgetPlace place, const QString& pluginId, PluginWidgetHandle handle );

    static void show( PluginUiPort& window, const Widget& widget );
    static void show( PluginUiPort& window, const MenuAction& action );

    /// The most recently active window's port, or nullptr.
    PluginUiPort* mostRecentlyActive() const;

    // Recursive: a window's port may make a plugin call the host again while
    // it shows or removes a widget.
    mutable std::recursive_mutex mutex_;
    /// Ordered by activation, the most recently active last.
    std::vector<PluginUiPort*> windows_;
    std::vector<Widget> widgets_;
    std::vector<MenuAction> menuActions_;
};

} // namespace logsquirl::plugins
