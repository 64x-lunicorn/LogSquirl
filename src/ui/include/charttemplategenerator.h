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

#include <QVector>

#include "chartseries.h"

class LogFormatDefinition;

// Generates pre-configured ChartSeriesDefinition templates from a detected
// log format.  The templates use the format's regex patterns, timestamp
// field, level mappings, and value definitions so the user can add
// chart series with a single click instead of manually writing regex.
//
// Template categories:
//   - Log Level Frequency   (count-mode per canonical level)
//   - Message Rate           (count all matching lines over time)
//   - Numeric Field Values   (extract integer/float fields)
//   - Field Occurrence       (count lines containing each field)
namespace ChartTemplateGenerator {

// Convert a strftime/lnav timestamp format string to Qt's QDateTime format.
// E.g. "%Y-%m-%d %H:%M:%S.%L" → "yyyy-MM-dd HH:mm:ss.zzz"
// Returns an empty string for formats that cannot be converted (e.g. epoch "%s").
QString strftimeToQtFormat( const QString& strftimeFmt );

// Pick the first regex pattern from the format that contains the given
// named capture group.  Returns the full pattern string, or empty if none
// matches.
QString patternContainingGroup( const LogFormatDefinition& format,
                                const QString& groupName );

// Find the numeric capture-group index for a named group within a compiled
// QRegularExpression.  Returns -1 if not found.
int namedGroupIndex( const QString& pattern, const QString& groupName );

// Generate "Log Level Frequency" series — one count-mode series per
// canonical level known to the format (error, warning, notice, …).
// Each series uses the level's regex from levelMappings().
// When |bucketMs| > 0 and a timestamp pattern is available, the X-axis
// is configured for timestamp bucketing.
QVector<ChartSeriesDefinition> levelFrequencyTemplates(
    const LogFormatDefinition& format, qint64 bucketMs = 1000 );

// Generate a single "All Levels" series that counts every line matching
// the format's first regex pattern.  Useful as a quick message-rate chart.
QVector<ChartSeriesDefinition> messageRateTemplates(
    const LogFormatDefinition& format, qint64 bucketMs = 1000 );

// Generate one series per numeric field (kind == "integer" or "float")
// that extracts the field's value.  X-axis uses timestamp if available.
QVector<ChartSeriesDefinition> numericFieldTemplates(
    const LogFormatDefinition& format );

// Generate one count-mode series per non-hidden field.
QVector<ChartSeriesDefinition> fieldOccurrenceTemplates(
    const LogFormatDefinition& format, qint64 bucketMs = 1000 );

} // namespace ChartTemplateGenerator
