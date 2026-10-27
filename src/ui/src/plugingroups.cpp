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

#include "plugingroups.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

#include "groupexchange.h"
#include "log.h"

using logsquirl::plugins::PluginMetadata;

QString pluginGroupShownName( const QString& groupName, const QString& pluginName )
{
    return QCoreApplication::translate( "PluginGroup", "%1 (%2)" ).arg( groupName, pluginName );
}

namespace logsquirl::plugingroups {

namespace {

QString whyNotRead( groupexchange::ReadError error )
{
    using groupexchange::ReadError;
    switch ( error ) {
    case ReadError::None:
        break;
    case ReadError::Unreadable:
        return QStringLiteral( "cannot be read" );
    case ReadError::NoGroups:
        return QStringLiteral( "holds no group" );
    case ReadError::OtherKind:
    case ReadError::NewerVersion:
        return QStringLiteral( "holds groups of another kind" );
    }
    return {};
}

// Appends the groups of the plugin's files to groups, each under the plugin's
// id for it.
template <typename Group>
void readFiles( const PluginMetadata& plugin, const QStringList& files,
                QList<PluginGroup<Group>>& groups )
{
    QSet<QString> ids;
    for ( const auto& file : files ) {
        const auto read = groupexchange::readGroups<Group>( file );
        if ( read.error != groupexchange::ReadError::None ) {
            LOG_WARNING << "Plugin " << plugin.id() << ": " << file << " "
                        << whyNotRead( read.error ) << ", skipped";
            continue;
        }
        for ( const auto& group : read.groups ) {
            const auto id = idOf( plugin.id(), group.id() );
            if ( ids.contains( id ) ) {
                LOG_WARNING << "Plugin " << plugin.id() << ": " << file << " holds a second group "
                            << "of the id " << group.id() << ", skipped";
                continue;
            }
            ids.insert( id );
            groups.append( { group.withId( id ), plugin.name() } );
        }
    }
}

} // namespace

QString idOf( const QString& pluginId, const QString& groupId )
{
    return QStringLiteral( "plugin:%1:%2" ).arg( pluginId, groupId );
}

PluginGroups readEnabled( const std::vector<PluginMetadata>& plugins, const QStringList& enabled )
{
    std::vector<const PluginMetadata*> shipping;
    for ( const auto& plugin : plugins ) {
        if ( enabled.contains( plugin.id() )
             && ( !plugin.predefinedFilters().isEmpty() || !plugin.highlighterSets().isEmpty() ) ) {
            shipping.push_back( &plugin );
        }
    }
    std::sort( shipping.begin(), shipping.end(),
               []( const auto* a, const auto* b ) { return a->id() < b->id(); } );

    PluginGroups groups;
    for ( const auto* plugin : shipping ) {
        readFiles( *plugin, plugin->predefinedFilterPaths(), groups.filterGroups );
        readFiles( *plugin, plugin->highlighterSetPaths(), groups.highlighterSets );
    }
    return groups;
}

} // namespace logsquirl::plugingroups
