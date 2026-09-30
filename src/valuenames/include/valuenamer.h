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
#include <QStringView>

#include <memory>

#include "naminggroup.h"

namespace logsquirl::valuenames {

// One value of a Log Line shown with its name: the raw text it stands for,
// in the columns of the Log Line, what is shown in its place, and what the
// tooltip tells about it.
struct NamedValue {
    // The raw range [start, start + length) of the Log Line.
    qsizetype start = 0;
    qsizetype length = 0;
    // What is shown instead of the raw range: the rule's template filled in.
    QString shown;

    // The raw value, the name it got, and where the name came from.
    QString value;
    QString name;
    QString table;
    QString rule;
    QString group;

    qsizetype end() const
    {
        return start + length;
    }

    bool operator==( const NamedValue& ) const = default;
};

// The Naming Rules of a set of Naming Groups, compiled once, which give the
// Named Values of a Log Line.
//
// Every match of every enabled rule is looked at, and every rule sees the
// raw text only. On overlap the earlier rule wins in the order the groups
// were handed over (the sidebar's: Team groups last), then the order of the
// rules within a group; a later rule skips a range already taken. A rule
// whose regex does not compile, and a key that does not, are left out.
//
// Line breaks and control characters of a name or a template are shown as
// spaces: a Log Line is drawn on one line.
//
// Reentrant: copies share the compiled rules, which are never changed after
// construction, so const calls on copies may run on several threads at once.
class ValueNamer {
public:
    // A Log Line longer than this gets no names, as it gets no Highlighters:
    // matching it could hang the views.
    static constexpr qsizetype MaxLineLength = 1'000'000;

    // Names nothing.
    ValueNamer() = default;

    // The enabled rules of the enabled groups, in this order.
    explicit ValueNamer( const QList<NamingGroup>& groups );

    // Whether no rule could name anything -- none is enabled, compiles, or
    // gives a capture group a table with a usable row -- so a Log Line need
    // not be looked at. Constant time.
    bool isEmpty() const
    {
        return rules_ == nullptr;
    }

    // The Named Values of the Log Line, ordered by start and not overlapping.
    QList<NamedValue> namedValues( const QString& line ) const;

    struct CompiledRules;

private:
    std::shared_ptr<const CompiledRules> rules_;
};

// The Log Line as shown: every Named Value's raw range replaced by what it
// shows. namedValues are those ValueNamer::namedValues gave for line.
QString shownLine( QStringView line, const QList<NamedValue>& namedValues );

// The name a row gives a value its key matched: {1}, {2}, ... replaced by the
// key's capture groups. A reference to a group the key does not have stays
// as it is; one to a group that took no part in the match becomes empty.
// keyGroups[0] is the whole match, keyGroups[n] group n.
QString nameWithKeyGroups( const QString& name, const QStringList& keyGroups );

// A rule's template filled in: {name} and {value} replaced, in one pass, so
// that a name holding "{value}" stays as it is.
QString fillTemplate( const QString& displayTemplate, const QString& name, const QString& value );

// Whether a key is plain text that matches just itself: ASCII only, no blank
// and no regex metacharacter. A table of such keys is looked up by hash.
bool isLiteralKey( const QString& key );

// What two keys of a table are compared by to find a duplicate: a literal
// key case-folded unless the table is case-sensitive, any other key as it
// is written (\d and \D are different keys in any table).
QString keyIdentity( const QString& key, bool caseSensitive );

// Whether the text holds a line break or another control character, which a
// name or template must not bring into a Log Line.
bool hasControlCharacters( QStringView text );

// The text with every line break and control character replaced by a space.
QString withoutControlCharacters( QString text );

// --- Validation, for the edit dialog's warnings ---

struct Problem {
    enum class Kind {
        // A Naming Rule's regex does not compile. detail: the error.
        InvalidRuleRegex,
        // A key of a Name Table does not compile. detail: the error.
        InvalidKeyRegex,
        // A key a row before already has (ignoring case unless the table is
        // case-sensitive): the row is never used. firstRow: that row.
        DuplicateKey,
        // The name uses {n}, but the key has fewer groups. detail: "{n}".
        MissingKeyGroup,
        // A rule gives a capture group a table its group does not have.
        // detail: the table's name.
        UnknownTable,
        // A rule gives a table to a capture group its regex does not have.
        // detail: the group as given.
        UnknownCaptureGroup,
        // A rule gives a capture group a second table, by number and by name
        // or twice: only the first is used. detail: the group as given.
        DuplicateCaptureGroup,
        // A rule has the name of a rule before it in the group; the name is
        // what the rule's check is kept by. detail: the name.
        DuplicateRuleName,
        // A rule's template holds a line break or control character, shown
        // as a space. detail: the template.
        ControlCharacterInTemplate,
        // A row's name holds a line break or control character, shown as a
        // space. detail: the name.
        ControlCharacterInName,
    };

    Kind kind;
    // The rule or the table the problem is in, whichever it is about.
    QString rule;
    QString table;
    // The row of the table, 0-based, or -1.
    int row = -1;
    int firstRow = -1;
    QString detail;

    bool operator==( const Problem& ) const = default;
};

// Every problem of the group, rules first, then tables, each in order.
QList<Problem> validate( const NamingGroup& group );

} // namespace logsquirl::valuenames
