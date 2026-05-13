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
#include <QStringList>

// Stores all loaded log format definitions and provides lookup by name.
// Formats loaded later override earlier ones with the same name,
// enabling user formats to override built-in ones.
class LogFormatRegistry {
  public:
    LogFormatRegistry() = default;

    // Load all .json files from a directory and add their formats.
    // Later calls override formats with the same name from earlier calls.
    void loadFromDirectory( const QString& directoryPath );

    // Add a single format definition. Overwrites any existing format with the same name.
    void addFormat( LogFormatDefinition format );

    // Look up a format by its symbolic name. Returns nullptr if not found.
    const LogFormatDefinition* formatByName( const QString& name ) const;

    // Number of loaded formats.
    int formatCount() const;

    // List all loaded format names.
    QStringList formatNames() const;

    // Access all loaded formats (for iteration by the matcher).
    const QHash<QString, LogFormatDefinition>& allFormats() const { return formats_; }

    // Load built-in formats embedded in the Qt resource system (:/formats/*.json).
    void loadBuiltinFormats();

    // Load user formats from the platform-specific user data directory.
    // Formats here override built-in ones with the same name.
    // Path: ~/.local/share/logsquirl/formats/ on Linux,
    //       ~/Library/Application Support/LogSquirl/formats/ on macOS,
    //       %APPDATA%/LogSquirl/formats/ on Windows.
    void loadUserFormats();

  private:
    QHash<QString, LogFormatDefinition> formats_;
};
