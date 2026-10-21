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

// A compressed Log File or an archive is decompressed up to a budget of
// bytes and no further, however far it would expand (#816). A tar under
// zstd or lz4 is parsed and extracted through the same budgeted stream,
// which seeks over the sequential decompressor as the tar reader needs
// (#838).

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <kcompressiondevice.h>
#include <ktar.h>
#include <kzip.h>
#include <lz4frame.h>
#include <zstd.h>

#include "archiveextraction.h"
#include "atomicflag.h"
#include "budgeteddevice.h"
#include "decompressor.h"
#include "lz4device.h"
#include "zstddevice.h"

namespace {

constexpr qint64 KiB = 1024;
constexpr qint64 MiB = 1024 * KiB;

// Expands past a budget of 1 MiB many times over: 8 MiB of one byte, a few
// KB compressed under every codec.
QByteArray expanding()
{
    return QByteArray( 8 * MiB, 'x' );
}

const QByteArray LogLines = "first Log Line\nsecond Log Line\n";

bool writeFile( const QString& path, const QByteArray& data )
{
    QFile file( path );
    return file.open( QIODevice::WriteOnly ) && file.write( data ) == data.size();
}

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
}

// Writes `data` compressed by KArchive's filters: gz, bz2 and xz.
bool writeFiltered( const QString& path, KCompressionDevice::CompressionType type,
                    const QByteArray& data )
{
    KCompressionDevice device( path, type );
    return device.open( QIODevice::WriteOnly ) && device.write( data ) == data.size();
}

QByteArray zstdCompressed( const QByteArray& data )
{
    const auto bound = ZSTD_compressBound( static_cast<std::size_t>( data.size() ) );
    QByteArray compressed( static_cast<qsizetype>( bound ), Qt::Uninitialized );
    const auto size = ZSTD_compress( compressed.data(), bound, data.constData(),
                                     static_cast<std::size_t>( data.size() ), 1 );
    if ( ZSTD_isError( size ) ) {
        return {};
    }
    compressed.truncate( static_cast<qsizetype>( size ) );
    return compressed;
}

QByteArray lz4Compressed( const QByteArray& data )
{
    const auto bound = LZ4F_compressFrameBound( static_cast<std::size_t>( data.size() ), nullptr );
    QByteArray compressed( static_cast<qsizetype>( bound ), Qt::Uninitialized );
    const auto size = LZ4F_compressFrame( compressed.data(), bound, data.constData(),
                                          static_cast<std::size_t>( data.size() ), nullptr );
    if ( LZ4F_isError( size ) ) {
        return {};
    }
    compressed.truncate( static_cast<qsizetype>( size ) );
    return compressed;
}

bool writeZstd( const QString& path, const QByteArray& data )
{
    const auto compressed = zstdCompressed( data );
    return !compressed.isEmpty() && writeFile( path, compressed );
}

bool writeLz4( const QString& path, const QByteArray& data )
{
    const auto compressed = lz4Compressed( data );
    return !compressed.isEmpty() && writeFile( path, compressed );
}

// `size` bytes that neither repeat nor compress away: a tar member whose
// data a tar reader mistaken for headers would never parse as such, and a
// stream whose every position holds its own bytes (#838).
QByteArray noise( qsizetype size, std::uint64_t seed )
{
    QByteArray bytes( size, Qt::Uninitialized );
    auto state = seed;
    for ( auto& byte : bytes ) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        byte = static_cast<char>( state >> 56 );
    }
    return bytes;
}

using Members = std::vector<std::pair<QString, QByteArray>>;

// A plain tar of the members, built in memory.
QByteArray tarOf( const Members& members )
{
    QBuffer buffer;
    {
        KTar tar( &buffer );
        REQUIRE( tar.open( QIODevice::WriteOnly ) );
        for ( const auto& [ name, data ] : members ) {
            REQUIRE( tar.writeFile( name, data ) );
        }
        REQUIRE( tar.close() );
    }
    return buffer.data();
}

// Writes a tar, compressed as its name says, of the given entries.
void writeTar( const QString& path, const std::function<void( KArchive& )>& entries )
{
    KTar tar( path );
    REQUIRE( tar.open( QIODevice::WriteOnly ) );
    entries( tar );
    REQUIRE( tar.close() );
}

void writeZip( const QString& path, const std::function<void( KArchive& )>& entries )
{
    KZip zip( path );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    entries( zip );
    REQUIRE( zip.close() );
}

// The bytes of every file below `folder`.
qint64 bytesBelow( const QString& folder )
{
    qint64 bytes = 0;
    QDirIterator it{ folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                     QDirIterator::Subdirectories };
    while ( it.hasNext() ) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

// The temporary files the tar reader would copy a decompressed tarball
// into, by name; none is ever created (#816).
QStringList tarReaderCopies()
{
    auto copies = QDir{ QDir::tempPath() }.entryList( { "ktar-*.tar" }, QDir::Files );
    copies.sort();
    return copies;
}

struct Outcome {
    bool started = false;
    bool finishedInTime = false;
    bool succeeded = false;
    DecompressionFailure failure = DecompressionFailure::None;
};

// Runs `start` on a Decompressor with the budget, as the main window does,
// and waits for it to finish.
Outcome run( qint64 budgetBytes, const std::function<bool( Decompressor&, AtomicFlag& )>& start )
{
    AtomicFlag interrupt;
    Decompressor decompressor{ budgetBytes };
    Outcome outcome;

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    QObject::connect( &decompressor, &Decompressor::finished, &loop, [ & ]( bool succeeded ) {
        outcome.finishedInTime = true;
        outcome.succeeded = succeeded;
        loop.quit();
    } );

    outcome.started = start( decompressor, interrupt );
    if ( !outcome.started ) {
        return outcome;
    }
    deadline.start( std::chrono::seconds{ 30 } );
    loop.exec();
    if ( outcome.finishedInTime ) {
        outcome.failure = decompressor.failure();
    }
    else {
        interrupt.set();
        decompressor.waitForResult();
    }
    return outcome;
}

Outcome decompress( const QString& path, QFile& output, qint64 budgetBytes )
{
    return run( budgetBytes, [ & ]( Decompressor& decompressor, AtomicFlag& interrupt ) {
        return decompressor.decompress( path, &output, interrupt );
    } );
}

Outcome extract( const QString& path, const QString& folder, qint64 budgetBytes )
{
    return run( budgetBytes, [ & ]( Decompressor& decompressor, AtomicFlag& interrupt ) {
        return decompressor.extract( path, folder, interrupt );
    } );
}

} // namespace

SCENARIO( "A compressed Log File is decompressed up to the budget and no further",
          "[compression][budget]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto data = expanding();

    struct Input {
        const char* name;
        std::function<bool( const QString& )> write;
    };
    const Input inputs[] = {
        { "big.log.gz",
          [ & ]( const QString& path ) {
              return writeFiltered( path, KCompressionDevice::GZip, data );
          } },
        { "big.log.bz2",
          [ & ]( const QString& path ) {
              return writeFiltered( path, KCompressionDevice::BZip2, data );
          } },
        { "big.log.xz",
          [ & ]( const QString& path ) {
              return writeFiltered( path, KCompressionDevice::Xz, data );
          } },
        { "big.log.zst", [ & ]( const QString& path ) { return writeZstd( path, data ); } },
        { "big.log.lz4", [ & ]( const QString& path ) { return writeLz4( path, data ); } },
    };

    for ( const auto& input : inputs ) {
        GIVEN( input.name )
        {
            const auto path = scratch.filePath( input.name );
            REQUIRE( input.write( path ) );
            REQUIRE( QFileInfo{ path }.size() < 1 * MiB );

            WHEN( "it is decompressed under a budget it expands past" )
            {
                QFile output{ scratch.filePath( "big.log" ) };
                REQUIRE( output.open( QIODevice::WriteOnly ) );
                const auto outcome = decompress( path, output, 1 * MiB );

                THEN( "it stops at the budget, and says so" )
                {
                    REQUIRE( outcome.started );
                    REQUIRE( outcome.finishedInTime );
                    REQUIRE_FALSE( outcome.succeeded );
                    REQUIRE( outcome.failure == DecompressionFailure::BudgetExceeded );
                    REQUIRE( QFileInfo{ output.fileName() }.size() <= 1 * MiB );
                }
            }

            WHEN( "it is decompressed under a budget it fits" )
            {
                QFile output{ scratch.filePath( "big.log" ) };
                REQUIRE( output.open( QIODevice::WriteOnly ) );
                const auto outcome = decompress( path, output, 8 * MiB );

                THEN( "it is decompressed whole" )
                {
                    REQUIRE( outcome.finishedInTime );
                    REQUIRE( outcome.succeeded );
                    REQUIRE( outcome.failure == DecompressionFailure::None );
                    REQUIRE( QFileInfo{ output.fileName() }.size() == data.size() );
                }
            }
        }
    }
}

SCENARIO( "An archive is extracted up to the budget as a whole", "[compression][budget]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto folder = scratch.filePath( "extracted" );

    GIVEN( "a tar.gz whose one entry expands past the budget" )
    {
        const auto path = scratch.filePath( "big.tar.gz" );
        writeTar( path,
                  [ & ]( KArchive& tar ) { REQUIRE( tar.writeFile( "big.log", expanding() ) ); } );
        REQUIRE( QFileInfo{ path }.size() < 1 * MiB );
        const auto copiesBefore = tarReaderCopies();

        WHEN( "it is extracted" )
        {
            const auto outcome = extract( path, folder, 1 * MiB );

            THEN( "it stops at the budget, the tar reader copying nothing on its own" )
            {
                REQUIRE( outcome.started );
                REQUIRE( outcome.finishedInTime );
                REQUIRE_FALSE( outcome.succeeded );
                REQUIRE( outcome.failure == DecompressionFailure::BudgetExceeded );
                REQUIRE( bytesBelow( folder ) <= 1 * MiB );
                REQUIRE( tarReaderCopies() == copiesBefore );
            }
        }
    }

    GIVEN( "a tar.gz within the budget" )
    {
        const auto path = scratch.filePath( "small.tar.gz" );
        writeTar( path, [ & ]( KArchive& tar ) {
            REQUIRE( tar.writeFile( "a/app.log", LogLines ) );
            REQUIRE( tar.writeFile( "other.log", LogLines ) );
        } );

        WHEN( "it is extracted" )
        {
            const auto outcome = extract( path, folder, 1 * MiB );

            THEN( "every entry is extracted" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE( outcome.succeeded );
                REQUIRE( readFile( QDir{ folder }.filePath( "a/app.log" ) ) == LogLines );
                REQUIRE( readFile( QDir{ folder }.filePath( "other.log" ) ) == LogLines );
            }
        }
    }

    GIVEN( "a zip of two entries that fit the budget one by one but not together" )
    {
        const auto path = scratch.filePath( "two.zip" );
        const QByteArray entry( 600 * KiB, 'x' );
        writeZip( path, [ & ]( KArchive& zip ) {
            REQUIRE( zip.writeFile( "first.log", entry ) );
            REQUIRE( zip.writeFile( "second.log", entry ) );
        } );

        WHEN( "it is extracted under a budget of 1 MiB" )
        {
            const auto outcome = extract( path, folder, 1 * MiB );

            THEN( "the budget counts the archive as a whole" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE_FALSE( outcome.succeeded );
                REQUIRE( outcome.failure == DecompressionFailure::BudgetExceeded );
                REQUIRE( bytesBelow( folder ) <= 1 * MiB );
            }
        }

        WHEN( "it is extracted under a budget of 2 MiB" )
        {
            const auto outcome = extract( path, folder, 2 * MiB );

            THEN( "both entries are extracted" )
            {
                REQUIRE( outcome.finishedInTime );
                REQUIRE( outcome.succeeded );
                REQUIRE( bytesBelow( folder ) == 2 * entry.size() );
            }
        }

        WHEN( "its directory is extracted directly" )
        {
            KZip zip{ path };
            REQUIRE( zip.open( QIODevice::ReadOnly ) );
            AtomicFlag interrupt;

            THEN( "the walk reports the budget" )
            {
                REQUIRE( extractArchiveDirectory( *zip.directory(), folder, 1 * MiB, interrupt )
                         == DecompressionFailure::BudgetExceeded );
                REQUIRE( bytesBelow( folder ) <= 1 * MiB );
                REQUIRE( extractArchiveDirectory( *zip.directory(), folder, 2 * MiB, interrupt )
                         == DecompressionFailure::None );
            }
        }
    }
}

SCENARIO( "A tar.zst or tar.lz4 extracts its members whole", "[compression][tar]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto folder = scratch.filePath( "extracted" );

    const Members members = {
        { "logs/first.log", noise( 300 * KiB, 1 ) },
        { "logs/second.log", noise( 450 * KiB + 7, 2 ) },
        { "third.log", noise( 200 * KiB + 511, 3 ) },
    };
    const auto tar = tarOf( members );
    REQUIRE( tar.size() > 950 * KiB );

    struct Input {
        const char* name;
        QByteArray bytes;
    };
    const Input inputs[] = {
        { "members.tar.zst", zstdCompressed( tar ) },
        { "members.tar.lz4", lz4Compressed( tar ) },
    };

    for ( const auto& input : inputs ) {
        GIVEN( input.name )
        {
            const auto path = scratch.filePath( input.name );
            REQUIRE_FALSE( input.bytes.isEmpty() );
            REQUIRE( writeFile( path, input.bytes ) );

            WHEN( "it is extracted within the budget" )
            {
                const auto outcome = extract( path, folder, 8 * MiB );

                THEN( "every member is extracted byte-identical" )
                {
                    REQUIRE( outcome.started );
                    REQUIRE( outcome.finishedInTime );
                    REQUIRE( outcome.failure == DecompressionFailure::None );
                    REQUIRE( outcome.succeeded );
                    for ( const auto& [ name, data ] : members ) {
                        REQUIRE( readFile( QDir{ folder }.filePath( name ) ) == data );
                    }
                }
            }

            WHEN( "it is extracted under a budget its first member passes" )
            {
                const auto outcome = extract( path, folder, 256 * KiB );

                THEN( "it stops at the budget" )
                {
                    REQUIRE( outcome.finishedInTime );
                    REQUIRE_FALSE( outcome.succeeded );
                    REQUIRE( outcome.failure == DecompressionFailure::BudgetExceeded );
                    REQUIRE( bytesBelow( folder ) <= 256 * KiB );
                }
            }
        }
    }
}

SCENARIO( "A budgeted stream over a sequential decompressor seeks as a tar reader needs",
          "[compression][tar]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const auto payload = noise( 1 * MiB, 4 );

    struct Input {
        const char* name;
        std::function<bool( const QString& )> write;
        std::function<std::unique_ptr<QIODevice>( const QString& )> device;
    };
    const Input inputs[] = {
        { "payload.zst", [ & ]( const QString& path ) { return writeZstd( path, payload ); },
          []( const QString& path ) { return std::make_unique<ZstdDevice>( path ); } },
        { "payload.lz4", [ & ]( const QString& path ) { return writeLz4( path, payload ); },
          []( const QString& path ) { return std::make_unique<Lz4Device>( path ); } },
    };

    for ( const auto& input : inputs ) {
        GIVEN( input.name )
        {
            const auto path = scratch.filePath( input.name );
            REQUIRE( input.write( path ) );

            WHEN( "it is read through a budgeted stream it fits" )
            {
                BudgetedDevice stream{ input.device( path ), 8 * MiB };
                REQUIRE( stream.open( QIODevice::ReadOnly ) );
                std::vector<char> buffer( 16 );

                THEN( "the stream tracks its position, as the tar reader relies on" )
                {
                    REQUIRE_FALSE( stream.isSequential() );
                    REQUIRE( stream.read( buffer.data(), 16 ) == 16 );
                    REQUIRE( stream.pos() == 16 );
                }

                THEN( "a forward seek lands on the bytes at that position" )
                {
                    REQUIRE( stream.read( buffer.data(), 16 ) == 16 );
                    REQUIRE( stream.seek( 600 * KiB ) );
                    REQUIRE( stream.pos() == 600 * KiB );
                    REQUIRE( stream.read( buffer.data(), 16 ) == 16 );
                    REQUIRE( QByteArray( buffer.data(), 16 ) == payload.mid( 600 * KiB, 16 ) );
                    REQUIRE( stream.pos() == 600 * KiB + 16 );
                }

                THEN( "a seek back lands on the same bytes again, never on garbage" )
                {
                    REQUIRE( stream.seek( 600 * KiB ) );
                    REQUIRE( stream.read( buffer.data(), 16 ) == 16 );
                    REQUIRE( stream.seek( 100 ) );
                    REQUIRE( stream.pos() == 100 );
                    REQUIRE( stream.read( buffer.data(), 16 ) == 16 );
                    REQUIRE( QByteArray( buffer.data(), 16 ) == payload.mid( 100, 16 ) );
                    REQUIRE_FALSE( stream.budgetExceeded() );
                }

                THEN( "a seek past the end fails cleanly" )
                {
                    REQUIRE_FALSE( stream.seek( payload.size() + 1 ) );
                    REQUIRE_FALSE( stream.budgetExceeded() );
                    REQUIRE( stream.seek( payload.size() ) );
                    REQUIRE( stream.atEnd() );
                }
            }

            WHEN( "it is read through a budgeted stream it passes" )
            {
                BudgetedDevice stream{ input.device( path ), 512 * KiB };
                REQUIRE( stream.open( QIODevice::ReadOnly ) );
                std::vector<char> buffer( 256 * KiB );
                REQUIRE( stream.read( buffer.data(), 100 ) == 100 );

                THEN( "a seek past the budget fails with the position unchanged" )
                {
                    REQUIRE_FALSE( stream.seek( 600 * KiB ) );
                    REQUIRE( stream.pos() == 100 );
                    REQUIRE( stream.budgetExceeded() );
                }

                THEN( "a forward seek counts against the budget" )
                {
                    REQUIRE( stream.seek( 400 * KiB ) );
                    REQUIRE( stream.read( buffer.data(), 256 * KiB ) == 112 * KiB );
                    REQUIRE( stream.read( buffer.data(), 1 ) < 0 );
                    REQUIRE( stream.budgetExceeded() );
                }
            }
        }
    }
}
