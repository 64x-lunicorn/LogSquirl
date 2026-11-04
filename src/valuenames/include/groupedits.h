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

#include <QString>
#include <QStringList>

#include "naminggroup.h"

// How a Naming Rule and a Name Table are added to a Naming Group: what the
// Value Names dialog's Add buttons and "Name this value..." both do (#883).
namespace logsquirl::valuenames {

// The name itself when none of taken has it, else the first free
// "<name> (n)", n from 2. Every name kept unique is made so with it: a
// rule's, a table's, a Naming Group's, and an imported Filter Group's or
// Highlighter Set's.
QString freeName( const QString& name, const QStringList& taken );

// The name of a new Name Table without a better one: "Name Table 1",
// "Name Table 2", ..., the first none of taken has.
QString newTableName( const QStringList& taken );

// The names of the rules and of the tables, but the one at the index except
// (none when it is -1): the names an edited one must not take.
QStringList ruleNames( const QList<NamingRule>& rules, qsizetype except = -1 );
QStringList tableNames( const QList<NameTable>& tables, qsizetype except = -1 );

// Adds the rule after the group's rules, under its name -- fallbackName when
// it has none -- made unique in the group: a rule's check is kept by its
// name. Returns the name it got.
QString addRule( NamingGroup& group, NamingRule rule, const QString& fallbackName );

// Adds the table after the group's tables, under its name -- newTableName()
// when it has none -- made unique in the group: the rules find their tables
// by name. Returns the name it got.
QString addTable( NamingGroup& group, NameTable table );

// A Naming Rule and the Name Table one of its capture groups uses.
struct RuleWithTable {
    NamingRule rule;
    // The capture group that uses the table, as GroupTable::group gives it.
    QString captureGroup = QStringLiteral( "1" );
    NameTable table;
    // Whether table is the group's table of that name, edited: its rows and
    // case sensitivity replace that table's. Otherwise, or when the group has
    // no table of that name, it is a new table.
    bool existingTable = false;
};

// The names the rule and the table got.
struct AddedRuleWithTable {
    QString rule;
    QString table;
};

// Adds the rule, and the table unless it is one the group has, to the group,
// the rule's capture group using the table under the name the table got. The
// rule's other capture groups keep the tables they use. A rule without a
// name gets ruleFallbackName, a new table without one newTableName().
AddedRuleWithTable addRuleWithTable( NamingGroup& group, RuleWithTable added,
                                     const QString& ruleFallbackName );

} // namespace logsquirl::valuenames
