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

#include "pluginrepository.h"

#include "log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include <kzip.h>

#ifdef LOGSQUIRL_KARCHIVE
#include <QAtomicInt>
#endif

namespace logsquirl::plugins {

namespace {

constexpr auto DefaultIndexUrl
    = "https://raw.githubusercontent.com/64x-lunicorn/LogSquirl-Plugins/main/plugins.json";

// Empty vector returned when no releases exist for a plugin ID
const std::vector<ReleaseEntry> EmptyReleases;

} // namespace

PluginRepository::PluginRepository( QObject* parent )
    : QObject( parent )
    , indexUrl_( DefaultIndexUrl )
{
}

void PluginRepository::setIndexUrl( const QUrl& url )
{
    indexUrl_ = url;
}

// ── Catalog fetch (top-level) ─────────────────────────────────────────

void PluginRepository::fetchCatalog()
{
    QNetworkRequest request( indexUrl_ );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/2.0" );

    auto* reply = network_.get( request );
    connect( reply, &QNetworkReply::finished, this, [ this, reply ]() {
        reply->deleteLater();

        if ( reply->error() != QNetworkReply::NoError ) {
            const auto msg = tr( "Failed to fetch plugin catalog: %1" )
                                 .arg( reply->errorString() );
            LOG_ERROR << msg;
            Q_EMIT fetchError( msg );
            return;
        }

        parseCatalog( reply->readAll() );
    } );
}

void PluginRepository::parseCatalog( const QByteArray& data )
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( data, &parseError );

    if ( parseError.error != QJsonParseError::NoError ) {
        Q_EMIT fetchError( tr( "Failed to parse plugin catalog: %1" )
                               .arg( parseError.errorString() ) );
        return;
    }

    if ( !doc.isObject() ) {
        Q_EMIT fetchError( tr( "Plugin catalog is not a JSON object" ) );
        return;
    }

    const auto root = doc.object();
    schemaVersion_ = root.value( "schema_version" ).toInt( 1 );
    const auto pluginsArray = root.value( "plugins" ).toArray();

    catalog_.clear();
    releases_.clear();
    iconCache_.clear();
    legacyEntries_.clear();
    pendingFetches_ = 0;

    if ( schemaVersion_ >= 2 ) {
        parseCatalogV2( pluginsArray );
    }
    else {
        parseCatalogV1( pluginsArray );
    }
}

// ── Schema v1 (legacy): all data inline ───────────────────────────────

void PluginRepository::parseCatalogV1( const QJsonArray& plugins )
{
    const auto platform = currentPlatform();

    legacyEntries_.reserve( static_cast<size_t>( plugins.size() ) );

    for ( const auto& val : plugins ) {
        const auto obj = val.toObject();

        RepositoryEntry entry;
        entry.id = obj.value( "id" ).toString();
        entry.name = obj.value( "name" ).toString();
        entry.version = obj.value( "version" ).toString();
        entry.description = obj.value( "description" ).toString();
        entry.author = obj.value( "author" ).toString();
        entry.downloadUrl = QUrl( obj.value( "download_url" ).toString() );
        entry.sha256 = obj.value( "sha256" ).toString();
        entry.apiVersion = obj.value( "api_version" ).toInt( 1 );

        for ( const auto& p : obj.value( "platforms" ).toArray() ) {
            entry.platforms << p.toString();
        }

        if ( entry.platforms.isEmpty() || entry.platforms.contains( platform )
             || entry.platforms.contains( "all" ) ) {
            legacyEntries_.push_back( std::move( entry ) );
        }
    }

    // Build catalog entries from legacy data for the unified dialog
    std::map<QString, CatalogEntry> seen;
    for ( const auto& le : legacyEntries_ ) {
        if ( seen.find( le.id ) == seen.end() ) {
            CatalogEntry ce;
            ce.id = le.id;
            ce.name = le.name;
            ce.author = le.author;
            ce.description = le.description;
            seen[ le.id ] = ce;
        }

        // Build a ReleaseEntry from the legacy data
        ReleaseAsset asset;
        asset.platform = platform;
        asset.downloadUrl = le.downloadUrl;
        asset.sha256 = le.sha256;

        auto& rels = releases_[ le.id ];
        bool found = false;
        for ( auto& rel : rels ) {
            if ( rel.version == le.version ) {
                rel.assets.push_back( std::move( asset ) );
                found = true;
                break;
            }
        }
        if ( !found ) {
            ReleaseEntry rel;
            rel.version = le.version;
            rel.apiVersion = le.apiVersion;
            rel.assets.push_back( std::move( asset ) );
            rels.push_back( std::move( rel ) );
        }
    }

    for ( auto& [ id, ce ] : seen ) {
        catalog_.push_back( std::move( ce ) );
    }

    LOG_INFO << "Plugin catalog (v1): " << catalog_.size() << " plugins, "
             << legacyEntries_.size() << " entries for " << platform;

    Q_EMIT catalogReady();
    Q_EMIT indexReady();
}

// ── Schema v2: lightweight catalog + per-plugin releases.json ─────────

void PluginRepository::parseCatalogV2( const QJsonArray& plugins )
{
    catalog_.reserve( static_cast<size_t>( plugins.size() ) );

    for ( const auto& val : plugins ) {
        const auto obj = val.toObject();

        CatalogEntry entry;
        entry.id = obj.value( "id" ).toString();
        entry.name = obj.value( "name" ).toString();
        entry.author = obj.value( "author" ).toString();
        entry.description = obj.value( "description" ).toString();
        entry.repoUrl = QUrl( obj.value( "repo_url" ).toString() );
        entry.releasesUrl = QUrl( obj.value( "releases_url" ).toString() );
        entry.iconUrl = QUrl( obj.value( "icon_url" ).toString() );

        if ( !entry.id.isEmpty() ) {
            catalog_.push_back( std::move( entry ) );
        }
    }

    LOG_INFO << "Plugin catalog (v2): " << catalog_.size() << " plugins";

    // Fetch releases.json and icons for each plugin
    fetchReleasesForAll();
}

void PluginRepository::fetchReleasesForAll()
{
    pendingFetches_ = 0;

    for ( const auto& entry : catalog_ ) {
        if ( entry.releasesUrl.isValid() && !entry.releasesUrl.isEmpty() ) {
            ++pendingFetches_;
            fetchReleases( entry );
        }
        if ( entry.iconUrl.isValid() && !entry.iconUrl.isEmpty() ) {
            ++pendingFetches_;
            fetchIcon( entry );
        }
    }

    // If no fetches needed, signal immediately
    if ( pendingFetches_ == 0 ) {
        Q_EMIT catalogReady();
        Q_EMIT indexReady();
    }
}

void PluginRepository::fetchReleases( const CatalogEntry& entry )
{
    QNetworkRequest request( entry.releasesUrl );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/2.0" );

    const auto pluginId = entry.id;
    auto* reply = network_.get( request );
    connect( reply, &QNetworkReply::finished, this, [ this, reply, pluginId ]() {
        reply->deleteLater();

        if ( reply->error() == QNetworkReply::NoError ) {
            parseReleases( pluginId, reply->readAll() );
        }
        else {
            LOG_WARNING << "Failed to fetch releases for " << pluginId << ": "
                        << reply->errorString();
        }

        checkAllFetched();
    } );
}

void PluginRepository::parseReleases( const QString& pluginId, const QByteArray& data )
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( data, &parseError );
    if ( !doc.isObject() ) {
        LOG_WARNING << "Invalid releases.json for " << pluginId;
        return;
    }

    const auto root = doc.object();
    const auto releasesArray = root.value( "releases" ).toArray();
    const auto platform = currentPlatform();

    auto& rels = releases_[ pluginId ];
    rels.reserve( static_cast<size_t>( releasesArray.size() ) );

    for ( const auto& val : releasesArray ) {
        const auto obj = val.toObject();

        ReleaseEntry release;
        release.version = obj.value( "version" ).toString();
        release.apiVersion = obj.value( "api_version" ).toInt( 1 );
        release.releaseNotes = obj.value( "release_notes" ).toString();

        for ( const auto& a : obj.value( "assets" ).toArray() ) {
            const auto aObj = a.toObject();
            const auto assetPlatform = aObj.value( "platform" ).toString();

            if ( assetPlatform == platform || assetPlatform == "all" ) {
                ReleaseAsset asset;
                asset.platform = assetPlatform;
                asset.downloadUrl = QUrl( aObj.value( "download_url" ).toString() );
                asset.sha256 = aObj.value( "sha256" ).toString();
                release.assets.push_back( std::move( asset ) );
            }
        }

        // Only include releases that have assets for the current platform
        if ( !release.assets.empty() ) {
            rels.push_back( std::move( release ) );
        }
    }

    LOG_INFO << "Fetched " << rels.size() << " releases for " << pluginId;
}

void PluginRepository::fetchIcon( const CatalogEntry& entry )
{
    QNetworkRequest request( entry.iconUrl );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/2.0" );

    const auto pluginId = entry.id;
    auto* reply = network_.get( request );
    connect( reply, &QNetworkReply::finished, this, [ this, reply, pluginId ]() {
        reply->deleteLater();

        if ( reply->error() == QNetworkReply::NoError ) {
            QPixmap pixmap;
            if ( pixmap.loadFromData( reply->readAll() ) ) {
                iconCache_[ pluginId ] = pixmap.scaled( 48, 48, Qt::KeepAspectRatio,
                                                        Qt::SmoothTransformation );
                Q_EMIT iconReady( pluginId, iconCache_[ pluginId ] );
            }
        }
        else {
            LOG_WARNING << "Failed to fetch icon for " << pluginId;
        }

        checkAllFetched();
    } );
}

void PluginRepository::checkAllFetched()
{
    --pendingFetches_;
    if ( pendingFetches_ <= 0 ) {
        pendingFetches_ = 0;
        Q_EMIT catalogReady();
        Q_EMIT indexReady();
    }
}

// ── Accessors ─────────────────────────────────────────────────────────

const std::vector<ReleaseEntry>& PluginRepository::releases( const QString& pluginId ) const
{
    auto it = releases_.find( pluginId );
    if ( it != releases_.end() ) {
        return it->second;
    }
    return EmptyReleases;
}

const ReleaseEntry* PluginRepository::latestRelease( const QString& pluginId ) const
{
    const auto& rels = releases( pluginId );
    if ( rels.empty() ) {
        return nullptr;
    }
    // Releases are expected in newest-first order from releases.json
    return &rels.front();
}

QPixmap PluginRepository::pluginIcon( const QString& pluginId ) const
{
    auto it = iconCache_.find( pluginId );
    if ( it != iconCache_.end() ) {
        return it->second;
    }
    return {};
}

// ── Download ──────────────────────────────────────────────────────────

void PluginRepository::downloadPlugin( const ReleaseAsset& asset, const QString& pluginId,
                                        const QString& destDir )
{
    if ( asset.downloadUrl.isEmpty() ) {
        Q_EMIT downloadError( tr( "No download URL for plugin %1" ).arg( pluginId ) );
        return;
    }

    QNetworkRequest request( asset.downloadUrl );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/2.0" );

    auto* reply = network_.get( request );
    connect( reply, &QNetworkReply::downloadProgress, this,
             &PluginRepository::downloadProgress );

    const auto expectedSha256 = asset.sha256.toLower();
    const auto fileName = QFileInfo( asset.downloadUrl.path() ).fileName();

    connect( reply, &QNetworkReply::finished, this,
             [ this, reply, destDir, expectedSha256, pluginId, fileName ]() {
                 reply->deleteLater();

                 if ( reply->error() != QNetworkReply::NoError ) {
                     Q_EMIT downloadError(
                         tr( "Download failed for %1: %2" )
                             .arg( pluginId, reply->errorString() ) );
                     return;
                 }

                 const auto data = reply->readAll();

                 if ( !expectedSha256.isEmpty() ) {
                     const auto actualHash
                         = QCryptographicHash::hash( data, QCryptographicHash::Sha256 )
                               .toHex()
                               .toLower();
                     if ( actualHash != expectedSha256 ) {
                         Q_EMIT downloadError(
                             tr( "Checksum mismatch for %1: expected %2, got %3" )
                                 .arg( pluginId, expectedSha256,
                                       QString::fromLatin1( actualHash ) ) );
                         return;
                     }
                 }

                 QDir().mkpath( destDir );
                 const auto archivePath = QDir( destDir ).filePath( fileName );
                 QFile outFile( archivePath );
                 if ( !outFile.open( QIODevice::WriteOnly ) ) {
                     Q_EMIT downloadError( tr( "Cannot write to %1" ).arg( archivePath ) );
                     return;
                 }
                 outFile.write( data );
                 outFile.close();

                 LOG_INFO << "Downloaded plugin " << pluginId << " to " << archivePath;
                 Q_EMIT downloadFinished( archivePath );
             } );
}

void PluginRepository::downloadPlugin( const RepositoryEntry& entry, const QString& destDir )
{
    ReleaseAsset asset;
    asset.downloadUrl = entry.downloadUrl;
    asset.sha256 = entry.sha256;
    asset.platform = currentPlatform();
    downloadPlugin( asset, entry.id, destDir );
}

// ── Platform detection ────────────────────────────────────────────────

QString PluginRepository::currentPlatform()
{
#if defined( Q_OS_WIN )
    return QStringLiteral( "windows" );
#elif defined( Q_OS_MACOS )
    return QStringLiteral( "macos" );
#else
    return QStringLiteral( "linux" );
#endif
}

// ── Archive extraction ────────────────────────────────────────────────

bool PluginRepository::extractPluginArchive( const QString& archivePath,
                                             const QString& destDir,
                                             QString* errorMessage )
{
    KZip zip( archivePath );
    if ( !zip.open( QIODevice::ReadOnly ) ) {
        const auto msg = QString( "Cannot open archive: %1" ).arg( archivePath );
        LOG_ERROR << msg;
        if ( errorMessage ) {
            *errorMessage = msg;
        }
        return false;
    }

    const auto* root = zip.directory();
    if ( !root ) {
        zip.close();
        const auto msg = QString( "Cannot read archive directory: %1" ).arg( archivePath );
        LOG_ERROR << msg;
        if ( errorMessage ) {
            *errorMessage = msg;
        }
        return false;
    }

    QDir().mkpath( destDir );

    const auto recursive = true;
#ifdef LOGSQUIRL_KARCHIVE
    QAtomicInt notCanceled{ 0 };
    const auto ok = root->copyTo( destDir, notCanceled, recursive );
#else
    const auto ok = root->copyTo( destDir, recursive );
#endif
    zip.close();

    if ( !ok ) {
        const auto msg
            = QString( "Failed to extract archive %1 to %2" ).arg( archivePath, destDir );
        LOG_ERROR << msg;
        if ( errorMessage ) {
            *errorMessage = msg;
        }
        return false;
    }

    LOG_INFO << "Extracted plugin archive " << archivePath << " to " << destDir;
    return true;
}

} // namespace logsquirl::plugins
