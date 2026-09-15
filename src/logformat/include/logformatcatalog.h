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
#include <QString>
#include <QStringList>

#include <memory>

// The Log Format Catalog: every Log Format available to choose from, built in
// and supplied by the user. A user's Log Format replaces a built-in one of the
// same name.
//
// One Catalog serves the whole application. It is built where the Settings
// Policies are derived and handed to whoever needs it; it is not a singleton
// and has no ambient accessor.
//
// Log Formats are held as shared, immutable objects. Rebuilding the Catalog
// parses every definition again into new objects, so a Log Format handed out
// before a rebuild stays alive and unchanged for as long as someone holds it:
// an open Log File keeps the Log Format it was recognized with.
class LogFormatCatalog {
public:
    using Formats = QHash<QString, std::shared_ptr<const LogFormatDefinition>>;

    // A Catalog whose rebuild() reads the built-in Log Formats only.
    LogFormatCatalog() = default;

    // A Catalog whose rebuild() reads the built-in Log Formats, then the ones
    // in userFormatsDirectory, which replace built-in ones of the same name.
    explicit LogFormatCatalog( QString userFormatsDirectory );

    // Where the application keeps the user's Log Formats:
    // ~/.local/share/logsquirl/formats/ on Linux,
    // ~/Library/Application Support/LogSquirl/formats/ on macOS,
    // %APPDATA%/LogSquirl/formats/ on Windows. Empty if there is none.
    static QString defaultUserFormatsDirectory();

    // Forget every Log Format and read them all again: the built-in ones,
    // then the user's. Log Formats handed out before stay valid.
    void rebuild();

    // Load all .json files from a directory and add their formats.
    // Later calls override formats with the same name from earlier calls.
    void loadFromDirectory( const QString& directoryPath );

    // Add a single format definition. Overwrites any existing format with the same name.
    void addFormat( LogFormatDefinition format );

    // Look up a format by its symbolic name. Returns nullptr if not found.
    std::shared_ptr<const LogFormatDefinition> formatByName( const QString& name ) const;

    // Number of loaded formats.
    int formatCount() const;

    // List all loaded format names.
    QStringList formatNames() const;

    // Access all loaded formats (for iteration by Format Recognition).
    const Formats& allFormats() const
    {
        return formats_;
    }

    // Load built-in formats embedded in the Qt resource system (:/formats/*.json).
    void loadBuiltinFormats();

private:
    QString userFormatsDirectory_;
    Formats formats_;
};
