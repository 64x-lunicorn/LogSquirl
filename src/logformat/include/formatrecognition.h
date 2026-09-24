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

#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "settingspolicies.h"

#include <QStringList>

#include <memory>

class AbstractLogData;

// Format Recognition: the decision which Log Format, if any, applies to a
// Log File, taken from its first Log Lines against the Log Format Catalog.
//
// The answer is one of the Catalog's own Log Formats -- the very object the
// Catalog holds, never a copy -- or nullptr. A disabled Recognition Policy
// answers nullptr without looking at a single Log Line.
//
// Among the Log Formats that match, the one matching the most sample lines
// wins, and on a tie the more specific one (more capture groups, or more
// fields for a JSON format); at least half of the sample lines must match.
//
// The kinds of Log Format are scored apart: a sample line that parses as a
// JSON object is scored only against JSON formats, which it matches when it
// contains the format's timestamp field; every other line only against regex
// formats. A Catalog without a JSON format scores every line as before.
//
// A logfmt format is scored on its own: a sample line counts for it when it
// reads completely as key/value pairs and contains the format's timestamp
// field as a key (a JSON object never does). A regex or JSON format keeps
// precedence: a logfmt format is chosen only when none of them would have been.
namespace FormatRecognition {

// How many Log Lines, from the first one, Format Recognition looks at.
inline constexpr int SampleDepth = 50;

// Recognizes the Log Format of a Log File from its first SampleDepth Log Lines.
std::shared_ptr<const LogFormatDefinition> recognize( const AbstractLogData& logFile,
                                                      const RecognitionPolicy& policy,
                                                      const LogFormatCatalog& catalog );

// Recognizes a Log Format from sample Log Lines already read; only the first
// SampleDepth of them are looked at.
std::shared_ptr<const LogFormatDefinition> recognize( const QStringList& firstLogLines,
                                                      const RecognitionPolicy& policy,
                                                      const LogFormatCatalog& catalog );

} // namespace FormatRecognition
