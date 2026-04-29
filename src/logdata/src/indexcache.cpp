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

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include "log.h"

namespace {

/// Build a hex-encoded SHA-256 of the absolute file path for use as a
/// cache-file name.  Using the path (not the file contents) is cheap
/// and deterministic.
QString pathHash( const QString& filePath )
{
    const auto canonical = QFileInfo( filePath ).absoluteFilePath().toUtf8();
    const auto digest
        = QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex();
    return QString::fromLatin1( digest );
}

} // namespace

QString IndexCache::cacheDir()
{
    const auto base = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
    return base + QStringLiteral( "/index" );
}

QString IndexCache::cacheFilePath( const QString& sourceFilePath )
{
    return cacheDir() + QStringLiteral( "/" ) + pathHash( sourceFilePath )
           + QStringLiteral( ".idx" );
}

std::optional<CachedIndex> IndexCache::tryLoad( const QString& filePath )
{
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
                          const QByteArray& encodingName, bool fakeFinalLF )
{
    const auto dir = cacheDir();
    if ( !QDir().mkpath( dir ) ) {
        LOG_WARNING << "Cannot create index cache directory: " << dir;
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

void IndexCache::remove( const QString& filePath )
{
    QFile::remove( cacheFilePath( filePath ) );
}

qint64 IndexCache::clearAll()
{
    const auto dir = cacheDir();
    qint64 freedBytes = 0;

    QDirIterator it( dir, { "*.idx" }, QDir::Files );
    while ( it.hasNext() ) {
        it.next();
        freedBytes += it.fileInfo().size();
        QFile::remove( it.filePath() );
    }

    LOG_INFO << "Cleared index cache: freed " << freedBytes << " bytes";
    return freedBytes;
}

qint64 IndexCache::totalCacheSize()
{
    const auto dir = cacheDir();
    qint64 total = 0;

    QDirIterator it( dir, { "*.idx" }, QDir::Files );
    while ( it.hasNext() ) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void IndexCache::evict( qint64 maxBytes )
{
    const auto dir = cacheDir();

    struct CacheEntry {
        QString path;
        qint64 size;
        QDateTime lastModified;
    };

    QList<CacheEntry> entries;
    qint64 totalSize = 0;

    QDirIterator it( dir, { "*.idx" }, QDir::Files );
    while ( it.hasNext() ) {
        it.next();
        const auto info = it.fileInfo();
        entries.append( { it.filePath(), info.size(), info.lastModified() } );
        totalSize += info.size();
    }

    if ( totalSize <= maxBytes ) {
        return;
    }

    // Sort oldest-first (LRU)
    std::sort( entries.begin(), entries.end(),
               []( const CacheEntry& a, const CacheEntry& b ) {
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
