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

#include "groupedits.h"

#include <QCoreApplication>

#include <utility>

namespace logsquirl::valuenames {

QString freeName( const QString& name, const QStringList& taken )
{
    if ( !taken.contains( name ) ) {
        return name;
    }
    for ( int n = 2;; ++n ) {
        const auto candidate = QStringLiteral( "%1 (%2)" ).arg( name ).arg( n );
        if ( !taken.contains( candidate ) ) {
            return candidate;
        }
    }
}

QString newTableName( const QStringList& taken )
{
    for ( int number = 1;; ++number ) {
        const auto name
            = QCoreApplication::translate( "GroupEdits", "Name Table %1" ).arg( number );
        if ( !taken.contains( name ) ) {
            return name;
        }
    }
}

namespace {

template <typename Item>
QStringList namesOf( const QList<Item>& items, qsizetype except )
{
    QStringList names;
    for ( qsizetype index = 0; index < items.size(); ++index ) {
        if ( index != except ) {
            names.append( items[ index ].name );
        }
    }
    return names;
}

} // namespace

QStringList ruleNames( const QList<NamingRule>& rules, qsizetype except )
{
    return namesOf( rules, except );
}

QStringList tableNames( const QList<NameTable>& tables, qsizetype except )
{
    return namesOf( tables, except );
}

QString addRule( NamingGroup& group, NamingRule rule, const QString& fallbackName )
{
    const auto name = rule.name.trimmed();
    rule.name = freeName( name.isEmpty() ? fallbackName : name, ruleNames( group.rules() ) );
    auto rules = group.rules();
    rules.append( rule );
    group.setRules( rules );
    return rules.back().name;
}

QString addTable( NamingGroup& group, NameTable table )
{
    const auto taken = tableNames( group.tables() );
    const auto name = table.name.trimmed();
    table.name = name.isEmpty() ? newTableName( taken ) : freeName( name, taken );
    auto tables = group.tables();
    tables.append( table );
    group.setTables( tables );
    return tables.back().name;
}

AddedRuleWithTable addRuleWithTable( NamingGroup& group, RuleWithTable added,
                                     const QString& ruleFallbackName )
{
    AddedRuleWithTable names;

    const auto existing = tableNames( group.tables() ).indexOf( added.table.name );
    if ( added.existingTable && existing >= 0 ) {
        auto tables = group.tables();
        tables[ existing ].rows = added.table.rows;
        tables[ existing ].caseSensitive = added.table.caseSensitive;
        group.setTables( tables );
        names.table = added.table.name;
    }
    else {
        names.table = addTable( group, std::move( added.table ) );
    }

    auto& groupTables = added.rule.groupTables;
    groupTables.removeIf(
        [ & ]( const GroupTable& groupTable ) { return groupTable.group == added.captureGroup; } );
    groupTables.append( GroupTable{ added.captureGroup, names.table } );
    names.rule = addRule( group, std::move( added.rule ), ruleFallbackName );
    return names;
}

} // namespace logsquirl::valuenames
