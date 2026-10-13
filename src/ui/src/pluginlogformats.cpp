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

#include "pluginlogformats.h"

#include <utility>

using logsquirl::plugins::ApplicationPlugins;

PluginLogFormats::PluginLogFormats( ApplicationPlugins& plugins,
                                    std::shared_ptr<LogFormatCatalog> catalog,
                                    EnabledPlugins enabledPlugins, QObject* parent )
    : QObject( parent )
    , plugins_( plugins )
    , catalog_( std::move( catalog ) )
    , enabledPlugins_( std::move( enabledPlugins ) )
{
    connect( &plugins_, &ApplicationPlugins::loaded, this, &PluginLogFormats::refresh );
    connect( &plugins_, &ApplicationPlugins::enabledPluginsChanged, this,
             &PluginLogFormats::refresh );
}

void PluginLogFormats::refresh()
{
    const auto enabled = enabledPlugins_();

    std::vector<LogFormatCatalog::PluginFormats> formats;
    for ( const auto& plugin : plugins_.catalog().discoveredPlugins() ) {
        if ( plugin.formats().isEmpty() || !enabled.contains( plugin.id() ) ) {
            continue;
        }
        formats.push_back( { plugin.id(), plugin.name(), plugin.formatPaths() } );
    }

    // A plugin without Log Formats enabled or disabled, or the plugins
    // discovered with none shipping any, leave the Catalog as it is.
    if ( formats == handedOver_ ) {
        return;
    }
    handedOver_ = formats;
    catalog_->setPluginFormats( std::move( formats ) );
    catalog_->rebuild();
}
