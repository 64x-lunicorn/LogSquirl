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
#include <QPixmap>
#include <QString>
#include <QUrl>

#include <map>
#include <vector>

namespace logsquirl::plugins {

/**
 * Lightweight catalog entry from the central plugins.json (schema v2).
 * Contains only identification and pointers to the plugin's own release manifest.
 */
struct CatalogEntry {
    QString id;           ///< Unique plugin identifier (reverse-DNS)
    QString name;         ///< Human-readable name
    QString author;       ///< Plugin author
    QString description;  ///< Short plugin description
    QUrl repoUrl;         ///< URL to the plugin's source repository
    QUrl releasesUrl;     ///< URL to the plugin's releases.json
    QUrl iconUrl;         ///< URL to the plugin icon (PNG/SVG, optional)
};

/**
 * A single downloadable asset within a release (one per platform).
 */
struct ReleaseAsset {
    QString platform;     ///< "macos", "linux", or "windows"
    QUrl downloadUrl;     ///< Direct HTTPS URL to the ZIP archive
    QString sha256;       ///< SHA-256 checksum of the archive
};

/**
 * A single version/release of a plugin, parsed from the plugin's releases.json.
 */
struct ReleaseEntry {
    QString version;         ///< Semantic version string
    int apiVersion = 1;      ///< Host API version required
    QString releaseNotes;    ///< Optional markdown release notes
    std::vector<ReleaseAsset> assets; ///< Per-platform download assets
};

/**
 * Legacy flat entry for schema v1 backward compatibility.
 */
struct RepositoryEntry {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QUrl downloadUrl;
    QString sha256;
    QStringList platforms;
    int apiVersion = 1;
};

/**
 * Fetches and parses the remote plugin catalog and per-plugin release manifests.
 *
 * Schema v2 (preferred): central plugins.json contains lightweight catalog entries.
 * Each plugin hosts its own releases.json with version and platform details.
 *
 * Schema v1 (legacy): central plugins.json contains all version/platform info inline.
 * Supported for backward compatibility during the transition period.
 */
class PluginRepository : public QObject {
    Q_OBJECT

  public:
    explicit PluginRepository( QObject* parent = nullptr );

    /** Set the URL of the plugins.json catalog. */
    void setIndexUrl( const QUrl& url );

    /** Return the currently configured catalog URL. */
    QUrl indexUrl() const { return indexUrl_; }

    /**
     * Fetch the central catalog.  For schema v2, also fetches releases.json
     * for each plugin and plugin icons.  Emits catalogReady() when all data
     * is available, or fetchError() on failure.
     */
    void fetchCatalog();

    /** Legacy alias — calls fetchCatalog(). */
    void fetchIndex() { fetchCatalog(); }

    /** Return the catalog entries from the last successful fetch. */
    const std::vector<CatalogEntry>& catalog() const { return catalog_; }

    /** Return releases for a given plugin ID.  Empty if not yet fetched. */
    const std::vector<ReleaseEntry>& releases( const QString& pluginId ) const;

    /** Return the latest release for a plugin that matches the current platform. */
    const ReleaseEntry* latestRelease( const QString& pluginId ) const;

    /** Return the cached icon for a plugin ID.  Null pixmap if not available. */
    QPixmap pluginIcon( const QString& pluginId ) const;

    /** Legacy: return flattened entries for schema v1. */
    const std::vector<RepositoryEntry>& entries() const { return legacyEntries_; }

    /**
     * Download a plugin archive to the given directory.
     * Verifies the SHA-256 checksum after download.
     */
    void downloadPlugin( const ReleaseAsset& asset, const QString& pluginId,
                         const QString& destDir );

    /** Legacy overload for RepositoryEntry. */
    void downloadPlugin( const RepositoryEntry& entry, const QString& destDir );

    /** Return the current platform tag (e.g. "macos", "linux", "windows"). */
    static QString currentPlatform();

    /**
     * Extract a plugin archive (ZIP) into the given directory.
     */
    static bool extractPluginArchive( const QString& archivePath,
                                      const QString& destDir,
                                      QString* errorMessage = nullptr );

  Q_SIGNALS:
    /** Emitted when catalog + all releases are fetched and ready. */
    void catalogReady();

    /** Legacy alias for catalogReady(). */
    void indexReady();

    /** Emitted when a plugin icon has been fetched. */
    void iconReady( const QString& pluginId, const QPixmap& icon );

    /** Emitted on fetch/download error. */
    void fetchError( const QString& errorMessage );

    /** Progress during archive download. */
    void downloadProgress( qint64 bytesReceived, qint64 bytesTotal );

    /** Emitted when a plugin archive has been downloaded and verified. */
    void downloadFinished( const QString& archivePath );

    /** Emitted when download verification fails. */
    void downloadError( const QString& errorMessage );

  private:
    void parseCatalog( const QByteArray& data );
    void parseCatalogV1( const QJsonArray& plugins );
    void parseCatalogV2( const QJsonArray& plugins );
    void fetchReleasesForAll();
    void fetchReleases( const CatalogEntry& entry );
    void parseReleases( const QString& pluginId, const QByteArray& data );
    void fetchIcon( const CatalogEntry& entry );
    void checkAllFetched();

    QNetworkAccessManager network_;
    QUrl indexUrl_;

    std::vector<CatalogEntry> catalog_;
    std::map<QString, std::vector<ReleaseEntry>> releases_;
    std::map<QString, QPixmap> iconCache_;
    std::vector<RepositoryEntry> legacyEntries_;  ///< Schema v1 fallback

    int schemaVersion_ = 0;
    int pendingFetches_ = 0;  ///< Counter for outstanding release/icon fetches
};

} // namespace logsquirl::plugins
