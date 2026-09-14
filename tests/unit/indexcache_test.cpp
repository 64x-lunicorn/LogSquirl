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

#include <catch2/catch.hpp>

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include "indexcache.h"
#include "linetypes.h"

// The Index cache is told its directory, so every test here builds its own
// cache on a QTemporaryDir of its own: nothing is shared between tests, and
// the developer's real cache directory is never touched.

namespace {

LinePositionArray makeLinePositions( std::initializer_list<qint64> offsets )
{
    LinePositionArray array;
    for ( const auto offset : offsets ) {
        array.append( OffsetInFile( offset ) );
    }
    return array;
}

IndexedHash makeHash( qint64 size )
{
    IndexedHash hash;
    hash.size = size;
    hash.fullDigest = static_cast<quint64>( size ) * 31u + 7u;
    hash.headerSize = size;
    hash.headerDigest = hash.fullDigest;
    hash.tailSize = 0;
    hash.tailOffset = 0;
    hash.tailDigest = 0;
    return hash;
}

} // namespace

SCENARIO( "The Index cache stores and retrieves indices at a temporary location", "[indexcache]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const IndexCache cache{ tempDir.filePath( "index-cache" ) };

    GIVEN( "a saved index" )
    {
        const QString sourcePath = "/some/log/file.log";
        const auto linePositions = makeLinePositions( { 4, 8, 20 } );
        const auto hash = makeHash( 20 );

        REQUIRE(
            cache.trySave( sourcePath, linePositions, LineLength( 8 ), hash, "UTF-8", false ) );

        WHEN( "the same path is loaded back" )
        {
            const auto loaded = cache.tryLoad( sourcePath );

            THEN( "a hit returns the data that was saved" )
            {
                REQUIRE( loaded.has_value() );
                REQUIRE( loaded->hash.size == hash.size );
                REQUIRE( loaded->hash.fullDigest == hash.fullDigest );
                REQUIRE( loaded->maxLength == LineLength( 8 ) );
                REQUIRE( loaded->encodingName == "UTF-8" );
                REQUIRE( loaded->linePosition.size() == linePositions.size() );
                for ( auto i = 0u; i < linePositions.size().get(); ++i ) {
                    REQUIRE( loaded->linePosition.at( i ) == linePositions.at( i ) );
                }
            }
        }

        WHEN( "the cache is asked for a path that was never saved" )
        {
            const auto loaded = cache.tryLoad( "/some/other/file.log" );

            THEN( "it is a miss" )
            {
                REQUIRE_FALSE( loaded.has_value() );
            }
        }

        WHEN( "the entry is invalidated and removed, as the caller does on a hash mismatch" )
        {
            cache.remove( sourcePath );
            const auto loaded = cache.tryLoad( sourcePath );

            THEN( "it is a miss from then on" )
            {
                REQUIRE_FALSE( loaded.has_value() );
            }
        }
    }

    GIVEN( "several cache entries with distinct ages, over the eviction budget" )
    {
        const auto linePositions = makeLinePositions( { 4, 8, 20, 4000, 20000, 20050 } );
        const auto hash = makeHash( 20050 );

        const QStringList sourcePaths = { "/oldest.log", "/middle.log", "/newest.log" };

        // Cache files written in quick succession can share a filesystem
        // mtime, which would make the LRU order under evict() ambiguous.
        // Save one entry at a time and set its mtime explicitly from the
        // single new file that save produced, rather than sorting the
        // directory listing afterwards -- the cache filename is a hash of
        // the source path, unrelated to save order or path spelling, so
        // there is no other reliable way to know which file is which.
        const auto baseTime = QDateTime::currentDateTime();
        for ( int i = 0; i < sourcePaths.size(); ++i ) {
            QStringList before;
            {
                QDirIterator it( cache.directory(), { "*.idx" }, QDir::Files );
                while ( it.hasNext() ) {
                    before << it.next();
                }
            }

            REQUIRE( cache.trySave( sourcePaths[ i ], linePositions, LineLength( 30 ), hash,
                                    "UTF-8", false ) );

            QStringList after;
            {
                QDirIterator it( cache.directory(), { "*.idx" }, QDir::Files );
                while ( it.hasNext() ) {
                    after << it.next();
                }
            }
            for ( const auto& path : before ) {
                after.removeOne( path );
            }
            REQUIRE( after.size() == 1 );

            QFile file( after.first() );
            REQUIRE( file.open( QIODevice::ReadOnly ) );
            file.setFileTime( baseTime.addSecs( i * 60 ), QFileDevice::FileModificationTime );
            file.close();
        }

        const auto totalSizeBeforeEviction = cache.totalCacheSize();
        REQUIRE( totalSizeBeforeEviction > 0 );

        WHEN( "eviction runs with a budget that fits only the newest entry" )
        {
            const auto perEntrySize = totalSizeBeforeEviction / sourcePaths.size();
            cache.evict( perEntrySize + perEntrySize / 2 );

            THEN( "the oldest entries are gone and the newest survives" )
            {
                REQUIRE_FALSE( cache.tryLoad( "/oldest.log" ).has_value() );
                REQUIRE_FALSE( cache.tryLoad( "/middle.log" ).has_value() );
                REQUIRE( cache.tryLoad( "/newest.log" ).has_value() );
                REQUIRE( cache.totalCacheSize() <= totalSizeBeforeEviction );
            }
        }
    }
}

SCENARIO( "Index caches at different locations do not see each other", "[indexcache]" )
{
    QTemporaryDir firstDir;
    QTemporaryDir secondDir;
    REQUIRE( firstDir.isValid() );
    REQUIRE( secondDir.isValid() );

    const IndexCache first{ firstDir.path() };
    const IndexCache second{ secondDir.path() };

    GIVEN( "an index saved to the first cache" )
    {
        const QString sourcePath = "/shared/name.log";
        REQUIRE( first.trySave( sourcePath, makeLinePositions( { 4, 8 } ), LineLength( 4 ),
                                makeHash( 8 ), "UTF-8", false ) );

        THEN( "the second cache misses it, holds nothing and clears nothing of the first" )
        {
            REQUIRE_FALSE( second.tryLoad( sourcePath ).has_value() );
            REQUIRE( second.totalCacheSize() == 0 );
            REQUIRE( second.clearAll() == 0 );
            REQUIRE( first.tryLoad( sourcePath ).has_value() );
        }
    }
}

SCENARIO( "An Index cache given no directory stores nothing", "[indexcache]" )
{
    // An underived Indexing Policy carries an empty directory. Resolving
    // that against the working directory, or the filesystem root, would
    // scatter cache files wherever the process happens to run.
    const IndexCache cache{ QString{} };

    THEN( "saving fails and loading misses" )
    {
        REQUIRE_FALSE( cache.trySave( "/some/file.log", makeLinePositions( { 4 } ), LineLength( 4 ),
                                      makeHash( 4 ), "UTF-8", false ) );
        REQUIRE_FALSE( cache.tryLoad( "/some/file.log" ).has_value() );
        REQUIRE( cache.totalCacheSize() == 0 );
        REQUIRE( cache.clearAll() == 0 );
    }
}
