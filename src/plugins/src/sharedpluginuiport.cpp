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

#include "sharedpluginuiport.h"

#include <algorithm>
#include <set>

namespace logsquirl::plugins {

void SharedPluginUiPort::addWindow( PluginUiPort* window )
{
    if ( !window ) {
        return;
    }
    const std::scoped_lock lock( mutex_ );
    if ( std::ranges::find( windows_, window ) != windows_.end() ) {
        return;
    }
    windows_.push_back( window );

    for ( const auto& action : menuActions_ ) {
        show( *window, action );
    }
    for ( auto& widget : widgets_ ) {
        if ( !widget.shownIn ) {
            widget.shownIn = window;
            show( *window, widget );
        }
    }
}

void SharedPluginUiPort::removeWindow( PluginUiPort* window )
{
    const std::scoped_lock lock( mutex_ );
    const auto it = std::ranges::find( windows_, window );
    if ( it == windows_.end() ) {
        return;
    }
    windows_.erase( it );

    // Only this window's copies go.
    std::set<QString> pluginIds;
    for ( const auto& action : menuActions_ ) {
        pluginIds.insert( action.pluginId );
    }
    for ( const auto& widget : widgets_ ) {
        pluginIds.insert( widget.pluginId );
    }
    for ( const auto& pluginId : pluginIds ) {
        window->removeContributions( pluginId );
    }

    // A widget exists once: it moves on rather than going with the window.
    auto* nextWindow = mostRecentlyActive();
    for ( auto& widget : widgets_ ) {
        if ( widget.shownIn == window ) {
            widget.shownIn = nextWindow;
            if ( nextWindow ) {
                show( *nextWindow, widget );
            }
        }
    }
}

void SharedPluginUiPort::activateWindow( PluginUiPort* window )
{
    const std::scoped_lock lock( mutex_ );
    const auto it = std::ranges::find( windows_, window );
    if ( it != windows_.end() ) {
        std::rotate( it, it + 1, windows_.end() );
    }
}

PluginUiPort* SharedPluginUiPort::mostRecentlyActive() const
{
    return windows_.empty() ? nullptr : windows_.back();
}

void SharedPluginUiPort::show( PluginUiPort& window, const Widget& widget )
{
    switch ( widget.place ) {
    case WidgetPlace::Status:
        window.addStatusWidget( widget.pluginId, widget.handle );
        break;
    case WidgetPlace::Sidebar:
        window.addSidebarTab( widget.pluginId, widget.label, widget.handle );
        break;
    case WidgetPlace::Footer:
        window.addFooterWidget( widget.pluginId, widget.handle );
        break;
    }
}

void SharedPluginUiPort::show( PluginUiPort& window, const MenuAction& action )
{
    window.addMenuAction( action.pluginId, action.menuPath, action.label, action.callback,
                          action.userData );
}

void SharedPluginUiPort::addWidget( WidgetPlace place, const QString& pluginId,
                                    const QString& label, PluginWidgetHandle handle )
{
    const std::scoped_lock lock( mutex_ );
    const auto alreadyAdded = std::ranges::any_of( widgets_, [ & ]( const Widget& widget ) {
        return widget.place == place && widget.pluginId == pluginId && widget.handle == handle;
    } );
    if ( alreadyAdded ) {
        return;
    }
    auto& widget = widgets_.emplace_back( Widget{ .place = place,
                                                  .pluginId = pluginId,
                                                  .label = label,
                                                  .handle = handle,
                                                  .shownIn = mostRecentlyActive() } );
    if ( widget.shownIn ) {
        show( *widget.shownIn, widget );
    }
}

void SharedPluginUiPort::removeWidget( WidgetPlace place, const QString& pluginId,
                                       PluginWidgetHandle handle )
{
    const std::scoped_lock lock( mutex_ );
    const auto it = std::ranges::find_if( widgets_, [ & ]( const Widget& widget ) {
        return widget.place == place && widget.pluginId == pluginId && widget.handle == handle;
    } );
    if ( it == widgets_.end() ) {
        return;
    }
    const auto removed = *it;
    widgets_.erase( it );
    if ( !removed.shownIn ) {
        return;
    }
    switch ( place ) {
    case WidgetPlace::Status:
        removed.shownIn->removeStatusWidget( pluginId, handle );
        break;
    case WidgetPlace::Sidebar:
        removed.shownIn->removeSidebarTab( pluginId, handle );
        break;
    case WidgetPlace::Footer:
        removed.shownIn->removeFooterWidget( pluginId, handle );
        break;
    }
}

void SharedPluginUiPort::addStatusWidget( const QString& pluginId, PluginWidgetHandle widget )
{
    addWidget( WidgetPlace::Status, pluginId, {}, widget );
}

void SharedPluginUiPort::removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget )
{
    removeWidget( WidgetPlace::Status, pluginId, widget );
}

void SharedPluginUiPort::addSidebarTab( const QString& pluginId, const QString& label,
                                        PluginWidgetHandle widget )
{
    addWidget( WidgetPlace::Sidebar, pluginId, label, widget );
}

void SharedPluginUiPort::removeSidebarTab( const QString& pluginId, PluginWidgetHandle widget )
{
    removeWidget( WidgetPlace::Sidebar, pluginId, widget );
}

void SharedPluginUiPort::addFooterWidget( const QString& pluginId, PluginWidgetHandle widget )
{
    addWidget( WidgetPlace::Footer, pluginId, {}, widget );
}

void SharedPluginUiPort::removeFooterWidget( const QString& pluginId, PluginWidgetHandle widget )
{
    removeWidget( WidgetPlace::Footer, pluginId, widget );
}

void SharedPluginUiPort::addMenuAction( const QString& pluginId, const QString& menuPath,
                                        const QString& label, PluginCallbackFn callback,
                                        void* userData )
{
    const std::scoped_lock lock( mutex_ );
    // A plugin enabled again without a restart registers its actions again.
    const auto alreadyAdded = std::ranges::any_of( menuActions_, [ & ]( const MenuAction& action ) {
        return action.pluginId == pluginId && action.label == label;
    } );
    if ( alreadyAdded ) {
        return;
    }
    const auto& action = menuActions_.emplace_back( MenuAction{ .pluginId = pluginId,
                                                                .menuPath = menuPath,
                                                                .label = label,
                                                                .callback = callback,
                                                                .userData = userData } );
    for ( auto* window : windows_ ) {
        show( *window, action );
    }
}

void SharedPluginUiPort::removeContributions( const QString& pluginId )
{
    const std::scoped_lock lock( mutex_ );
    std::erase_if( menuActions_,
                   [ & ]( const MenuAction& action ) { return action.pluginId == pluginId; } );
    std::erase_if( widgets_,
                   [ & ]( const Widget& widget ) { return widget.pluginId == pluginId; } );
    for ( auto* window : windows_ ) {
        window->removeContributions( pluginId );
    }
}

PluginWidgetHandle SharedPluginUiPort::configurationParent()
{
    const std::scoped_lock lock( mutex_ );
    auto* window = mostRecentlyActive();
    return window ? window->configurationParent() : PluginWidgetHandle{};
}

} // namespace logsquirl::plugins
