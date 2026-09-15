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

#include <limits>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "filedigest.h"
#include "indexcache.h"
#include "linetypes.h"

// The Index cache is told its directory, so every test here builds its own
// cache on a QTemporaryDir of its own: nothing is shared between tests, and
// the developer's real cache directory is never touched.
//
// The cache only hands out an Index while the bytes of the Log File it was
// built from are unchanged (the file may have grown since), so the
// Log Files here are real files, and the hash stored with each Index is
// computed from them the way the indexer computes it.

namespace {

constexpr qint64 DigestBlockSize = 5 * 1024 * 1024;
constexpr qint64 NoBudgetLimit = std::numeric_limits<qint64>::max();

LinePositionArray makeLinePositions( std::initializer_list<qint64> offsets )
{
    LinePositionArray array;
    for ( const auto offset : offsets ) {
        array.append( OffsetInFile( offset ) );
    }
    return array;
}

const auto SomeLinePositions = makeLinePositions( { 4, 8, 20, 4000, 20000, 20050 } );

void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( content ) == content.size() );
}

quint64 digestOf( const QByteArray& data )
{
    FileDigest digest;
    digest.addData( data.constData(), static_cast<size_t>( data.size() ) );
    return digest.digest();
}

// The hash the indexer records for a Log File: a digest of its first block
// and, for a file longer than one block, of its last block.
IndexedHash hashOfFile( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto content = file.readAll();

    IndexedHash hash;
    hash.size = content.size();
    hash.fullDigest = digestOf( content );

    const auto header = content.left( DigestBlockSize );
    hash.headerSize = header.size();
    hash.headerDigest = digestOf( header );

    if ( content.size() <= DigestBlockSize ) {
        hash.tailOffset = 0;
        hash.tailSize = header.size();
        hash.tailDigest = hash.headerDigest;
    }
    else {
        hash.tailOffset = content.size() - DigestBlockSize;
        const auto tail = content.mid( hash.tailOffset );
        hash.tailSize = tail.size();
        hash.tailDigest = digestOf( tail );
    }
    return hash;
}

bool store( const IndexCache& cache, const QString& logFile,
            const LinePositionArray& linePositions = SomeLinePositions )
{
    return cache.trySave( logFile, linePositions, LineLength( 30 ), hashOfFile( logFile ), "UTF-8",
                          false );
}

QStringList cacheFiles( const QString& directory )
{
    return QDir( directory ).entryList( { QStringLiteral( "*.idx" ) }, QDir::Files );
}

// Stores an Index and dates the cache file that store produced. The cache
// file's name is a hash of the Log File's path, so the only reliable way to
// tell which file is the new one is to compare the directory before and after.
void storeWrittenAt( const IndexCache& cache, const QString& directory, const QString& logFile,
                     const QDateTime& writtenAt )
{
    const auto before = cacheFiles( directory );
    REQUIRE( store( cache, logFile ) );
    auto after = cacheFiles( directory );
    for ( const auto& name : before ) {
        after.removeOne( name );
    }
    REQUIRE( after.size() == 1 );

    QFile file( QDir( directory ).filePath( after.first() ) );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.setFileTime( writtenAt, QFileDevice::FileModificationTime ) );
}

qint64 sizeOfOneEntry( const QString& logFile )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const IndexCache cache{ scratch.path(), QString{}, NoBudgetLimit };
    REQUIRE( store( cache, logFile ) );
    return cache.totalCacheSize();
}

} // namespace

SCENARIO( "The Index cache hands out an Index only while it fits its Log File", "[indexcache]" )
{
    QTemporaryDir cacheDir;
    QTemporaryDir logDir;
    REQUIRE( cacheDir.isValid() );
    REQUIRE( logDir.isValid() );
    const IndexCache cache{ cacheDir.path(), QString{}, NoBudgetLimit };

    GIVEN( "an Index stored for a Log File shorter than one digest block" )
    {
        const auto logFile = logDir.filePath( "short.log" );
        writeFile( logFile, "first line\nsecond line\nthird line\n" );
        const auto hash = hashOfFile( logFile );
        REQUIRE( store( cache, logFile ) );

        WHEN( "the Log File is unchanged" )
        {
            const auto loaded = cache.tryLoad( logFile );

            THEN( "the Index is loaded back as it was stored" )
            {
                REQUIRE( loaded.has_value() );
                REQUIRE( loaded->hash.size == hash.size );
                REQUIRE( loaded->hash.headerDigest == hash.headerDigest );
                REQUIRE( loaded->maxLength == LineLength( 30 ) );
                REQUIRE( loaded->encodingName == "UTF-8" );
                REQUIRE( loaded->linePosition.size() == SomeLinePositions.size() );
                for ( auto i = 0u; i < SomeLinePositions.size().get(); ++i ) {
                    REQUIRE( loaded->linePosition.at( i ) == SomeLinePositions.at( i ) );
                }
            }
        }

        WHEN( "the cache is asked for a Log File it holds nothing for" )
        {
            const auto other = logDir.filePath( "other.log" );
            writeFile( other, "unrelated\n" );

            THEN( "it is a miss" )
            {
                REQUIRE_FALSE( cache.tryLoad( other ).has_value() );
            }
        }

        WHEN( "the Log File's header changes but its size does not" )
        {
            writeFile( logFile, "FIRST line\nsecond line\nthird line\n" );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( loaded.has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }

        WHEN( "the Log File grows and the bytes it was indexed from are unchanged" )
        {
            writeFile( logFile, "first line\nsecond line\nthird line\nfourth line\n" );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "the Index is loaded back, for the size it was built at" )
            {
                REQUIRE( loaded.has_value() );
                REQUIRE( loaded->hash.size == hash.size );
                REQUIRE( loaded->linePosition.size() == SomeLinePositions.size() );
                REQUIRE( cacheFiles( cacheDir.path() ).size() == 1 );
            }
        }

        WHEN( "the Log File grows and its header changes" )
        {
            writeFile( logFile, "FIRST line\nsecond line\nthird line\nfourth line\n" );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( loaded.has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }

        WHEN( "the Log File shrinks" )
        {
            writeFile( logFile, "first line\nsecond line\n" );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( loaded.has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }

#ifndef Q_OS_WIN
        WHEN( "the Log File exists but cannot be opened for now" )
        {
            REQUIRE( QFile::setPermissions( logFile, QFileDevice::Permissions{} ) );
            // A process running with root privileges opens it all the same.
            const bool lockedOut = !QFile( logFile ).open( QIODevice::ReadOnly );
            const auto loaded = cache.tryLoad( logFile );
            REQUIRE( QFile::setPermissions( logFile,
                                            QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );

            THEN( "nothing is loaded, but the entry is kept for when it can be" )
            {
                if ( lockedOut ) {
                    REQUIRE_FALSE( loaded.has_value() );
                    REQUIRE( cacheFiles( cacheDir.path() ).size() == 1 );
                    REQUIRE( cache.tryLoad( logFile ).has_value() );
                }
                else {
                    WARN( "the Log File stayed readable, so there is nothing to check" );
                }
            }
        }
#endif

        WHEN( "the Log File is deleted" )
        {
            REQUIRE( QFile::remove( logFile ) );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( cache.tryLoad( logFile ).has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }

        WHEN( "the cache file is deleted before it is opened, as a concurrent eviction would" )
        {
            for ( const auto& name : cacheFiles( cacheDir.path() ) ) {
                REQUIRE( QFile::remove( QDir( cacheDir.path() ).filePath( name ) ) );
            }

            THEN( "it is a miss" )
            {
                REQUIRE_FALSE( cache.tryLoad( logFile ).has_value() );
            }
        }

        WHEN( "the cache file is not an Index" )
        {
            const auto names = cacheFiles( cacheDir.path() );
            REQUIRE( names.size() == 1 );
            writeFile( QDir( cacheDir.path() ).filePath( names.first() ), "garbage" );

            THEN( "nothing is loaded and the unreadable entry is gone" )
            {
                REQUIRE_FALSE( cache.tryLoad( logFile ).has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }
    }

    GIVEN( "an Index stored for a Log File longer than one digest block" )
    {
        const auto logFile = logDir.filePath( "long.log" );
        auto content = QByteArray( DigestBlockSize + DigestBlockSize / 2, 'x' );
        writeFile( logFile, content );
        REQUIRE( store( cache, logFile ) );

        THEN( "it is loaded back while the Log File is unchanged" )
        {
            REQUIRE( cache.tryLoad( logFile ).has_value() );
        }

        WHEN( "the Log File grows and its stored tail range is unchanged" )
        {
            writeFile( logFile, content + QByteArray( 1000, 'z' ) );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "the Index is loaded back, for the size it was built at" )
            {
                REQUIRE( loaded.has_value() );
                REQUIRE( loaded->hash.size == content.size() );
            }
        }

        WHEN( "the Log File grows and a byte in its stored tail range changes" )
        {
            content[ content.size() - 1 ] = 'y';
            writeFile( logFile, content + QByteArray( 1000, 'z' ) );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( loaded.has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }

        WHEN( "only the Log File's tail changes" )
        {
            content[ content.size() - 1 ] = 'y';
            writeFile( logFile, content );
            const auto loaded = cache.tryLoad( logFile );

            THEN( "nothing is loaded and the stale entry is gone" )
            {
                REQUIRE_FALSE( loaded.has_value() );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }
    }
}

SCENARIO( "The Index cache decides for itself what it does not keep", "[indexcache]" )
{
    QTemporaryDir cacheDir;
    QTemporaryDir logDir;
    QTemporaryDir excludedDir;
    REQUIRE( cacheDir.isValid() );
    REQUIRE( logDir.isValid() );
    REQUIRE( excludedDir.isValid() );

    GIVEN( "a cache that excludes a directory" )
    {
        const IndexCache cache{ cacheDir.path(), excludedDir.path(), NoBudgetLimit };

        WHEN( "an Index is stored for a Log File under the excluded directory" )
        {
            QDir( excludedDir.path() ).mkpath( "nested" );
            const auto logFile = excludedDir.filePath( "nested/excluded.log" );
            writeFile( logFile, "line\n" );
            const auto stored = store( cache, logFile );

            THEN( "nothing is written and nothing is loaded" )
            {
                REQUIRE_FALSE( stored );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
                REQUIRE_FALSE( cache.tryLoad( logFile ).has_value() );
            }
        }

#ifndef Q_OS_WIN
        WHEN( "an Index is stored for a Log File reached through a link to the excluded directory" )
        {
            const auto link = logDir.filePath( "link-to-excluded" );
            REQUIRE( QFile::link( excludedDir.path(), link ) );
            writeFile( excludedDir.filePath( "linked.log" ), "line\n" );
            const auto logFile = link + QStringLiteral( "/linked.log" );
            const auto stored = store( cache, logFile );

            THEN(
                "nothing is written, as the Log File is under the excluded directory all the same" )
            {
                REQUIRE_FALSE( stored );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }
#endif

        WHEN( "an Index is stored for a Log File in a sibling directory sharing the name's prefix" )
        {
            const auto sibling = excludedDir.path() + QStringLiteral( "-sibling" );
            REQUIRE( QDir().mkpath( sibling ) );
            const auto logFile = sibling + QStringLiteral( "/kept.log" );
            writeFile( logFile, "line\n" );
            const auto stored = store( cache, logFile );
            const auto loaded = cache.tryLoad( logFile );
            QDir( sibling ).removeRecursively();

            THEN( "it is kept, since the Log File is not under the excluded directory" )
            {
                REQUIRE( stored );
                REQUIRE( loaded.has_value() );
            }
        }

        WHEN( "an empty Index is stored" )
        {
            const auto logFile = logDir.filePath( "empty.log" );
            writeFile( logFile, "" );
            const auto stored = store( cache, logFile, LinePositionArray{} );

            THEN( "nothing is written" )
            {
                REQUIRE_FALSE( stored );
                REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
            }
        }
    }

    GIVEN( "a cache with a budget of 0" )
    {
        const IndexCache cache{ cacheDir.path(), QString{}, 0 };
        const auto logFile = logDir.filePath( "some.log" );
        writeFile( logFile, "line\n" );

        THEN( "it stores nothing" )
        {
            REQUIRE_FALSE( store( cache, logFile ) );
            REQUIRE( cacheFiles( cacheDir.path() ).isEmpty() );
        }
    }

    GIVEN( "Log Files whose Indexes are all the same size" )
    {
        const QStringList names = { "first.log", "second.log", "third.log" };
        QStringList logFiles;
        for ( const auto& name : names ) {
            logFiles << logDir.filePath( name );
            writeFile( logFiles.last(), name.toUtf8() + "\n" );
        }
        const auto entrySize = sizeOfOneEntry( logFiles.first() );

        AND_GIVEN( "a budget that fits two entries, holding the first two" )
        {
            const IndexCache cache{ cacheDir.path(), QString{}, 2 * entrySize + entrySize / 2 };

            // The first was written well before the second. Dated explicitly,
            // as two files written in quick succession can share an mtime.
            const auto now = QDateTime::currentDateTime();
            storeWrittenAt( cache, cacheDir.path(), logFiles[ 0 ], now.addSecs( -2 * 3600 ) );
            storeWrittenAt( cache, cacheDir.path(), logFiles[ 1 ], now.addSecs( -1 * 3600 ) );

            WHEN( "the first is loaded, then a third is stored" )
            {
                REQUIRE( cache.tryLoad( logFiles[ 0 ] ).has_value() );
                REQUIRE( store( cache, logFiles[ 2 ] ) );

                THEN( "the least recently loaded entry is evicted, not the least recently written" )
                {
                    REQUIRE( cacheFiles( cacheDir.path() ).size() == 2 );
                    REQUIRE_FALSE( cache.tryLoad( logFiles[ 1 ] ).has_value() );
                    REQUIRE( cache.tryLoad( logFiles[ 0 ] ).has_value() );
                    REQUIRE( cache.tryLoad( logFiles[ 2 ] ).has_value() );
                }
            }

            WHEN( "a third is stored without loading either" )
            {
                REQUIRE( store( cache, logFiles[ 2 ] ) );

                THEN( "the oldest entry is evicted and the one just written is kept" )
                {
                    REQUIRE( cacheFiles( cacheDir.path() ).size() == 2 );
                    REQUIRE_FALSE( cache.tryLoad( logFiles[ 0 ] ).has_value() );
                    REQUIRE( cache.tryLoad( logFiles[ 1 ] ).has_value() );
                    REQUIRE( cache.tryLoad( logFiles[ 2 ] ).has_value() );
                }
            }
        }

        AND_GIVEN( "a budget that fits one entry, holding the first" )
        {
            const IndexCache cache{ cacheDir.path(), QString{}, entrySize + entrySize / 2 };
            REQUIRE( store( cache, logFiles[ 0 ] ) );

            WHEN( "an Index larger than the whole budget is stored" )
            {
                // Irregular line lengths, so the positions do not compress
                // down to next to nothing.
                LinePositionArray large;
                qint64 offset = 0;
                quint32 state = 12345;
                for ( int line = 0; line < 100000; ++line ) {
                    state = state * 1664525u + 1013904223u;
                    offset += 1 + ( state >> 8 ) % 5000;
                    large.append( OffsetInFile( offset ) );
                }
                const auto stored = store( cache, logFiles[ 1 ], large );

                THEN( "it is not written, and the existing entry is left untouched" )
                {
                    REQUIRE_FALSE( stored );
                    REQUIRE( cacheFiles( cacheDir.path() ).size() == 1 );
                    REQUIRE( cache.tryLoad( logFiles[ 0 ] ).has_value() );
                }
            }
        }
    }
}

SCENARIO( "Index caches at different locations do not see each other", "[indexcache]" )
{
    QTemporaryDir firstDir;
    QTemporaryDir secondDir;
    QTemporaryDir logDir;
    REQUIRE( firstDir.isValid() );
    REQUIRE( secondDir.isValid() );
    REQUIRE( logDir.isValid() );

    const IndexCache first{ firstDir.path(), QString{}, NoBudgetLimit };
    const IndexCache second{ secondDir.path(), QString{}, NoBudgetLimit };

    GIVEN( "an index saved to the first cache" )
    {
        const auto logFile = logDir.filePath( "name.log" );
        writeFile( logFile, "line\n" );
        REQUIRE( store( first, logFile ) );

        THEN( "the second cache misses it, holds nothing and clears nothing of the first" )
        {
            REQUIRE_FALSE( second.tryLoad( logFile ).has_value() );
            REQUIRE( second.totalCacheSize() == 0 );
            REQUIRE( second.clearAll() == 0 );
            REQUIRE( first.tryLoad( logFile ).has_value() );
        }
    }
}

SCENARIO( "An Index cache given no directory stores nothing", "[indexcache]" )
{
    // An underived Indexing Policy carries an empty directory, and so does a
    // Policy with the cache turned off. Resolving that against the working
    // directory, or the filesystem root, would scatter cache files wherever
    // the process happens to run.
    QTemporaryDir logDir;
    REQUIRE( logDir.isValid() );
    const auto logFile = logDir.filePath( "some.log" );
    writeFile( logFile, "line\n" );

    const IndexCache cache{ QString{}, QString{}, NoBudgetLimit };

    THEN( "saving fails and loading misses" )
    {
        REQUIRE_FALSE( store( cache, logFile ) );
        REQUIRE_FALSE( cache.tryLoad( logFile ).has_value() );
        REQUIRE( cache.totalCacheSize() == 0 );
        REQUIRE( cache.clearAll() == 0 );
    }
}
