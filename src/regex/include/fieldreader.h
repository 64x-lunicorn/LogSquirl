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
#include <utility>

#include <QString>
#include <QStringList>

// Reads the fields of a Log Line for the Field Terms of a logical
// combination (#798): a sub-pattern written `Name:"pattern"` is applied to
// the text of the field named, not to the whole line.
//
// Declared here, beside the matcher, so that the regex library stays
// ignorant of Log Formats: the logformat library implements it over the
// Log Format (LogFormatFieldReader), agreeing with what the Table View's
// cells show, and the pattern a Search is requested with carries one
// (RegularExpressionPattern::fieldReader). The matcher reads a line's
// fields once for all the Field Terms of a combination.
//
// Immutable once built, and read from the Search's worker threads: every
// method is const and keeps no state, so one reader may serve several
// matchers at once.
class FieldReader {
public:
    virtual ~FieldReader() = default;

    // What tells one reader's Log Format from another's: the same pattern
    // reads differently under another Log Format, so a Search's cached
    // results are keyed on it too.
    virtual QString identity() const = 0;

    // The fields a Field Term may name, in the order readFields() gives
    // their text.
    virtual QStringList fieldNames() const = 0;
    // The index of the field named, none when the Log Format has no such
    // field.
    virtual std::optional<int> fieldIndex( const QString& name ) const = 0;

    // The text of every field of the line, one entry per field name, in
    // fields: a field the line has no value for is empty. False for a line
    // the Log Format does not parse, which has no fields at all; fields is
    // then not to be read.
    virtual bool readFields( const QString& line, QStringList& fields ) const = 0;

    // Where the field's text lies in the line, from the first character up
    // to, not including, the one after the last: for whoever marks what a
    // Field Term matched in the line (the Regex Lab). None for a line the
    // Log Format does not parse, a field with no text in the line, or a Log
    // Format that places its fields nowhere in the line (JSON, logfmt).
    virtual std::optional<std::pair<qsizetype, qsizetype>> fieldSpan( const QString& line,
                                                                      int field ) const = 0;
};
