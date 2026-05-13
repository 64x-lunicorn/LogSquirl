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

#include <QVector>

// Parses lnav-compatible JSON format definition files.
// Supports the subset of fields needed by LogSquirl:
//   regex, value, timestamp-field, level-field, body-field,
//   level, sample, file-pattern, timestamp-format, ordered-by-time,
//   thread-id-field, opid-field, title, description
class LogFormatParser {
  public:
    // Parse a JSON string that may contain one or more format definitions.
    // Returns a list of successfully parsed formats.
    // Keys like "$schema" are ignored.
    // Formats without a "regex" section are skipped.
    static QVector<LogFormatDefinition> parseJsonString( const char* jsonString );

    // Parse a JSON file on disk.
    // Returns a list of successfully parsed formats, or empty on error.
    static QVector<LogFormatDefinition> parseFile( const QString& filePath );
};
