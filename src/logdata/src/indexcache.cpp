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

#include "indexcache.h"

#include <algorithm>
#include <utility>

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "log.h"

namespace {

/// Build a hex-encoded SHA-256 of the absolute file path for use as a
/// cache-file name.  Using the path (not the file contents) is cheap
/// and deterministic.
QString pathHash( const QString& filePath )
{
    const auto canonical = QFileInfo( filePath ).absoluteFilePath().toUtf8();
    const auto digest = QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex();
    return QString::fromLatin1( digest );
}

} // namespace

IndexCache::IndexCache( QString directory )
    : directory_( std::move( directory ) )
{
}

QString IndexCache::cacheFilePath( const QString& sourceFilePath ) const
{
    return directory_ + QStringLiteral( "/" ) + pathHash( sourceFilePath )
           + QStringLiteral( ".idx" );
}

std::optional<CachedIndex> IndexCache::tryLoad( const QString& filePath ) const
{
    if ( directory_.isEmpty() ) {
        return std::nullopt;
    }

    const auto path = cacheFilePath( filePath );
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return std::nullopt;
    }

    QDataStream in( &file );
    in.setVersion( QDataStream::Qt_6_0 );

    // Header
    quint32 magic = 0;
    quint32 version = 0;
    in >> magic >> version;
    if ( magic != kMagic || version != kVersion ) {
        LOG_INFO << "Index cache version mismatch for " << filePath;
        return std::nullopt;
    }

    // IndexedHash
    IndexedHash hash;
    in >> hash.size >> hash.fullDigest;
    in >> hash.headerSize >> hash.headerDigest;
    in >> hash.tailSize >> hash.tailOffset >> hash.tailDigest;
    if ( in.status() != QDataStream::Ok ) {
        return std::nullopt;
    }

    // maxLength
    qint32 maxLen = 0;
    in >> maxLen;

    // Encoding
    QByteArray encodingName;
    in >> encodingName;

    // fakeFinalLF flag
    bool fakeLF = false;
    in >> fakeLF;

    // Compressed line positions
    CompressedLinePositionStorage storage;
    if ( !storage.deserialize( in ) ) {
        LOG_WARNING << "Index cache deserialization failed for " << filePath;
        return std::nullopt;
    }

    if ( in.status() != QDataStream::Ok ) {
        return std::nullopt;
    }

    // Build LinePositionArray from the deserialized storage
    LinePositionArray linePosition( std::move( storage ) );
    if ( fakeLF ) {
        linePosition.setFakeFinalLF( true );
    }

    CachedIndex result;
    result.linePosition = std::move( linePosition );
    result.maxLength = LineLength( maxLen );
    result.hash = hash;
    result.encodingName = encodingName;
    result.fakeFinalLF = fakeLF;

    LOG_INFO << "Loaded index cache for " << filePath << " (" << result.hash.size << " bytes, "
             << result.linePosition.size() << " lines)";

    return result;
}

bool IndexCache::trySave( const QString& filePath, const LinePositionArray& linePosition,
                          LineLength maxLength, const IndexedHash& hash,
                          const QByteArray& encodingName, bool fakeFinalLF ) const
{
    if ( directory_.isEmpty() ) {
        return false;
    }

    if ( !QDir().mkpath( directory_ ) ) {
        LOG_WARNING << "Cannot create index cache directory: " << directory_;
        return false;
    }

    const auto path = cacheFilePath( filePath );
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        LOG_WARNING << "Cannot open index cache for writing: " << path;
        return false;
    }

    QDataStream out( &file );
    out.setVersion( QDataStream::Qt_6_0 );

    // Header
    out << kMagic << kVersion;

    // IndexedHash
    out << hash.size << hash.fullDigest;
    out << hash.headerSize << hash.headerDigest;
    out << hash.tailSize << hash.tailOffset << hash.tailDigest;

    // maxLength
    out << static_cast<qint32>( maxLength.get() );

    // Encoding
    out << encodingName;

    // fakeFinalLF
    out << fakeFinalLF;

    // Compressed line positions via direct storage serialization
    linePosition.storage().serialize( out );

    if ( out.status() != QDataStream::Ok ) {
        LOG_WARNING << "Failed to write index cache for " << filePath;
        file.cancelWriting();
        return false;
    }

    if ( !file.commit() ) {
        LOG_WARNING << "Failed to commit index cache for " << filePath;
        return false;
    }

    LOG_INFO << "Saved index cache for " << filePath << " (" << linePosition.size() << " lines)";
    return true;
}

void IndexCache::remove( const QString& filePath ) const
{
    if ( directory_.isEmpty() ) {
        return;
    }

    QFile::remove( cacheFilePath( filePath ) );
}

QFileInfoList IndexCache::cacheFiles() const
{
    if ( directory_.isEmpty() ) {
        return {};
    }

    return QDir( directory_ ).entryInfoList( { QStringLiteral( "*.idx" ) }, QDir::Files );
}

qint64 IndexCache::clearAll() const
{
    qint64 freedBytes = 0;

    for ( const auto& file : cacheFiles() ) {
        freedBytes += file.size();
        QFile::remove( file.filePath() );
    }

    LOG_INFO << "Cleared index cache: freed " << freedBytes << " bytes";
    return freedBytes;
}

qint64 IndexCache::totalCacheSize() const
{
    qint64 total = 0;

    for ( const auto& file : cacheFiles() ) {
        total += file.size();
    }
    return total;
}

void IndexCache::evict( qint64 maxBytes ) const
{
    struct CacheEntry {
        QString path;
        qint64 size;
        QDateTime lastModified;
    };

    QList<CacheEntry> entries;
    qint64 totalSize = 0;

    for ( const auto& file : cacheFiles() ) {
        entries.append( { file.filePath(), file.size(), file.lastModified() } );
        totalSize += file.size();
    }

    if ( totalSize <= maxBytes ) {
        return;
    }

    // Sort oldest-first (LRU)
    std::sort( entries.begin(), entries.end(), []( const CacheEntry& a, const CacheEntry& b ) {
        return a.lastModified < b.lastModified;
    } );

    for ( const auto& entry : entries ) {
        if ( totalSize <= maxBytes ) {
            break;
        }
        if ( QFile::remove( entry.path ) ) {
            totalSize -= entry.size;
            LOG_INFO << "Evicted index cache entry: " << entry.path << " (" << entry.size
                     << " bytes)";
        }
        else {
            LOG_WARNING << "Failed to evict index cache entry: " << entry.path;
        }
    }
}
