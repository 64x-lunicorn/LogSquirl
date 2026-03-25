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

#include <expected>

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

    const QString& id() const { return id_; }
    const QString& name() const { return name_; }
    const QString& version() const { return version_; }
    const QString& description() const { return description_; }
    const QString& author() const { return author_; }
    const QString& license() const { return license_; }
    const QString& library() const { return library_; }
    LogSquirlPluginType type() const { return type_; }
    int apiVersion() const { return apiVersion_; }

    /** Directory containing the plugin.json file (set when loaded from file). */
    const QString& directory() const { return directory_; }

    /** Absolute path to the shared library (directory + library). */
    QString libraryPath() const;

  private:
    PluginMetadata() = default;

    QString id_;
    QString name_;
    QString version_;
    QString description_;
    QString author_;
    QString license_;
    QString library_;
    QString directory_;
    LogSquirlPluginType type_ = LOGSQUIRL_PLUGIN_DATASOURCE;
    int apiVersion_ = 0;
};

} // namespace logsquirl::plugins
