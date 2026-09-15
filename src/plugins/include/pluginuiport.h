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

#include <QString>

namespace logsquirl::plugins {

/// C-style callback function pointer used in the plugin host API.
using PluginCallbackFn = void ( * )( void* );

/**
 * A widget a plugin hands to the host.
 *
 * It wraps the `void*` the host callback table carries, so the plugin ABI
 * stays as it is. The plugin layer only passes it on; the UI layer that
 * implements the PluginUiPort is the only place that turns it back into a
 * widget.
 */
struct PluginWidgetHandle {
    void* widget = nullptr;

    bool operator==( const PluginWidgetHandle& ) const = default;
};

/**
 * The Plugin UI Port: everything the plugin layer needs from the user
 * interface to show what plugins contribute.
 *
 * The plugin layer calls it from the host callbacks, on whichever thread the
 * plugin called the host; an implementation that touches widgets moves that
 * work to their thread. Every call names the plugin it is for, so an
 * implementation can take all of a plugin's contributions away again.
 */
class PluginUiPort {
public:
    virtual ~PluginUiPort() = default;

    /** Show a plugin's widget in the status area. */
    virtual void addStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Take a status widget of a plugin away again; the plugin keeps owning it. */
    virtual void removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Show a plugin's widget as a sidebar tab titled label. */
    virtual void addSidebarTab( const QString& pluginId, const QString& label,
                                PluginWidgetHandle widget ) = 0;

    /** Take a sidebar tab of a plugin away again; the plugin keeps owning its widget. */
    virtual void removeSidebarTab( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Show a plugin's widget in the footer area. */
    virtual void addFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Take a footer widget of a plugin away again; the plugin keeps owning it. */
    virtual void removeFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /**
     * Add a menu action for a plugin.
     * @param menuPath  Slash-separated menu path the plugin asked for.
     * @param label     Text of the action.
     * @param callback  Called with userData when the user triggers the action.
     */
    virtual void addMenuAction( const QString& pluginId, const QString& menuPath,
                                const QString& label, PluginCallbackFn callback, void* userData )
        = 0;

    /**
     * Take away everything a plugin still contributes: its menu actions and
     * any widget it did not remove itself. Called after the plugin was shut
     * down and before its library is released.
     */
    virtual void removeContributions( const QString& pluginId ) = 0;

    /** The widget a plugin's configuration dialog is opened on. */
    virtual PluginWidgetHandle configurationParent() = 0;
};

} // namespace logsquirl::plugins
