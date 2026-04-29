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

#include <optional>

#include <QString>

#include "compressedlinestorage.h"
#include "linepositionarray.h"
#include "logdataworker.h"

/// Result of loading an index from the disk cache.
struct CachedIndex {
    LinePositionArray linePosition;
    LineLength maxLength;
    IndexedHash hash;
    QByteArray encodingName; // QTextCodec::name()
    bool fakeFinalLF = false;
};

/// Persistent disk cache for line-offset indices.
///
/// Each cache file stores the compressed line positions, the file hash
/// (used for validation), the maximum line length, and the detected
/// encoding.  Cache files are stored under
/// `QStandardPaths::CacheLocation / "index"` with a filename derived
/// from a hash of the absolute source file path.
///
/// The cache is validated by comparing the stored IndexedHash with the
/// current file: header hash (first 5 MB) and tail hash (last 5 MB).
/// If the file has grown but the indexed portion is unchanged, the
/// cached index can be used as the starting point for a partial
/// re-index.
class IndexCache {
public:
    /// Try to load a cached index for the given file.
    /// Returns std::nullopt if the cache does not exist or is invalid.
    static std::optional<CachedIndex> tryLoad( const QString& filePath );

    /// Save an index to the disk cache.
    static bool trySave( const QString& filePath, const LinePositionArray& linePosition,
                         LineLength maxLength, const IndexedHash& hash,
                         const QByteArray& encodingName, bool fakeFinalLF );

    /// Remove the cached index for a specific file.
    static void remove( const QString& filePath );

    /// Remove all cached indices and return the number of bytes freed.
    static qint64 clearAll();

    /// Return the total size of all cached index files in bytes.
    static qint64 totalCacheSize();

    /// Return the cache directory path.
    static QString cacheDir();

    /// Enforce the maximum cache size by evicting least-recently-used
    /// entries until total size is below maxBytes.
    static void evict( qint64 maxBytes );

private:
    /// Compute the cache file path for a source file.
    static QString cacheFilePath( const QString& sourceFilePath );

    /// Magic bytes at the start of every cache file.
    static constexpr quint32 kMagic = 0x4C534149; // "LSAI"
    /// Format version — increment when the on-disk layout changes.
    static constexpr quint32 kVersion = 1;
};
