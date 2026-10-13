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
#include <vector>

// The Log Format Catalog: every Log Format available to choose from, built
// in, supplied by enabled plugins and supplied by the user. By name, a user's
// Log Format replaces a plugin's, and a plugin's a built-in one. Every Log
// Format carries its Source.
//
// One Catalog serves the whole application. It is built where the Settings
// Policies are derived and handed to whoever needs it; it is not a singleton
// and has no ambient accessor.
//
// The Catalog knows no plugin: whoever owns both the plugin layer and the
// Catalog hands it the Log Format files of the enabled plugins with
// setPluginFormats() -- an id to order by, a name to show and files to read
// -- and rebuilds it (#793).
//
// Log Formats are held as shared, immutable objects. Rebuilding the Catalog
// parses every definition again into new objects, so a Log Format handed out
// before a rebuild stays alive and unchanged for as long as someone holds it:
// an open Log File keeps the Log Format it was recognized with.
class LogFormatCatalog {
public:
    using Formats = QHash<QString, std::shared_ptr<const LogFormatDefinition>>;

    // The Log Format files one enabled plugin ships, as its manifest names
    // them (#793).
    struct PluginFormats {
        // Orders the plugins: between two naming the same Log Format, the one
        // whose id sorts first wins.
        QString id;
        // The plugin's display name, the Source its Log Formats show.
        QString name;
        // Absolute paths of the plugin's Log Format files.
        QStringList files;

        bool operator==( const PluginFormats& ) const = default;
    };

    // What went wrong with a plugin's Log Formats in the last rebuild: a file
    // that could not be read, which was skipped, or a Log Format another
    // plugin had already supplied. Every warning is also logged.
    struct PluginFormatWarning {
        QString pluginId;
        QString message;

        bool operator==( const PluginFormatWarning& ) const = default;
    };

    // A Catalog whose rebuild() reads the built-in Log Formats only.
    LogFormatCatalog() = default;

    // A Catalog whose rebuild() reads the built-in Log Formats, then the ones
    // in userFormatsDirectory, which replace built-in ones of the same name.
    explicit LogFormatCatalog( QString userFormatsDirectory );

    // Where the application keeps the user's Log Formats: `formats` in its
    // data directory (DataLocation), so ~/.local/share/logsquirl/formats/ on
    // Linux, ~/Library/Application Support/logsquirl/formats/ on macOS,
    // %APPDATA%/logsquirl/formats/ on Windows, and beside the executable in a
    // portable run. Empty if there is none.
    static QString defaultUserFormatsDirectory();

    // The Log Format files of the enabled plugins, read by every rebuild()
    // from now on, after the built-in Log Formats and before the user's. The
    // list replaces the one handed in before; an empty one means no plugin
    // supplies a Log Format. Does not rebuild.
    void setPluginFormats( std::vector<PluginFormats> plugins );

    // Forget every Log Format and read them all again: the built-in ones,
    // then the enabled plugins', then the user's. Log Formats handed out
    // before stay valid.
    void rebuild();

    // The warnings of the last rebuild about the plugins' Log Format files.
    const std::vector<PluginFormatWarning>& pluginFormatWarnings() const
    {
        return pluginFormatWarnings_;
    }

    // The messages of the last rebuild's warnings about one plugin's Log
    // Format files, in the order they arose; empty when there were none.
    QStringList pluginFormatWarnings( const QString& pluginId ) const;

    // Load all .json files from a directory and add their formats, with the
    // User Source. Later calls override formats with the same name from
    // earlier calls.
    void loadFromDirectory( const QString& directoryPath );

    // Add a single format definition with the given Source. Overwrites any
    // existing format with the same name.
    void addFormat( LogFormatDefinition format, LogFormatSource source = {} );

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
    // Reads the plugins' Log Format files, in id order, and records the
    // warnings.
    void loadPluginFormats();

    QString userFormatsDirectory_;
    std::vector<PluginFormats> pluginFormats_;
    std::vector<PluginFormatWarning> pluginFormatWarnings_;
    Formats formats_;
};
