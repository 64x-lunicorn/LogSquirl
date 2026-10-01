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

#include "valuenamescollection.h"

#include <utility>

#include <QSettings>

#include "log.h"

using logsquirl::valuenames::NamingGroup;

namespace {

// The id of the group a check key is of: a group's id holds no '/'.
QString groupIdOfKey( const QString& key )
{
    return key.section( QLatin1Char( '/' ), 0, 0 );
}

// The keys of what is unchecked of the groups.
QSet<QString> uncheckedOf( const QList<NamingGroup>& groups )
{
    QSet<QString> keys;
    for ( const auto& group : groups ) {
        if ( !group.isEnabled() ) {
            keys.insert( ValueNamesCollection::groupCheckKey( group.id() ) );
        }
        for ( const auto& rule : group.rules() ) {
            if ( !rule.enabled ) {
                keys.insert( ValueNamesCollection::ruleCheckKey( group.id(), rule.name ) );
            }
        }
    }
    return keys;
}

QSet<QString> idsOf( const QList<NamingGroup>& groups )
{
    QSet<QString> ids;
    for ( const auto& group : groups ) {
        ids.insert( group.id() );
    }
    return ids;
}

} // namespace

QString ValueNamesCollection::groupCheckKey( const QString& groupId )
{
    return groupId;
}

QString ValueNamesCollection::ruleCheckKey( const QString& groupId, const QString& ruleName )
{
    return groupId + QLatin1Char( '/' ) + ruleName;
}

bool ValueNamesCollection::setGroups( QList<NamingGroup> groups )
{
    // The checks of the groups replaced, and of those removed, are the ones
    // handed over now.
    const auto replaced = idsOf( ownGroups_ ) + idsOf( groups );
    QSet<QString> keys;
    for ( const auto& key : std::as_const( uncheckedKeys_ ) ) {
        if ( !replaced.contains( groupIdOfKey( key ) ) ) {
            keys.insert( key );
        }
    }
    uncheckedKeys_ = keys + uncheckedOf( groups );

    ownGroups_ = std::move( groups );
    return rebuild();
}

bool ValueNamesCollection::setTeamGroups( QList<NamingGroup> groups )
{
    teamGroups_ = std::move( groups );
    return rebuild();
}

bool ValueNamesCollection::setUncheckedKeys( const QSet<QString>& keys )
{
    const auto held = idsOf( groups_ );
    QSet<QString> next;
    for ( const auto& key : std::as_const( uncheckedKeys_ ) ) {
        if ( !held.contains( groupIdOfKey( key ) ) ) {
            next.insert( key );
        }
    }
    for ( const auto& key : keys ) {
        if ( held.contains( groupIdOfKey( key ) ) ) {
            next.insert( key );
        }
    }
    uncheckedKeys_ = std::move( next );
    return rebuild();
}

void ValueNamesCollection::applyChecks( QList<NamingGroup>& groups ) const
{
    for ( auto& group : groups ) {
        group.setEnabled( !uncheckedKeys_.contains( groupCheckKey( group.id() ) ) );
        auto rules = group.rules();
        for ( auto& rule : rules ) {
            rule.enabled = !uncheckedKeys_.contains( ruleCheckKey( group.id(), rule.name ) );
        }
        group.setRules( rules );
    }
}

bool ValueNamesCollection::rebuild()
{
    applyChecks( ownGroups_ );
    applyChecks( teamGroups_ );

    auto groups = ownGroups_ + teamGroups_;
    if ( groups == groups_ ) {
        return false;
    }
    groups_ = std::move( groups );
    namer_ = logsquirl::valuenames::ValueNamer{ groups_ };
    ++generation_;
    return true;
}

void ValueNamesCollection::retrieveFromStorage( QSettings& settings )
{
    QList<NamingGroup> groups;
    if ( settings.contains( "ValueNamesCollection/version" ) ) {
        settings.beginGroup( "ValueNamesCollection" );
        if ( settings.value( "version" ).toInt() <= ValueNamesCollection_VERSION ) {
            const int size = settings.beginReadArray( "sets" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                NamingGroup group;
                group.retrieveFromStorage( settings );
                groups.append( std::move( group ) );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown ValueNamesCollection version, ignoring";
        }
        settings.endGroup();
    }

    QSet<QString> unchecked;
    if ( settings.contains( "ValueNamesChecks/version" ) ) {
        settings.beginGroup( "ValueNamesChecks" );
        if ( settings.value( "version" ).toInt() <= ValueNamesChecks_VERSION ) {
            const int size = settings.beginReadArray( "unchecked" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                unchecked.insert( settings.value( "key" ).toString() );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown ValueNamesChecks version, ignoring";
        }
        settings.endGroup();
    }

    uncheckedKeys_ = std::move( unchecked );
    ownGroups_ = std::move( groups );
    rebuild();
}

void ValueNamesCollection::saveToStorage( QSettings& settings ) const
{
    settings.beginGroup( "ValueNamesCollection" );
    settings.setValue( "version", ValueNamesCollection_VERSION );
    settings.remove( "sets" );
    settings.beginWriteArray( "sets" );
    for ( int i = 0; i < ownGroups_.size(); ++i ) {
        settings.setArrayIndex( i );
        ownGroups_[ i ].saveToStorage( settings );
    }
    settings.endArray();
    settings.endGroup();

    saveChecksToStorage( settings );
}

void ValueNamesCollection::saveChecks() const
{
    saveChecksToStorage( PersistentInfo::getSettings( app_settings{} ) );
}

void ValueNamesCollection::saveChecksToStorage( QSettings& settings ) const
{
    settings.beginGroup( "ValueNamesChecks" );
    settings.setValue( "version", ValueNamesChecks_VERSION );
    settings.remove( "unchecked" );
    settings.beginWriteArray( "unchecked" );
    int index = 0;
    for ( const auto& key : uncheckedKeys_ ) {
        settings.setArrayIndex( index++ );
        settings.setValue( "key", key );
    }
    settings.endArray();
    settings.endGroup();
}
