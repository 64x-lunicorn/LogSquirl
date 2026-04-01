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

#include "tabgroupinfo.h"

#include <algorithm>

#include "log.h"
#include "uuid.h"

namespace {
constexpr int TabGroupInfoVersion = 1;
} // namespace

const std::vector<TabGroupInfo::TabGroup>& TabGroupInfo::groups() const
{
    return groups_;
}

std::optional<TabGroupInfo::TabGroup> TabGroupInfo::groupForTab( const QString& tabPath ) const
{
    for ( const auto& group : groups_ ) {
        if ( group.tabPaths.contains( tabPath ) ) {
            return group;
        }
    }
    return std::nullopt;
}

QString TabGroupInfo::addGroup( const QString& name, const QColor& color )
{
    const auto id = generateIdFromUuid();
    groups_.push_back( TabGroup{ id, name, color, {} } );
    return id;
}

TabGroupInfo& TabGroupInfo::removeGroup( const QString& groupId )
{
    auto it = findGroup( groupId );
    if ( it != groups_.end() ) {
        groups_.erase( it );
    }
    return *this;
}

TabGroupInfo& TabGroupInfo::renameGroup( const QString& groupId, const QString& newName )
{
    auto it = findGroup( groupId );
    if ( it != groups_.end() ) {
        it->name = newName;
    }
    return *this;
}

TabGroupInfo& TabGroupInfo::setGroupColor( const QString& groupId, const QColor& color )
{
    auto it = findGroup( groupId );
    if ( it != groups_.end() ) {
        it->color = color;
    }
    return *this;
}

TabGroupInfo& TabGroupInfo::addTabToGroup( const QString& groupId, const QString& tabPath )
{
    // Remove from any existing group first
    removeTabFromGroup( tabPath );

    auto it = findGroup( groupId );
    if ( it != groups_.end() && !it->tabPaths.contains( tabPath ) ) {
        it->tabPaths.append( tabPath );
    }
    return *this;
}

TabGroupInfo& TabGroupInfo::removeTabFromGroup( const QString& tabPath )
{
    for ( auto& group : groups_ ) {
        group.tabPaths.removeAll( tabPath );
    }
    return *this;
}

void TabGroupInfo::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "TabGroupInfo::saveToStorage";

    settings.beginGroup( "TabGroupInfo" );
    settings.setValue( "version", TabGroupInfoVersion );
    settings.remove( "groups" );
    settings.beginWriteArray( "groups" );
    for ( int i = 0; i < static_cast<int>( groups_.size() ); ++i ) {
        settings.setArrayIndex( i );
        const auto& group = groups_.at( static_cast<size_t>( i ) );
        settings.setValue( "id", group.id );
        settings.setValue( "name", group.name );
        settings.setValue( "color", group.color.name( QColor::HexArgb ) );

        settings.beginWriteArray( "tabPaths" );
        for ( int j = 0; j < group.tabPaths.size(); ++j ) {
            settings.setArrayIndex( j );
            settings.setValue( "path", group.tabPaths.at( j ) );
        }
        settings.endArray();
    }
    settings.endArray();
    settings.endGroup();
}

void TabGroupInfo::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "TabGroupInfo::retrieveFromStorage";

    groups_.clear();

    if ( settings.contains( "TabGroupInfo/version" ) ) {
        settings.beginGroup( "TabGroupInfo" );
        if ( settings.value( "version" ).toInt() == TabGroupInfoVersion ) {
            const int groupCount = settings.beginReadArray( "groups" );
            for ( int i = 0; i < groupCount; ++i ) {
                settings.setArrayIndex( i );

                TabGroup group;
                group.id = settings.value( "id" ).toString();
                group.name = settings.value( "name" ).toString();
                group.color = QColor( settings.value( "color" ).toString() );

                const int pathCount = settings.beginReadArray( "tabPaths" );
                for ( int j = 0; j < pathCount; ++j ) {
                    settings.setArrayIndex( j );
                    group.tabPaths.append( settings.value( "path" ).toString() );
                }
                settings.endArray();

                groups_.push_back( std::move( group ) );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of tab group info, ignoring it...";
        }
        settings.endGroup();
    }
}

std::vector<TabGroupInfo::TabGroup>::iterator TabGroupInfo::findGroup( const QString& groupId )
{
    return std::find_if( groups_.begin(), groups_.end(),
                         [ &groupId ]( const auto& g ) { return g.id == groupId; } );
}

std::vector<TabGroupInfo::TabGroup>::const_iterator
TabGroupInfo::findGroup( const QString& groupId ) const
{
    return std::find_if( groups_.cbegin(), groups_.cend(),
                         [ &groupId ]( const auto& g ) { return g.id == groupId; } );
}
