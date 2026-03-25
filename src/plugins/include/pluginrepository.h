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

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <vector>

namespace logsquirl::plugins {

/**
 * Describes a single plugin available in the remote repository.
 * Parsed from the plugins.json index file.
 */
struct RepositoryEntry {
    QString id;           ///< Unique plugin identifier
    QString name;         ///< Human-readable name
    QString version;      ///< Semantic version string
    QString description;  ///< Short plugin description
    QString author;       ///< Plugin author
    QUrl downloadUrl;     ///< URL to the plugin archive (.zip)
    QString sha256;       ///< SHA-256 checksum of the archive
    QStringList platforms; ///< Supported platforms (e.g. "macos", "linux", "windows")
    int apiVersion = 1;   ///< Minimum host API version required
};

/**
 * Fetches and parses the remote plugin repository index.
 *
 * The index is a JSON file hosted on GitHub with the following structure:
 * @code
 * {
 *   "schema_version": 1,
 *   "plugins": [
 *     {
 *       "id": "com.example.myplugin",
 *       "name": "My Plugin",
 *       "version": "1.0.0",
 *       "description": "Does useful things",
 *       "author": "Author Name",
 *       "download_url": "https://...",
 *       "sha256": "abc123...",
 *       "platforms": ["macos", "linux", "windows"],
 *       "api_version": 1
 *     }
 *   ]
 * }
 * @endcode
 */
class PluginRepository : public QObject {
    Q_OBJECT

  public:
    explicit PluginRepository( QObject* parent = nullptr );

    /** Set the URL of the plugins.json index file. */
    void setIndexUrl( const QUrl& url );

    /** Return the currently configured index URL. */
    QUrl indexUrl() const { return indexUrl_; }

    /** Fetch the remote index.  Emits indexReady() or fetchError() when done. */
    void fetchIndex();

    /** Return the entries parsed from the last successful fetch. */
    const std::vector<RepositoryEntry>& entries() const { return entries_; }

    /**
     * Download a plugin archive to the given directory.
     * Verifies the SHA-256 checksum after download.
     * Emits downloadFinished() on completion.
     */
    void downloadPlugin( const RepositoryEntry& entry, const QString& destDir );

    /** Return the current platform tag (e.g. "macos", "linux", "windows"). */
    static QString currentPlatform();

  Q_SIGNALS:
    /** Emitted when the index has been successfully fetched and parsed. */
    void indexReady();

    /** Emitted when an error occurs during fetch or download. */
    void fetchError( const QString& errorMessage );

    /** Progress during archive download. */
    void downloadProgress( qint64 bytesReceived, qint64 bytesTotal );

    /**
     * Emitted when a plugin archive has been downloaded and verified.
     * @param archivePath Path to the downloaded archive file.
     */
    void downloadFinished( const QString& archivePath );

    /** Emitted when download verification fails. */
    void downloadError( const QString& errorMessage );

  private:
    void parseIndex( const QByteArray& data );

    QNetworkAccessManager network_;
    QUrl indexUrl_;
    std::vector<RepositoryEntry> entries_;
};

} // namespace logsquirl::plugins
