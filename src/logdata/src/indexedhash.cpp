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

#include "indexedhash.h"

#include "filedigest.h"

#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <cstddef>

namespace {

/// Whether the given range of the Log File has the given digest, computed
/// with the same FileDigest the indexer records it with. Read in chunks, so
/// a corrupt size in a cache file cannot make this allocate without bound.
bool digestMatches( QFile& logFile, qint64 offset, qint64 size, quint64 expectedDigest )
{
    if ( offset < 0 || size < 0 || !logFile.seek( offset ) ) {
        return false;
    }

    constexpr qint64 ChunkSize = 1024 * 1024;
    QByteArray buffer( static_cast<qsizetype>( std::min( size, ChunkSize ) ), Qt::Uninitialized );
    FileDigest digest;
    for ( auto remaining = size; remaining > 0; ) {
        const auto readBytes
            = logFile.read( buffer.data(), std::min( remaining, qint64{ buffer.size() } ) );
        if ( readBytes <= 0 ) {
            return false;
        }
        digest.addData( buffer.data(), static_cast<size_t>( readBytes ) );
        remaining -= readBytes;
    }
    return digest.digest() == expectedDigest;
}

bool recordedBytesMatch( QFile& logFile, const IndexedHash& recorded, DigestCoverage coverage )
{
    switch ( coverage ) {
    case DigestCoverage::Full:
    case DigestCoverage::FullUnlessGrown:
        return digestMatches( logFile, 0, recorded.size, recorded.fullDigest );
    case DigestCoverage::HeaderAndTail:
        // A Log File no longer than one digest block has its tail digest
        // taken at offset 0, which is its header digest again.
        return digestMatches( logFile, 0, recorded.headerSize, recorded.headerDigest )
               && ( recorded.tailOffset == 0
                    || digestMatches( logFile, recorded.tailOffset, recorded.tailSize,
                                      recorded.tailDigest ) );
    }
    return false;
}

} // namespace

IndexFit indexFit( const IndexedHash& recorded, const QString& logFilePath,
                   DigestCoverage coverage )
{
    QFile logFile( logFilePath );
    if ( !logFile.exists() ) {
        return IndexFit::Changed;
    }
    if ( !logFile.open( QIODevice::ReadOnly ) ) {
        return IndexFit::LogFileUnreadable;
    }

    const auto size = logFile.size();
    if ( coverage == DigestCoverage::FullUnlessGrown ) {
        coverage = size > recorded.size ? DigestCoverage::HeaderAndTail : DigestCoverage::Full;
    }
    if ( size == 0 || size < recorded.size || !recordedBytesMatch( logFile, recorded, coverage ) ) {
        return IndexFit::Changed;
    }
    return size > recorded.size ? IndexFit::Grown : IndexFit::Unchanged;
}
