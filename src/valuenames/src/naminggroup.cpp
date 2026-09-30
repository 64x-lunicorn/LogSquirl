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

#include "naminggroup.h"

#include <QSettings>
#include <QUuid>

#include "log.h"

namespace logsquirl::valuenames {

namespace {

// A group id as generateIdFromUuid() of the utilities makes it, without
// linking them and the Qt modules they bring for one line.
QString newGroupId()
{
    return QUuid::createUuid().toString( QUuid::Id128 );
}

} // namespace

QString NamingRule::tableFor( const QString& group ) const
{
    for ( const auto& groupTable : groupTables ) {
        if ( groupTable.group == group ) {
            return groupTable.table;
        }
    }
    return {};
}

NamingGroup NamingGroup::createNewGroup( const QString& name )
{
    NamingGroup group;
    group.id_ = newGroupId();
    group.name_ = name;
    return group;
}

NamingGroup NamingGroup::withId( const QString& id ) const
{
    auto copy = *this;
    copy.id_ = id;
    return copy;
}

bool NamingGroup::sameAs( const NamingGroup& other ) const
{
    const auto unchecked = []( QList<NamingRule> rules ) {
        for ( auto& rule : rules ) {
            rule.enabled = true;
        }
        return rules;
    };
    return name_ == other.name_ && tables_ == other.tables_
           && unchecked( rules_ ) == unchecked( other.rules_ );
}

const NameTable* NamingGroup::table( const QString& name ) const
{
    for ( const auto& nameTable : tables_ ) {
        if ( nameTable.name == name ) {
            return &nameTable;
        }
    }
    return nullptr;
}

void NamingGroup::saveToStorage( QSettings& settings ) const
{
    settings.beginGroup( "NamingGroup" );
    settings.setValue( "version", NamingGroup_VERSION );
    settings.setValue( "name", name_ );
    settings.setValue( "id", id_ );

    settings.remove( "rules" );
    settings.beginWriteArray( "rules" );
    for ( int i = 0; i < rules_.size(); ++i ) {
        const auto& rule = rules_[ i ];
        settings.setArrayIndex( i );
        settings.setValue( "name", rule.name );
        settings.setValue( "regex", rule.pattern );
        settings.setValue( "template", rule.displayTemplate );
        settings.beginWriteArray( "groups" );
        for ( int j = 0; j < rule.groupTables.size(); ++j ) {
            settings.setArrayIndex( j );
            settings.setValue( "group", rule.groupTables[ j ].group );
            settings.setValue( "table", rule.groupTables[ j ].table );
        }
        settings.endArray();
    }
    settings.endArray();

    settings.remove( "tables" );
    settings.beginWriteArray( "tables" );
    for ( int i = 0; i < tables_.size(); ++i ) {
        const auto& nameTable = tables_[ i ];
        settings.setArrayIndex( i );
        settings.setValue( "name", nameTable.name );
        settings.setValue( "caseSensitive", nameTable.caseSensitive );
        settings.beginWriteArray( "rows" );
        for ( int j = 0; j < nameTable.rows.size(); ++j ) {
            settings.setArrayIndex( j );
            settings.setValue( "key", nameTable.rows[ j ].key );
            settings.setValue( "name", nameTable.rows[ j ].name );
        }
        settings.endArray();
    }
    settings.endArray();
    settings.endGroup();
}

void NamingGroup::retrieveFromStorage( QSettings& settings )
{
    rules_.clear();
    tables_.clear();

    if ( !settings.contains( "NamingGroup/version" ) ) {
        return;
    }

    settings.beginGroup( "NamingGroup" );
    if ( settings.value( "version" ).toInt() <= NamingGroup_VERSION ) {
        name_ = settings.value( "name" ).toString();
        id_ = settings.value( "id", newGroupId() ).toString();

        const int ruleCount = settings.beginReadArray( "rules" );
        rules_.reserve( ruleCount );
        for ( int i = 0; i < ruleCount; ++i ) {
            settings.setArrayIndex( i );
            NamingRule rule;
            rule.name = settings.value( "name" ).toString();
            rule.pattern = settings.value( "regex" ).toString();
            rule.displayTemplate
                = settings.value( "template", NamingRule::defaultTemplate() ).toString();
            const int groupCount = settings.beginReadArray( "groups" );
            for ( int j = 0; j < groupCount; ++j ) {
                settings.setArrayIndex( j );
                rule.groupTables.append( { settings.value( "group" ).toString(),
                                           settings.value( "table" ).toString() } );
            }
            settings.endArray();
            rules_.append( std::move( rule ) );
        }
        settings.endArray();

        const int tableCount = settings.beginReadArray( "tables" );
        tables_.reserve( tableCount );
        for ( int i = 0; i < tableCount; ++i ) {
            settings.setArrayIndex( i );
            NameTable nameTable;
            nameTable.name = settings.value( "name" ).toString();
            nameTable.caseSensitive = settings.value( "caseSensitive", false ).toBool();
            const int rowCount = settings.beginReadArray( "rows" );
            nameTable.rows.reserve( rowCount );
            for ( int j = 0; j < rowCount; ++j ) {
                settings.setArrayIndex( j );
                nameTable.rows.append(
                    { settings.value( "key" ).toString(), settings.value( "name" ).toString() } );
            }
            settings.endArray();
            tables_.append( std::move( nameTable ) );
        }
        settings.endArray();
    }
    else {
        LOG_ERROR << "Unknown NamingGroup version, ignoring";
    }
    settings.endGroup();
}

} // namespace logsquirl::valuenames
