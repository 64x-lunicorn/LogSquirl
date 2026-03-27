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

// Default repository index URL — can be overridden via setIndexUrl()
constexpr auto DefaultIndexUrl
    = "https://raw.githubusercontent.com/64x-lunicorn/LogSquirl/master/plugins.json";

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

void PluginRepository::fetchIndex()
{
    QNetworkRequest request( indexUrl_ );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/1.0" );

    auto* reply = network_.get( request );
    connect( reply, &QNetworkReply::finished, this, [ this, reply ]() {
        reply->deleteLater();

        if ( reply->error() != QNetworkReply::NoError ) {
            const auto msg = tr( "Failed to fetch plugin index: %1" ).arg( reply->errorString() );
            LOG_ERROR << msg;
            Q_EMIT fetchError( msg );
            return;
        }

        const auto data = reply->readAll();
        parseIndex( data );
    } );
}

void PluginRepository::parseIndex( const QByteArray& data )
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( data, &parseError );

    if ( parseError.error != QJsonParseError::NoError ) {
        const auto msg
            = tr( "Failed to parse plugin index: %1" ).arg( parseError.errorString() );
        LOG_ERROR << msg;
        Q_EMIT fetchError( msg );
        return;
    }

    if ( !doc.isObject() ) {
        Q_EMIT fetchError( tr( "Plugin index is not a JSON object" ) );
        return;
    }

    const auto root = doc.object();
    const auto pluginsArray = root.value( "plugins" ).toArray();

    entries_.clear();
    entries_.reserve( static_cast<size_t>( pluginsArray.size() ) );

    const auto platform = currentPlatform();

    for ( const auto& val : pluginsArray ) {
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

        const auto platformsArr = obj.value( "platforms" ).toArray();
        for ( const auto& p : platformsArr ) {
            entry.platforms << p.toString();
        }

        // Only include entries for the current platform (or "all")
        if ( entry.platforms.isEmpty() || entry.platforms.contains( platform )
             || entry.platforms.contains( "all" ) ) {
            entries_.push_back( std::move( entry ) );
        }
    }

    LOG_INFO << "Plugin repository: " << entries_.size() << " plugins available for " << platform;
    Q_EMIT indexReady();
}

void PluginRepository::downloadPlugin( const RepositoryEntry& entry, const QString& destDir )
{
    if ( entry.downloadUrl.isEmpty() ) {
        Q_EMIT downloadError( tr( "No download URL for plugin %1" ).arg( entry.id ) );
        return;
    }

    QNetworkRequest request( entry.downloadUrl );
    request.setHeader( QNetworkRequest::UserAgentHeader, "LogSquirl-PluginRepo/1.0" );

    auto* reply = network_.get( request );

    connect( reply, &QNetworkReply::downloadProgress, this,
             &PluginRepository::downloadProgress );

    // Capture the expected checksum for verification
    const auto expectedSha256 = entry.sha256.toLower();
    const auto pluginId = entry.id;
    const auto fileName = QFileInfo( entry.downloadUrl.path() ).fileName();

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

                 // Verify SHA-256 checksum if provided
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

                 // Write archive to destination
                 QDir().mkpath( destDir );
                 const auto archivePath = QDir( destDir ).filePath( fileName );
                 QFile outFile( archivePath );
                 if ( !outFile.open( QIODevice::WriteOnly ) ) {
                     Q_EMIT downloadError(
                         tr( "Cannot write to %1" ).arg( archivePath ) );
                     return;
                 }
                 outFile.write( data );
                 outFile.close();

                 LOG_INFO << "Downloaded plugin " << pluginId << " to " << archivePath;
                 Q_EMIT downloadFinished( archivePath );
             } );
}

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
