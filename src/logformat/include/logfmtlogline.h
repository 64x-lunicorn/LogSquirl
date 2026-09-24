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

#include <QHash>
#include <QString>

#include <optional>

// Reading a Log Line that is logfmt, for Log Formats of the kind Logfmt: a
// sequence of key/value pairs separated by whitespace.
//
// A value is bare (`port=8080`, up to the next whitespace) or double-quoted
// (`msg="said \"hi\""`), and a quoted value may hold spaces and the escapes
// \" and \\. A key without "=" is a key with an empty value. A Log Line that
// does not read completely that way -- an unterminated quote, a pair without a
// key, a key that holds a quote -- is not a logfmt Log Line, and neither is a
// blank one or a JSON object.
namespace LogfmtLogLine {

// The pairs a Log Line is, or none. When a key occurs twice the last wins.
std::optional<QHash<QString, QString>> parse( const QString& line );

} // namespace LogfmtLogLine
