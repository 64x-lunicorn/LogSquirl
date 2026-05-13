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
#include <QStringList>
#include <QVector>

// Represents one value/field definition from a lnav format's "value" section.
struct LogFormatValueDef {
    QString kind = "string";
    bool identifier = false;
    bool hidden = false;
};

// Represents one sample line from the format's "sample" section.
struct LogFormatSample {
    QString line;
    QString level; // optional expected level
};

// Represents a single parsed log format definition (one entry from a lnav JSON file).
// Holds metadata, regex patterns, field definitions, and sample lines.
class LogFormatDefinition {
  public:
    LogFormatDefinition() = default;

    // Format symbolic name (JSON key, e.g. "syslog_log")
    const QString& name() const { return name_; }
    void setName( const QString& name ) { name_ = name; }

    // Human-readable title
    const QString& title() const { return title_; }
    void setTitle( const QString& title ) { title_ = title; }

    // Description
    const QString& description() const { return description_; }
    void setDescription( const QString& desc ) { description_ = desc; }

    // Regex patterns: key = pattern name (e.g. "basic"), value = PCRE2 pattern string
    const QHash<QString, QString>& regexPatterns() const { return regexPatterns_; }
    void setRegexPatterns( const QHash<QString, QString>& patterns ) { regexPatterns_ = patterns; }

    // Special field names (defaults follow lnav conventions)
    const QString& timestampField() const { return timestampField_; }
    void setTimestampField( const QString& field ) { timestampField_ = field; }

    const QString& levelField() const { return levelField_; }
    void setLevelField( const QString& field ) { levelField_ = field; }

    const QString& bodyField() const { return bodyField_; }
    void setBodyField( const QString& field ) { bodyField_ = field; }

    const QString& threadIdField() const { return threadIdField_; }
    void setThreadIdField( const QString& field ) { threadIdField_ = field; }

    const QString& opidField() const { return opidField_; }
    void setOpidField( const QString& field ) { opidField_ = field; }

    // Level string mappings: key = canonical level (e.g. "error"), value = regex/string to match
    const QHash<QString, QString>& levelMappings() const { return levelMappings_; }
    void setLevelMappings( const QHash<QString, QString>& mappings )
    {
        levelMappings_ = mappings;
    }

    // Value/field definitions from the "value" section
    const QHash<QString, LogFormatValueDef>& valueDefinitions() const
    {
        return valueDefinitions_;
    }
    void setValueDefinitions( const QHash<QString, LogFormatValueDef>& defs )
    {
        valueDefinitions_ = defs;
    }

    // Ordered list of value field names preserving JSON insertion order
    const QStringList& valueFieldOrder() const { return valueFieldOrder_; }
    void setValueFieldOrder( const QStringList& order ) { valueFieldOrder_ = order; }

    // Sample lines used for validation and specificity testing
    const QVector<LogFormatSample>& sampleLines() const { return sampleLines_; }
    void setSampleLines( const QVector<LogFormatSample>& samples ) { sampleLines_ = samples; }

    // File-pattern: regex to match log file paths (optional optimization)
    const QString& filePattern() const { return filePattern_; }
    void setFilePattern( const QString& pattern ) { filePattern_ = pattern; }

    // Timestamp format strings (strftime-like, optional)
    const QStringList& timestampFormats() const { return timestampFormats_; }
    void setTimestampFormats( const QStringList& formats ) { timestampFormats_ = formats; }

    // Whether the format's messages are ordered by time
    bool orderedByTime() const { return orderedByTime_; }
    void setOrderedByTime( bool ordered ) { orderedByTime_ = ordered; }

  private:
    QString name_;
    QString title_;
    QString description_;

    QHash<QString, QString> regexPatterns_;

    QString timestampField_ = "timestamp";
    QString levelField_ = "level";
    QString bodyField_ = "body";
    QString threadIdField_;
    QString opidField_;

    QHash<QString, QString> levelMappings_;
    QHash<QString, LogFormatValueDef> valueDefinitions_;
    QStringList valueFieldOrder_;
    QVector<LogFormatSample> sampleLines_;

    QString filePattern_;
    QStringList timestampFormats_;
    bool orderedByTime_ = true;
};
