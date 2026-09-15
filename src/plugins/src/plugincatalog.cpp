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

#include "plugincatalog.h"

#include "log.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>

namespace logsquirl::plugins {

// ── Platform-specific plugin directories ────────────────────────────────────

QStringList PluginCatalog::defaultPluginDirectories()
{
    QStringList dirs;
    const auto appDir = QCoreApplication::applicationDirPath();

#if defined( Q_OS_MACOS )
    // Inside .app bundle: Contents/PlugIns/
    dirs << QDir( appDir + "/../PlugIns" ).absolutePath();
    // User-installed plugins
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + "/plugins";
#elif defined( Q_OS_WIN )
    dirs << appDir + "/plugins";
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + "/plugins";
#else
    // Linux / other Unix
    dirs << appDir + "/plugins";
    dirs << QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + "/plugins";
#endif

    return dirs;
}

// ── Discovery ───────────────────────────────────────────────────────────────

void PluginCatalog::discoverPlugins()
{
    discoverPlugins( defaultPluginDirectories() );
}

void PluginCatalog::discoverPlugins( const QStringList& directories )
{
    discovered_.clear();
    for ( const auto& dir : directories ) {
        discoverPluginsIn( dir );
    }
    LOG_INFO << "Plugin discovery complete: " << discovered_.size() << " plugin(s) found";
}

void PluginCatalog::discoverPluginsIn( const QString& directory )
{
    const QDir dir( directory );
    if ( !dir.exists() ) {
        LOG_DEBUG << "Plugin directory does not exist: " << directory;
        return;
    }

    LOG_INFO << "Scanning for plugins in: " << directory;

    // Look for plugin.json in immediate subdirectories
    QDirIterator it( directory, QDir::Dirs | QDir::NoDotAndDotDot );
    while ( it.hasNext() ) {
        const auto subDir = it.next();
        const auto manifestPath = QDir( subDir ).filePath( "plugin.json" );

        if ( !QFile::exists( manifestPath ) ) {
            continue;
        }

        auto result = PluginMetadata::fromJsonFile( manifestPath );
        if ( !result.has_value() ) {
            LOG_WARNING << "Skipping invalid plugin manifest: " << result.error();
            continue;
        }

        // Check for duplicate IDs — keep the first one found
        const auto& meta = result.value();
        if ( const auto* existing = findDiscovered( meta.id() ) ) {
            LOG_WARNING << "Duplicate plugin ID '" << meta.id() << "' — keeping first found at "
                        << existing->directory();
            continue;
        }

        LOG_INFO << "Discovered plugin: " << meta.id() << " v" << meta.version() << " at "
                 << meta.directory();
        discovered_.push_back( std::move( result.value() ) );
    }
}

const PluginMetadata* PluginCatalog::findDiscovered( const QString& pluginId ) const
{
    for ( const auto& meta : discovered_ ) {
        if ( meta.id() == pluginId ) {
            return &meta;
        }
    }
    return nullptr;
}

} // namespace logsquirl::plugins
