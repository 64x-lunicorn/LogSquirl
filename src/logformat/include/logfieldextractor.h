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

// Extracts structured fields from raw log lines using a format definition's regex patterns.
// It keeps no cache of its own: the table model caches the Rows it shows.
class LogFieldExtractor {
public:
    // Construct an extractor for the given format definition, which must
    // outlive the extractor.
    explicit LogFieldExtractor( const LogFormatDefinition& format );

    // Extract fields from a raw line (always runs the regex).
    ExtractedFields extractFields( const QString& line ) const;

    // Get the ordered list of column names for table display.
    // Order: timestamp, level, [value fields ordered by definition], body
    QStringList columnNames() const;

private:
    const LogFormatDefinition& format_;
    QVector<QRegularExpression> compiledPatterns_;
};
