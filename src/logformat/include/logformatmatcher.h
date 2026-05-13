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
#include "logformatregistry.h"

#include <QStringList>

// Detects which log format (if any) matches a set of sample lines from a file.
// Uses specificity-based ordering: a format that matches only its own lines
// is preferred over one that matches everything.
class LogFormatMatcher {
  public:
    // Construct a matcher backed by the given registry.
    explicit LogFormatMatcher( const LogFormatRegistry& registry );

    // Try to detect a format from the given lines (typically the first ~1000 lines of a file).
    // Returns a pointer to the best matching format, or nullptr if no format matches
    // above the minimum threshold (50% of lines must match).
    const LogFormatDefinition* detectFormat( const QStringList& lines ) const;

  private:
    const LogFormatRegistry& registry_;
};
