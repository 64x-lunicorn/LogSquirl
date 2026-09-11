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

// The Index cache hard-wires its own location on disk in production (a
// path derived from QStandardPaths), so these tests redirect it to a
// QTemporaryDir via IndexCache::setCacheDirOverride() rather than touching
// the developer's real cache directory.

namespace {

class CacheDirOverride {
public:
    explicit CacheDirOverride( const QString& dir )
    {
        IndexCache::setCacheDirOverride( dir );
    }

    ~CacheDirOverride()
    {
        IndexCache::setCacheDirOverride( QString() );
    }

    CacheDirOverride( const CacheDirOverride& ) = delete;
    CacheDirOverride& operator=( const CacheDirOverride& ) = delete;
};

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
    CacheDirOverride override( tempDir.filePath( "index-cache" ) );

    GIVEN( "a saved index" )
    {
        const QString sourcePath = "/some/log/file.log";
        const auto linePositions = makeLinePositions( { 4, 8, 20 } );
        const auto hash = makeHash( 20 );

        REQUIRE( IndexCache::trySave( sourcePath, linePositions, LineLength( 8 ), hash, "UTF-8",
                                      false ) );

        WHEN( "the same path is loaded back" )
        {
            const auto loaded = IndexCache::tryLoad( sourcePath );

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
            const auto loaded = IndexCache::tryLoad( "/some/other/file.log" );

            THEN( "it is a miss" )
            {
                REQUIRE_FALSE( loaded.has_value() );
            }
        }

        WHEN( "the entry is invalidated and removed, as the caller does on a hash mismatch" )
        {
            IndexCache::remove( sourcePath );
            const auto loaded = IndexCache::tryLoad( sourcePath );

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
        const auto cacheDir = IndexCache::cacheDir();
        const auto baseTime = QDateTime::currentDateTime();
        for ( int i = 0; i < sourcePaths.size(); ++i ) {
            QStringList before;
            {
                QDirIterator it( cacheDir, { "*.idx" }, QDir::Files );
                while ( it.hasNext() ) {
                    before << it.next();
                }
            }

            REQUIRE( IndexCache::trySave( sourcePaths[ i ], linePositions, LineLength( 30 ), hash,
                                          "UTF-8", false ) );

            QStringList after;
            {
                QDirIterator it( cacheDir, { "*.idx" }, QDir::Files );
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

        const auto totalSizeBeforeEviction = IndexCache::totalCacheSize();
        REQUIRE( totalSizeBeforeEviction > 0 );

        WHEN( "eviction runs with a budget that fits only the newest entry" )
        {
            const auto perEntrySize = totalSizeBeforeEviction / sourcePaths.size();
            IndexCache::evict( perEntrySize + perEntrySize / 2 );

            THEN( "the oldest entries are gone and the newest survives" )
            {
                REQUIRE_FALSE( IndexCache::tryLoad( "/oldest.log" ).has_value() );
                REQUIRE_FALSE( IndexCache::tryLoad( "/middle.log" ).has_value() );
                REQUIRE( IndexCache::tryLoad( "/newest.log" ).has_value() );
                REQUIRE( IndexCache::totalCacheSize() <= totalSizeBeforeEviction );
            }
        }
    }
}
