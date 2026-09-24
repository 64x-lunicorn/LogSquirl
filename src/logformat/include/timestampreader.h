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

#include "logformatdefinition.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>

// Reads the Timestamp of a Log Line: the point in time it carries in its Log
// Format's timestamp field. Built once per Log Format, which compiles the
// Log Format's patterns and timestamp formats then, not per Log Line.
//
// The Timestamp is read with the timestamp-format alternatives the Log Format
// declares (strftime directives: %Y %y %m %d %e %H %M %S %L %f %b %B %a %A
// %z %Z %s %%), the first that fits winning; epoch values ("%s") are divided
// by the Log Format's timestamp-divisor. A Log Format that declares none has
// the common formats tried in turn: ISO 8601 with a "T" or a space, syslog,
// Common Log Format, glog and a few more.
//
// A Timestamp is compared as written: what has no time zone is not
// converted, and a zone that is written ("Z", "+0100", "PDT") is read and
// ignored. The result holds the clock time as if it were UTC, only so that
// two Timestamps compare; it is not the instant the Log Line happened. A
// format without a year takes the reference year, one without a date takes
// 1970-01-01.
class TimestampReader {
public:
    // The reference year is the one a format without a year gets; 0 is the
    // current year.
    explicit TimestampReader( const LogFormatDefinition& format, int referenceYear = 0 );
    ~TimestampReader();
    TimestampReader( TimestampReader&& ) noexcept;
    TimestampReader& operator=( TimestampReader&& ) noexcept;

    // Whether the Log Format has a timestamp field in one of its patterns;
    // when not, there is no Timestamp to read. Cheap: nothing is compiled.
    static bool isAvailableFor( const LogFormatDefinition& format );
    bool isAvailable() const;

    // The Timestamp of a Log Line, or none: for a line that does not match
    // the Log Format (a continuation line, a stack trace) or whose timestamp
    // field is in a format this reader does not know.
    std::optional<QDateTime> timestampOf( const QString& line ) const;

    // The Timestamp written in the text of a timestamp field (the field only,
    // not a whole Log Line).
    std::optional<QDateTime> parseField( QStringView text ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
