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

#include <QList>
#include <QString>

class QSettings;

// What Value Names are configured with (#647): Naming Groups holding Naming
// Rules and the Name Tables those rules use. Plain values, edited by the
// dialog and handed to a ValueNamer, which compiles them.
namespace logsquirl::valuenames {

// One row of a Name Table: a key regex, anchored to the whole captured
// value, and the name it gives that value. The name may use the key's
// capture groups as {1}, {2}, ...
struct NameRow {
    QString key;
    QString name;

    bool operator==( const NameRow& ) const = default;
};

// Rows of key regex -> name. The first row whose key matches wins. Keys
// ignore case unless the table is case-sensitive.
struct NameTable {
    QString name;
    QList<NameRow> rows;
    bool caseSensitive = false;

    bool operator==( const NameTable& ) const = default;
};

// Which Name Table one capture group of a Naming Rule uses. The group is
// given by its number ("1", "2", ...) or, for a named group, by its name;
// "0" is the whole match, which a rule without capture groups looks up.
struct GroupTable {
    QString group;
    QString table;

    bool operator==( const GroupTable& ) const = default;
};

// A regex plus, per capture group, the Name Table it uses, plus how a named
// value is shown. A capture group without a table stays as it is.
struct NamingRule {
    static QString defaultTemplate()
    {
        return QStringLiteral( "{name}({value})" );
    }

    QString name;
    QString pattern;
    QList<GroupTable> groupTables;
    // Shows a named value; {name} and {value} are its only placeholders.
    QString displayTemplate = defaultTemplate();
    // Whether the rule is checked. Not part of the group's file: the checks
    // are kept apart from the groups, as the Predefined Filters' are.
    bool enabled = true;

    // The table the given capture group uses, empty for none.
    QString tableFor( const QString& group ) const;

    bool operator==( const NamingRule& ) const = default;
};

// A named group of Naming Rules and the Name Tables they use; the unit that
// is enabled, exported and shared. A rule only ever uses the tables of its
// own group, found by name.
class NamingGroup {
public:
    // A new group under a freshly generated id.
    static NamingGroup createNewGroup( const QString& name );

    NamingGroup() = default;

    QString id() const
    {
        return id_;
    }

    // A copy of this group under the given id: a group that arrives from
    // elsewhere takes the id of the one it replaces, or a fresh one.
    NamingGroup withId( const QString& id ) const;

    // A copy of this group with the group and every rule checked, as
    // everything new is.
    NamingGroup withEverythingChecked() const;

    QString name() const
    {
        return name_;
    }
    void setName( const QString& name )
    {
        name_ = name;
    }

    const QList<NamingRule>& rules() const
    {
        return rules_;
    }
    void setRules( const QList<NamingRule>& rules )
    {
        rules_ = rules;
    }

    const QList<NameTable>& tables() const
    {
        return tables_;
    }
    void setTables( const QList<NameTable>& tables )
    {
        tables_ = tables;
    }

    // The first table of this group of that name, nullptr if there is none.
    const NameTable* table( const QString& name ) const;

    // Whether the group is checked. Like a rule's check, not part of its file.
    bool isEnabled() const
    {
        return enabled_;
    }
    void setEnabled( bool enabled )
    {
        enabled_ = enabled;
    }

    // Equal in everything, the checks included.
    bool operator==( const NamingGroup& ) const = default;

    // Whether both have the same name, rules and tables, whatever is checked.
    // The id is not compared.
    bool sameAs( const NamingGroup& other ) const;

    // Reads/writes the group -- its id, name, rules and tables, not the
    // checks -- in the QSettings object passed, under "NamingGroup".
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

    // The newest version of the storage this one reads: a group stored by a
    // later one is not read.
    static constexpr int newestStorageVersion()
    {
        return NamingGroup_VERSION;
    }

private:
    static constexpr int NamingGroup_VERSION = 1;

    QString id_;
    QString name_;
    QList<NamingRule> rules_;
    QList<NameTable> tables_;
    bool enabled_ = true;
};

} // namespace logsquirl::valuenames
