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

#include <QFileInfo>
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

/// The Index Cache: the Indexes of earlier sessions, kept on disk so a Log
/// File opened again need not be indexed again.
///
/// Each cache file stores the compressed line positions, the file hash
/// (used for validation), the maximum line length, and the detected
/// encoding.  Cache files live in the directory the cache is given, with
/// a filename derived from a hash of the absolute source file path.
///
/// The cache is told its directory rather than finding one for itself:
/// the application passes the location from its Indexing Policy, and
/// anything else -- a test included -- passes a location of its own. A
/// cache given an empty directory stores nothing and finds nothing, which
/// is how a cache that is turned off is expressed.
///
/// The cache owns its rules; whoever indexes a Log File only asks it for an
/// Index and hands it one afterwards:
///  - An Index is handed out only while it still fits its Log File: the
///    bytes it was built from are unchanged. The Log File is no shorter,
///    and the header and tail digests (the first and last 5 MB it was
///    built from, re-hashed at their stored offsets) match. An entry that
///    no longer fits, or cannot be read, is deleted.
///  - An Index is always complete for the byte size it was built at. The
///    Log File may have grown since; whether the Index is used as it is or
///    indexing goes on from it is for whoever indexes the Log File to decide.
///  - Nothing is kept for a Log File under the excluded directory, nor for
///    an empty Index.
///  - The cache never grows past its budget. After storing an entry it
///    evicts the least recently used entries, by the cache file's
///    modification time, which a successful load refreshes. An entry that
///    alone exceeds the budget is not written at all.
///
/// A cache holds no state in memory, so several may share a directory:
/// writes are atomic, and a load that loses a race against an eviction of
/// the same entry is a miss.
class IndexCache {
public:
    IndexCache( QString directory, QString excludedDirectory, qint64 budgetBytes );

    /// The Index cached for the given Log File, if there is one that still
    /// fits it, for the size recorded in its hash. Deletes a stale or
    /// unreadable entry.
    std::optional<CachedIndex> tryLoad( const QString& filePath ) const;

    /// Keeps an Index for the given Log File, unless the cache's rules say
    /// otherwise, then evicts older entries until the cache fits its budget.
    /// Returns whether the Index was written.
    bool trySave( const QString& filePath, const LinePositionArray& linePosition,
                  LineLength maxLength, const IndexedHash& hash, const QByteArray& encodingName,
                  bool fakeFinalLF ) const;

    /// Remove all cached indices and return the number of bytes freed.
    qint64 clearAll() const;

    /// Return the total size of all cached index files in bytes.
    qint64 totalCacheSize() const;

private:
    /// Compute the cache file path for a source file.
    QString cacheFilePath( const QString& sourceFilePath ) const;

    /// The cache files in the directory; none when there is no directory.
    QFileInfoList cacheFiles() const;

    /// Whether the Log File lies under the excluded directory.
    bool isExcluded( const QString& filePath ) const;

    /// Evicts the least recently used entries, never the one at keptPath,
    /// until the cache fits its budget.
    void evict( const QString& keptPath ) const;

    /// Magic bytes at the start of every cache file.
    static constexpr quint32 kMagic = 0x4C534149; // "LSAI"
    /// Format version — increment when the on-disk layout changes.
    static constexpr quint32 kVersion = 1;

    QString directory_;
    QString excludedDirectory_;
    qint64 budgetBytes_;
};
