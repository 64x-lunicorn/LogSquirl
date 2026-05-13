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

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <list>
#include <unordered_map>

// Represents the result of extracting fields from a single log line.
class ExtractedFields {
  public:
    ExtractedFields() = default;

    // Whether the extraction matched (i.e., at least one regex pattern matched the line).
    bool isValid() const { return valid_; }
    void setValid( bool valid ) { valid_ = valid; }

    // Get the value of a named field. Returns empty string if field not found.
    QString value( const QString& fieldName ) const
    {
        return fields_.value( fieldName );
    }

    // Set a field value.
    void setValue( const QString& fieldName, const QString& value )
    {
        fields_[ fieldName ] = value;
    }

    // All field names present in this extraction.
    QStringList fieldNames() const { return QStringList( fields_.keys() ); }

  private:
    bool valid_ = false;
    QHash<QString, QString> fields_;
};

// Extracts structured fields from raw log lines using a format definition's regex patterns.
// Includes an LRU cache keyed by line number for efficient re-access during scrolling.
class LogFieldExtractor {
  public:
    // Construct an extractor for the given format definition.
    // cacheCapacity controls the LRU cache size (number of lines cached).
    explicit LogFieldExtractor( const LogFormatDefinition& format,
                                int cacheCapacity = 10000 );

    // Extract fields from a raw line.
    // lineNumber is used as the cache key (use -1 or omit for uncached extraction).
    ExtractedFields extractFields( const QString& line, int64_t lineNumber = -1 );

    // Get the ordered list of column names for table display.
    // Order: timestamp, level, [value fields ordered by definition], body
    QStringList columnNames() const;

    // Clear the LRU cache (e.g., when file changes).
    void invalidateCache();

  private:
    // Extract without caching (always runs regex).
    ExtractedFields doExtract( const QString& line ) const;

    const LogFormatDefinition& format_;
    QVector<QRegularExpression> compiledPatterns_;
    int cacheCapacity_;

    // LRU cache: line number -> extracted fields
    // Using a list for O(1) move-to-front and a map for O(1) lookup.
    using CacheList = std::list<std::pair<int64_t, ExtractedFields>>;
    CacheList cacheList_;
    std::unordered_map<int64_t, CacheList::iterator> cacheMap_;
};
