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

// A Log File decompressed from an archive is read from a temporary file that
// is gone by the next start. The Session saves the archive and the member
// instead, and a restore decompresses the archive again (#596).

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <kcompressiondevice.h>
#include <kzip.h>

#include "archivemember.h"

namespace {

const QByteArray LogLines = "first Log Line\nsecond Log Line\n";

// A budget these archives are nowhere near (#816).
constexpr qint64 Generous = 1024 * 1024;

QByteArray gzipped( const QByteArray& data, const QString& scratch )
{
    {
        KCompressionDevice device( scratch, KCompressionDevice::GZip );
        REQUIRE( device.open( QIODevice::WriteOnly ) );
        REQUIRE( device.write( data ) == data.size() );
    }
    QFile file( scratch );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

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

} // namespace

SCENARIO( "A saved archive member is decompressed again", "[ui][session][archive]" )
{
    QTemporaryDir archives;
    QTemporaryDir decompressed;
    REQUIRE( archives.isValid() );
    REQUIRE( decompressed.isValid() );

    // app.log.gz on its own, and logs.zip holding a/app.log and app.log.gz.
    const auto compressed = gzipped( LogLines, archives.filePath( "scratch.gz" ) );
    const auto gzPath = archives.filePath( "app.log.gz" );
    REQUIRE( writeFile( gzPath, compressed ) );
    const auto zipPath = archives.filePath( "logs.zip" );
    {
        KZip zip( zipPath );
        REQUIRE( zip.open( QIODevice::WriteOnly ) );
        REQUIRE( zip.writeFile( "a/app.log", LogLines ) );
        REQUIRE( zip.writeFile( "app.log.gz", compressed ) );
        REQUIRE( zip.writeFile( "other.log", "another Log File\n" ) );
        zip.close();
    }
    // links.zip holding a link to a file of the user's beside the archive (#815).
    const auto secretPath = archives.filePath( "secret.log" );
    REQUIRE( writeFile( secretPath, "the user's own file\n" ) );
    const auto linksPath = archives.filePath( "links.zip" );
    {
        KZip zip( linksPath );
        REQUIRE( zip.open( QIODevice::WriteOnly ) );
        REQUIRE( zip.writeFile( "a/app.log", LogLines ) );
        REQUIRE( zip.writeSymLink( "link.log", secretPath ) );
        zip.close();
    }

    GIVEN( "a compressed single file" )
    {
        const auto member = ArchiveMember{}.inside( gzPath, {} );
        REQUIRE( member == ArchiveMember{ gzPath, { QString{} } } );

        THEN( "its one file is decompressed into the directory" )
        {
            const auto path = decompressArchiveMember( member, decompressed.path(), Generous );
            REQUIRE_FALSE( path.isEmpty() );
            REQUIRE( path.startsWith( decompressed.path() ) );
            REQUIRE( readFile( path ) == LogLines );
        }
    }

    GIVEN( "the member the user picked in an archive of several" )
    {
        const auto member = ArchiveMember{}.inside( zipPath, "a/app.log" );

        THEN( "that member, and no other, is what the path names" )
        {
            const auto path = decompressArchiveMember( member, decompressed.path(), Generous );
            REQUIRE_FALSE( path.isEmpty() );
            REQUIRE( path.endsWith( "/a/app.log" ) );
            REQUIRE( readFile( path ) == LogLines );
        }
    }

    GIVEN( "a compressed file inside an archive" )
    {
        const auto member = ArchiveMember{}.inside( zipPath, "app.log.gz" ).inside( {}, {} );
        REQUIRE( member == ArchiveMember{ zipPath, { "app.log.gz", QString{} } } );

        THEN( "both levels are decompressed" )
        {
            const auto path = decompressArchiveMember( member, decompressed.path(), Generous );
            REQUIRE_FALSE( path.isEmpty() );
            REQUIRE( readFile( path ) == LogLines );
        }
    }

    GIVEN( "an archive that is gone" )
    {
        const auto member = ArchiveMember{}.inside( archives.filePath( "gone.zip" ), "a/app.log" );

        THEN( "nothing is decompressed" )
        {
            REQUIRE( decompressArchiveMember( member, decompressed.path(), Generous ).isEmpty() );
        }
    }

    GIVEN( "a member that is not in the archive" )
    {
        THEN( "nothing is named, however the member is written" )
        {
            for ( const auto* name : { "missing.log", "../app.log.gz", "a/../../logs.zip" } ) {
                INFO( name );
                const auto member = ArchiveMember{}.inside( zipPath, name );
                REQUIRE(
                    decompressArchiveMember( member, decompressed.path(), Generous ).isEmpty() );
            }
        }
    }

    GIVEN( "a member that is a link to a file outside the archive" )
    {
        const auto member = ArchiveMember{}.inside( linksPath, "link.log" );

        THEN( "nothing is named, and the file the link leads to is untouched" )
        {
            REQUIRE( decompressArchiveMember( member, decompressed.path(), Generous ).isEmpty() );
            REQUIRE( readFile( secretPath ) == "the user's own file\n" );
            // The other member is still there to take.
            const auto path = decompressArchiveMember(
                ArchiveMember{}.inside( linksPath, "a/app.log" ), decompressed.path(), Generous );
            REQUIRE( readFile( path ) == LogLines );
            REQUIRE_FALSE(
                QFileInfo{ QFileInfo{ path }.dir().filePath( "../link.log" ) }.isSymLink() );
        }
    }

    GIVEN( "a member that does not fit the archive's kind" )
    {
        THEN( "nothing is decompressed" )
        {
            // A compressed single file has no named members, an archive no
            // unnamed one.
            REQUIRE( decompressArchiveMember( ArchiveMember{ gzPath, { "app.log" } },
                                              decompressed.path(), Generous )
                         .isEmpty() );
            REQUIRE( decompressArchiveMember( ArchiveMember{ zipPath, { QString{} } },
                                              decompressed.path(), Generous )
                         .isEmpty() );
        }
    }
}

// A Log File from an archive keeps its tab name and tab group by its archive
// and member, which name it at every start where its temporary path does not
// (#609).
TEST_CASE( "An archive member is known by one key at every start", "[ui][session][archive]" )
{
    CHECK( ArchiveMember{}.key().isEmpty() );
    CHECK( ArchiveMember{ "/logs/app.log.gz", { QString{} } }.key()
           == QStringLiteral( "/logs/app.log.gz!/" ) );
    CHECK( ArchiveMember{ "/logs/logs.zip", { "a/app.log" } }.key()
           == QStringLiteral( "/logs/logs.zip!/a/app.log" ) );
    CHECK( ArchiveMember{ "/logs/logs.zip", { "app.log.gz", QString{} } }.key()
           == QStringLiteral( "/logs/logs.zip!/app.log.gz!/" ) );

    // Another member of the same archive, or the archive itself, is another key.
    CHECK( ArchiveMember{ "/logs/logs.zip", { "b/app.log" } }.key()
           != ArchiveMember{ "/logs/logs.zip", { "a/app.log" } }.key() );
    CHECK( ArchiveMember{ "/logs/logs.zip", { "a/app.log" } }.key()
           != QStringLiteral( "/logs/logs.zip" ) );
}

// Past the decompression budget, a member is not decompressed, and nothing
// of it is left in the directory (#816).
SCENARIO( "A saved archive member past the decompression budget is not decompressed",
          "[ui][session][archive][budget]" )
{
    QTemporaryDir archives;
    QTemporaryDir decompressed;
    REQUIRE( archives.isValid() );
    REQUIRE( decompressed.isValid() );

    const auto isEmpty = [ & ] {
        return QDir{ decompressed.path() }
            .entryList( QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot )
            .isEmpty();
    };

    // 4 MiB of one byte, a few KB compressed; the budget below is 1 MiB.
    const QByteArray expanding( 4 * 1024 * 1024, 'x' );
    const auto compressed = gzipped( expanding, archives.filePath( "scratch.gz" ) );
    const auto gzPath = archives.filePath( "huge.log.gz" );
    REQUIRE( writeFile( gzPath, compressed ) );
    const auto zipPath = archives.filePath( "huge.zip" );
    {
        KZip zip( zipPath );
        REQUIRE( zip.open( QIODevice::WriteOnly ) );
        REQUIRE( zip.writeFile( "huge.log", expanding ) );
        REQUIRE( zip.writeFile( "huge.log.gz", compressed ) );
        zip.close();
    }

    GIVEN( "a compressed single file that expands past the budget" )
    {
        THEN( "it is not decompressed, and the directory stays empty" )
        {
            REQUIRE( decompressArchiveMember( ArchiveMember{}.inside( gzPath, {} ),
                                              decompressed.path(), Generous )
                         .isEmpty() );
            REQUIRE( isEmpty() );
        }
    }

    GIVEN( "an archive whose members expand past the budget" )
    {
        THEN( "its member is not taken, and the directory stays empty" )
        {
            REQUIRE( decompressArchiveMember( ArchiveMember{}.inside( zipPath, "huge.log" ),
                                              decompressed.path(), Generous )
                         .isEmpty() );
            REQUIRE( isEmpty() );
        }

        THEN( "a member within the budget at every level is taken under a budget that fits" )
        {
            const auto path = decompressArchiveMember(
                ArchiveMember{}.inside( zipPath, "huge.log.gz" ).inside( "huge.log.gz", {} ),
                decompressed.path(), 8 * 1024 * 1024 );
            REQUIRE_FALSE( path.isEmpty() );
            REQUIRE( readFile( path ) == expanding );
        }
    }
}
