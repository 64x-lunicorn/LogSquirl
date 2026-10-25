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

#include "logsquirl_plugin_api.h"

#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include <expected>
#include <optional>

namespace logsquirl::plugins {

/**
 * Parsed and validated plugin manifest (plugin.json).
 *
 * Constructed via the static fromJson() factory which validates all required
 * fields and checks API version compatibility.
 */
class PluginMetadata {
public:
    /**
     * Parse a plugin.json file at the given path.
     * @param jsonPath  Absolute path to the plugin.json file.
     * @return Metadata on success, human-readable error on failure.
     */
    static std::expected<PluginMetadata, QString> fromJsonFile( const QString& jsonPath );

    /**
     * Parse plugin metadata from a JSON byte array.
     * @param json     Raw JSON content.
     * @param context  File path or label used in error messages.
     * @return Metadata on success, human-readable error on failure.
     */
    static std::expected<PluginMetadata, QString> fromJson( const QByteArray& json,
                                                            const QString& context );

    /**
     * Whether an id is a plain plugin name: letters, digits, dots, dashes
     * and underscores, not starting with a dot. Such an id names a folder
     * directly under a plugin directory and nothing else; an empty id, a
     * path, a parent reference or any other character is refused, by the
     * manifest parser and by the catalog alike (#819).
     */
    static bool isValidId( const QString& id );

    const QString& id() const
    {
        return id_;
    }
    const QString& name() const
    {
        return name_;
    }
    const QString& version() const
    {
        return version_;
    }
    const QString& description() const
    {
        return description_;
    }
    const QString& author() const
    {
        return author_;
    }
    const QString& license() const
    {
        return license_;
    }
    const QString& library() const
    {
        return library_;
    }
    const QString& icon() const
    {
        return icon_;
    }
    LogSquirlPluginType type() const
    {
        return type_;
    }
    int apiVersion() const
    {
        return apiVersion_;
    }

    /** Directory containing the plugin.json file (set when loaded from file). */
    const QString& directory() const
    {
        return directory_;
    }

    /** Absolute path to the shared library (directory + library). */
    QString libraryPath() const;

    /** Absolute path to the icon file (directory + icon), empty if none. */
    QString iconPath() const;

    /**
     * The Log Format files the plugin ships, as the manifest's optional
     * `formats` list names them: paths relative to the plugin directory, in
     * the manifest's order (#793). A path leaving the plugin directory is
     * rejected when the manifest is parsed. Empty when the plugin ships none.
     */
    const QStringList& formats() const
    {
        return formats_;
    }

    /**
     * Absolute paths to the Log Format files (directory + each of formats()),
     * empty when the metadata was not read from a file.
     */
    QStringList formatPaths() const;

    /**
     * The Qt the plugin was built against, as the manifest's optional
     * `qt_version` names it (major.minor), or none when the manifest leaves it
     * out: a plugin that hands no widget to the host needs no Qt (#862).
     */
    const std::optional<QVersionNumber>& qtVersion() const
    {
        return qtVersion_;
    }

    /**
     * Why a host running on Qt hostQt cannot serve this plugin, or empty when
     * it can. Qt 6 is backward binary compatible: a plugin built against the
     * host's Qt major and the host's minor or an older one is served, one
     * built against a newer minor or another major is not. A plugin that
     * names no Qt is always served (#862).
     */
    QString qtRefusal( const QVersionNumber& hostQt ) const;

    /** Why the Qt this LogSquirl runs on (qVersion()) cannot serve this plugin. */
    QString qtRefusal() const;

private:
    PluginMetadata() = default;

    QString id_;
    QString name_;
    QString version_;
    QString description_;
    QString author_;
    QString license_;
    QString library_;
    QString icon_;
    QStringList formats_;
    std::optional<QVersionNumber> qtVersion_;
    QString directory_;
    LogSquirlPluginType type_ = LOGSQUIRL_PLUGIN_DATASOURCE;
    int apiVersion_ = 0;
};

} // namespace logsquirl::plugins
