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

QStringList ruleNames( const NamingGroup& group )
{
    QStringList names;
    for ( const auto& rule : group.rules() ) {
        names.append( rule.name );
    }
    return names;
}

QStringList tableNames( const NamingGroup& group )
{
    QStringList names;
    for ( const auto& table : group.tables() ) {
        names.append( table.name );
    }
    return names;
}

QString addRule( NamingGroup& group, NamingRule rule, const QString& fallbackName )
{
    const auto name = rule.name.trimmed();
    rule.name = freeName( name.isEmpty() ? fallbackName : name, ruleNames( group ) );
    auto rules = group.rules();
    rules.append( rule );
    group.setRules( rules );
    return rules.back().name;
}

QString addTable( NamingGroup& group, NameTable table, const QString& fallbackName )
{
    const auto name = table.name.trimmed();
    table.name = freeName( name.isEmpty() ? fallbackName : name, tableNames( group ) );
    auto tables = group.tables();
    tables.append( table );
    group.setTables( tables );
    return tables.back().name;
}

AddedRuleWithTable addRuleWithTable( NamingGroup& group, RuleWithTable added,
                                     const QString& ruleFallbackName,
                                     const QString& tableFallbackName )
{
    AddedRuleWithTable names;

    const auto existing = tableNames( group ).indexOf( added.table.name );
    if ( added.existingTable && existing >= 0 ) {
        auto tables = group.tables();
        tables[ existing ].rows = added.table.rows;
        tables[ existing ].caseSensitive = added.table.caseSensitive;
        group.setTables( tables );
        names.table = added.table.name;
    }
    else {
        names.table = addTable( group, std::move( added.table ), tableFallbackName );
    }

    auto& groupTables = added.rule.groupTables;
    groupTables.removeIf(
        [ & ]( const GroupTable& groupTable ) { return groupTable.group == added.captureGroup; } );
    groupTables.append( GroupTable{ added.captureGroup, names.table } );
    names.rule = addRule( group, std::move( added.rule ), ruleFallbackName );
    return names;
}

} // namespace logsquirl::valuenames
