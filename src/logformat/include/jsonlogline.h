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

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

// Reading a Log Line that is a JSON object, for Log Formats of the kind Json.
// One Log Line is one JSON object; anything else (a truncated line, plain
// text, an array) is not a JSON Log Line.
namespace JsonLogLine {

// The JSON object a Log Line is, or none. Cheap for a line that does not start
// with a "{".
std::optional<QJsonObject> parse( const QString& line );

// The value a field path addresses: "src/file" is the member "file" of the
// member "src". A member that is literally named like the whole path wins. A
// missing member is an undefined value.
QJsonValue valueAt( const QJsonObject& object, const QString& path );

// The text of a value as a table cell: strings as they are, numbers without
// an exponent for whole values, booleans as true/false. A missing value, null,
// an array and an object are empty.
QString cellText( const QJsonValue& value );

// The point in time an epoch value is, given what it is divided by to get
// seconds (1000 for milliseconds). Held as UTC clock time, like a Timestamp.
QDateTime fromEpoch( double value, double divisor );

// An epoch value as it is shown in a cell: "yyyy-MM-dd HH:mm:ss.zzz".
QString epochCellText( double value, double divisor );

} // namespace JsonLogLine
