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

#include <optional>

#include <QString>
#include <QStringView>

// What "Name this value..." proposes for a value the user points at in a Log
// Line (#883): the Naming Rule's regex, the Name Table's name and the key the
// value is entered as.
namespace logsquirl::valuenames {

// A range [start, start + length) of a Log Line.
struct LineRange {
    qsizetype start = 0;
    qsizetype length = 0;

    qsizetype end() const
    {
        return start + length;
    }

    bool operator==( const LineRange& ) const = default;
};

// A value of a Log Line: a number, decimal, negative, with a fraction, or
// hexadecimal, with or without 0x; or a UUID (#957).
struct FoundValue {
    enum class Kind {
        // 42, also the 12 of 12ms
        Decimal,
        // 3.25
        Fraction,
        // 0x1F
        PrefixedHex,
        // 1f2e: hex digits, a letter and a digit among them
        BareHex,
        // 84f1fc54-cd08-51c3-af5b-3687900e5e6a: 8-4-4-4-12 hex digits
        Uuid,
    };

    LineRange range;
    Kind kind = Kind::Decimal;
    // Whether a minus sign before it belongs to it: -5, not the 5 of id-5.
    bool negative = false;

    bool operator==( const FoundValue& ) const = default;
};

// The value the text range of the line holds: the last one in it. A minus
// sign right before it, in the range or not, belongs to it when no letter or
// digit comes before the sign; a UUID the range holds a part of is all of
// it. None when the range holds no number and no UUID.
std::optional<FoundValue> valueIn( QStringView line, LineRange range );

// What the user points at when nothing is selected: the UUID at the column;
// else the word at the column if it holds a value; else, on a word without
// one, that word with the words '-' joins to it, a text value (#957) --
// user_id of user_id=5, read-only -- or the word alone when what '-' joins
// holds a number (req of req-42, whose digits a click names). On a separator,
// the run of non-blank text there if it holds a value, else the text value
// around it. None on a blank or a separator outside a value.
std::optional<LineRange> valueTextAt( QStringView line, qsizetype column );

// What "Name this value..." proposes for the value in the range of the line.
struct NamingProposal {
    // The value as the Log Line holds it, and where.
    QString value;
    LineRange valueRange;
    // The Naming Rule's regex: the text around the value, escaped, with the
    // value as its only capture group. The text before it is the range's;
    // when that holds no letter, the word before the value in the line, and
    // what separates them. A value without anything before it starts the
    // Log Line, ^.
    //
    // A text value is captured up to the character after it in the Log Line,
    // [^,]+ before a ',', or, at the end of the Log Line, to its end. When
    // the value holds that character, or ends within a word, it is captured
    // lazily, followed by what comes after it up to the end of the next word.
    QString pattern;
    // The Name Table's name: the last word with a letter before the value,
    // empty when there is none.
    QString tableName;
    // The key of the Name Table's row for the value: matches the value alone.
    QString key;
    // How the named value is shown.
    QString displayTemplate;
};

// The template "Name this value..." proposes: the name and the raw value.
inline QString proposedTemplate()
{
    return QStringLiteral( "{name} ({value})" );
}

// The value is the one valueIn() finds in the range; when it finds none, the
// range as it is, a text value (#957). None when the range is empty, not
// within the line, or only blanks.
std::optional<NamingProposal> proposeNaming( QStringView line, LineRange range );

// The text escaped so that, as a regex, it matches just itself; blanks,
// '=', ':' and the like stay as they are, a tab is written \t.
QString escapedForRegex( QStringView text );

} // namespace logsquirl::valuenames
