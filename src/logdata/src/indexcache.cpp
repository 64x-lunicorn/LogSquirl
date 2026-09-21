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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "indexedhash.h"
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

#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
constexpr auto PathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr auto PathCaseSensitivity = Qt::CaseSensitive;
#endif

/// The path with symbolic links resolved where it exists, so one directory
/// reached by two spellings -- macOS's /var and /private/var, say -- compares
/// equal.
QString resolvedPath( const QString& path )
{
    const QFileInfo info( path );
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath( info.absoluteFilePath() ) : canonical;
}

} // namespace

IndexCache::IndexCache( QString directory, QString excludedDirectory, qint64 budgetBytes )
    : directory_( std::move( directory ) )
    , excludedDirectory_( std::move( excludedDirectory ) )
    , budgetBytes_( budgetBytes )
{
}

QString IndexCache::cacheFilePath( const QString& sourceFilePath ) const
{
    return directory_ + QStringLiteral( "/" ) + pathHash( sourceFilePath )
           + QStringLiteral( ".idx" );
}

bool IndexCache::isExcluded( const QString& filePath ) const
{
    if ( excludedDirectory_.isEmpty() ) {
        return false;
    }

    auto excludedPrefix = resolvedPath( excludedDirectory_ );
    if ( !excludedPrefix.endsWith( QLatin1Char( '/' ) ) ) {
        excludedPrefix += QLatin1Char( '/' );
    }
    return resolvedPath( filePath ).startsWith( excludedPrefix, PathCaseSensitivity );
}

std::optional<CachedIndex> IndexCache::tryLoad( const QString& filePath,
                                                DigestCoverage coverage ) const
{
    if ( directory_.isEmpty() || isExcluded( filePath ) ) {
        return std::nullopt;
    }

    const auto path = cacheFilePath( filePath );

    // Set when the entry yields nothing yet may still be good.
    bool keepEntry = false;

    const auto readEntry = [ & ]( QFile& file ) -> std::optional<CachedIndex> {
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

        // Checked before the line positions are read: a stale entry is
        // found out without deserializing the Index it holds.
        // An Index fits a Log File that has grown since as well: it is
        // complete for the size it was built at. Every recorded hash has
        // header and tail digests, but only some a full digest: an entry
        // written under fast modification detection has none, and is taken
        // for stale by a caller asking for one (#337).
        switch ( indexFit( hash, filePath, coverage ) ) {
        case IndexFit::Unchanged:
        case IndexFit::Grown:
            break;
        case IndexFit::Changed:
            LOG_INFO << "Cached index stale for " << filePath;
            return std::nullopt;
        case IndexFit::LogFileUnreadable:
            keepEntry = true;
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
        return result;
    };

    std::optional<CachedIndex> result;
    {
        QFile file( path );
        // No entry, or one evicted by a concurrent run since: a miss either way.
        if ( !file.open( QIODevice::ReadOnly ) ) {
            return std::nullopt;
        }
        result = readEntry( file );
    }

    if ( !result ) {
        if ( !keepEntry ) {
            QFile::remove( path );
        }
        return std::nullopt;
    }

    // Eviction goes by modification time, so marking the entry as used now
    // makes it evict the least recently used entry rather than the least
    // recently written. Opened again for writing, as some platforms refuse
    // to set the time through a read-only handle; ExistingOnly so that an
    // entry evicted in the meantime is not recreated empty.
    QFile touched( path );
    if ( !touched.open( QIODevice::ReadWrite | QIODevice::ExistingOnly )
         || !touched.setFileTime( QDateTime::currentDateTime(),
                                  QFileDevice::FileModificationTime ) ) {
        LOG_DEBUG << "Cannot mark index cache entry as used, eviction treats it as written: "
                  << path;
    }

    LOG_INFO << "Loaded index cache for " << filePath << " (" << result->hash.size << " bytes, "
             << result->linePosition.size() << " lines)";

    return result;
}

bool IndexCache::trySave( const QString& filePath, const LinePositionArray& linePosition,
                          LineLength maxLength, const IndexedHash& hash,
                          const QByteArray& encodingName, bool fakeFinalLF ) const
{
    // An empty Index has no value, wastes disk space, and exercises the
    // empty-storage code paths in CompressedLinePositionStorage::serialize()
    // unnecessarily.
    if ( directory_.isEmpty() || budgetBytes_ <= 0 || linePosition.size().get() == 0
         || isExcluded( filePath ) ) {
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

    if ( out.status() != QDataStream::Ok || !file.flush() ) {
        LOG_WARNING << "Failed to write index cache for " << filePath;
        file.cancelWriting();
        return false;
    }

    // The budget is hard: rather than evicting everything else and still not
    // fitting, an entry that alone exceeds it is dropped before it replaces
    // anything.
    if ( file.size() > budgetBytes_ ) {
        LOG_INFO << "Index for " << filePath << " (" << file.size()
                 << " bytes) exceeds the index cache budget of " << budgetBytes_ << " bytes";
        file.cancelWriting();
        return false;
    }

    if ( !file.commit() ) {
        LOG_WARNING << "Failed to commit index cache for " << filePath;
        return false;
    }

    LOG_INFO << "Saved index cache for " << filePath << " (" << linePosition.size() << " lines)";

    evict( path );
    return true;
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

void IndexCache::evict( const QString& keptPath ) const
{
    struct CacheEntry {
        QString path;
        qint64 size;
        QDateTime lastUsed;
    };

    const auto keptName = QFileInfo( keptPath ).fileName();

    QList<CacheEntry> entries;
    qint64 totalSize = 0;

    for ( const auto& file : cacheFiles() ) {
        totalSize += file.size();
        if ( file.fileName() != keptName ) {
            entries.append( { file.filePath(), file.size(), file.lastModified() } );
        }
    }

    if ( totalSize <= budgetBytes_ ) {
        return;
    }

    // Least recently used first: a load refreshes the modification time.
    std::sort( entries.begin(), entries.end(),
               []( const CacheEntry& a, const CacheEntry& b ) { return a.lastUsed < b.lastUsed; } );

    for ( const auto& entry : entries ) {
        if ( totalSize <= budgetBytes_ ) {
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
