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

#include "pluginmetadata.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace logsquirl::plugins {

/**
 * The Plugin Catalog: which plugins are installed.
 *
 * Scans plugin directories for plugin.json manifests and keeps their metadata.
 * It never loads a plugin library; loading is the Plugin Host's job, which
 * looks plugins up here by id. Needs Qt Core only.
 */
class PluginCatalog {
public:
    /** Return the platform-specific plugin search directories. */
    static QStringList defaultPluginDirectories();

    /**
     * Rescan the default plugin directories. Replaces what was discovered
     * before, so plugins removed from disk disappear from the catalog.
     */
    void discoverPlugins();

    /**
     * Rescan the given directories, in order. Replaces what was discovered
     * before; for a duplicate id the plugin found first is kept.
     */
    void discoverPlugins( const QStringList& directories );

    /**
     * Scan a single directory: each immediate subdirectory with a valid
     * plugin.json is one plugin. Results are merged into the discovered list;
     * a plugin whose id is already listed is skipped.
     */
    void discoverPluginsIn( const QString& directory );

    /** Return metadata for all discovered plugins, in discovery order. */
    const std::vector<PluginMetadata>& discoveredPlugins() const
    {
        return discovered_;
    }

    /** Return the metadata of a discovered plugin, or nullptr if the id is unknown. */
    const PluginMetadata* findDiscovered( const QString& pluginId ) const;

private:
    std::vector<PluginMetadata> discovered_;
};

} // namespace logsquirl::plugins
