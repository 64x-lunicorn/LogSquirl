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
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <optional>

// Represents the result of extracting fields from a single log line.
class ExtractedFields {
public:
    ExtractedFields() = default;

    // Whether the extraction matched (i.e., at least one regex pattern matched the line).
    bool isValid() const
    {
        return valid_;
    }
    void setValid( bool valid )
    {
        valid_ = valid;
    }

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
    QStringList fieldNames() const
    {
        return QStringList( fields_.keys() );
    }

private:
    bool valid_ = false;
    QHash<QString, QString> fields_;
};

// Extracts structured fields from raw log lines: a Regex format by its named
// capture groups, a Json format by the path of each field in the JSON object,
// a Logfmt format by the key of each field.
// It keeps no cache of its own: the table model caches the Rows it shows.
//
// For a Json or Logfmt format every Log Line is valid, so it is a Row; one that
// is not a JSON object (not logfmt) has all its fields empty. A key a Logfmt
// Log Line lacks is an empty field, and a key the format does not declare is
// ignored. A numeric timestamp field is shown as
// the point in time it is (through the timestamp divisor), UTC.
class LogFieldExtractor {
public:
    // Construct an extractor for the given format definition, which must
    // outlive the extractor.
    explicit LogFieldExtractor( const LogFormatDefinition& format );

    // Extract fields from a raw line (always runs the regex or parses the JSON).
    ExtractedFields extractFields( const QString& line ) const;

    // Extract one field of a raw line: the value extractFields() gives for it,
    // or nothing when extractFields() calls the line invalid. For a Json or
    // Logfmt format only that key is read, not every field of the line.
    std::optional<QString> extractField( const QString& line, const QString& fieldName ) const;

    // Get the ordered list of column names for table display.
    // Order: timestamp, level, [value fields ordered by definition], body
    QStringList columnNames() const;

private:
    QString jsonFieldText( const QJsonObject& object, const QString& field ) const;
    ExtractedFields extractRegexFields( const QString& line ) const;
    ExtractedFields extractJsonFields( const QString& line ) const;
    ExtractedFields extractLogfmtFields( const QString& line ) const;

    // A pattern with its named capture groups, looked up once when the
    // extractor is built rather than for every line.
    struct CompiledPattern {
        QRegularExpression regex;
        // Each named group and its index; index -1 for a name that several
        // groups share, which is then captured by name.
        QVector<QPair<QString, int>> namedGroups;
    };

    const LogFormatDefinition& format_;
    QVector<CompiledPattern> compiledPatterns_;
    // Json or Logfmt format: the fields (paths, keys) to read from each Log Line
    QStringList keyedFields_;
};
