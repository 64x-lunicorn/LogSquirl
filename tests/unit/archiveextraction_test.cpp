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

// Every entry of an archive lands inside the extraction folder and nowhere
// else, whatever the archive names (#815).

#include <catch2/catch_test_macros.hpp>

#include <functional>

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <ktar.h>
#include <kzip.h>

#include "archiveextraction.h"
#include "atomicflag.h"

namespace {

const QByteArray LogLines = "first Log Line\nsecond Log Line\n";
const QByteArray Secret = "the victim's own file\n";

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

// Every entry below `folder`, links included, relative to it.
QStringList entriesBelow( const QString& folder )
{
    QStringList entries;
    const QDir dir{ folder };
    QDirIterator it{ folder, QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
                     QDirIterator::Subdirectories };
    while ( it.hasNext() ) {
        entries.append( dir.relativeFilePath( it.next() ) );
    }
    entries.sort();
    return entries;
}

bool holdsNoLink( const QString& folder )
{
    QDirIterator it{ folder, QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
                     QDirIterator::Subdirectories };
    while ( it.hasNext() ) {
        it.next();
        if ( it.fileInfo().isSymLink() ) {
            return false;
        }
    }
    return true;
}

// Writes an archive of each kind with the same entries.
void writeArchive( KArchive& archive, const std::function<void( KArchive& )>& entries )
{
    REQUIRE( archive.open( QIODevice::WriteOnly ) );
    entries( archive );
    REQUIRE( archive.close() );
}

bool extract( const QString& archivePath, const QString& folder )
{
    AtomicFlag interrupt;
    auto extracted = false;
    if ( archivePath.endsWith( ".zip" ) ) {
        KZip zip{ archivePath };
        REQUIRE( zip.open( QIODevice::ReadOnly ) );
        extracted = extractArchiveDirectory( *zip.directory(), folder, interrupt );
    }
    else {
        KTar tar{ archivePath };
        REQUIRE( tar.open( QIODevice::ReadOnly ) );
        extracted = extractArchiveDirectory( *tar.directory(), folder, interrupt );
    }
    return extracted;
}

} // namespace

SCENARIO( "An archive extracts into its folder and nowhere else", "[compression][archive]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );

    // The extraction folder, a folder of the user's beside it, and a sibling
    // whose name merely starts with the extraction folder's.
    const auto folder = scratch.filePath( "dest" );
    const auto outside = scratch.filePath( "outside" );
    const auto sibling = scratch.filePath( "destX" );
    REQUIRE( QDir().mkpath( folder ) );
    REQUIRE( QDir().mkpath( outside ) );
    const auto secret = QDir{ outside }.filePath( "secret.log" );
    REQUIRE( writeFile( secret, Secret ) );

    const auto entries = [ & ]( KArchive& archive ) {
        REQUIRE( archive.writeFile( "inside.log", LogLines ) );
        // A link whose target is absolute, and one that leads out of the folder.
        REQUIRE( archive.writeSymLink( "absolute", secret ) );
        REQUIRE( archive.writeSymLink( "relative", "../outside/secret.log" ) );
        // A link and a directory differing in letter case only, with a file
        // inside: on a case-insensitive file system the directory is the link.
        REQUIRE( archive.writeSymLink( "D", outside ) );
        REQUIRE( archive.writeFile( "d/through.log", LogLines ) );
        // A path that lands in the sibling once cleaned.
        REQUIRE( archive.writeFile( "../destX/escaped.log", LogLines ) );
    };

    for ( const auto* archiveName : { "hostile.tar", "hostile.zip" } ) {
        GIVEN( archiveName )
        {
            const auto archivePath = scratch.filePath( archiveName );
            if ( archivePath.endsWith( ".zip" ) ) {
                KZip zip{ archivePath };
                writeArchive( zip, entries );
            }
            else {
                KTar tar{ archivePath };
                writeArchive( tar, entries );
            }

            WHEN( "it is extracted" )
            {
                REQUIRE( extract( archivePath, folder ) );

                THEN( "the plain entries are in the folder" )
                {
                    REQUIRE( readFile( QDir{ folder }.filePath( "inside.log" ) ) == LogLines );
                    const QFileInfo through{ QDir{ folder }.filePath( "d/through.log" ) };
                    REQUIRE( through.isFile() );
                    REQUIRE_FALSE( through.isSymLink() );
                    REQUIRE_FALSE( QFileInfo{ QDir{ folder }.filePath( "d" ) }.isSymLink() );
                    REQUIRE( readFile( through.filePath() ) == LogLines );
                }

                THEN( "no link was created" )
                {
                    REQUIRE( holdsNoLink( folder ) );
                    for ( const auto* name : { "absolute", "relative", "D", "absolute.lnk",
                                               "relative.lnk", "D.lnk" } ) {
                        INFO( name );
                        const QFileInfo link{ QDir{ folder }.filePath( name ) };
                        REQUIRE_FALSE( link.isSymLink() );
                        // A directory named D is the file system folding the
                        // case of d; a link of that name is not.
                        REQUIRE( ( !link.exists() || link.isDir() ) );
                    }
                }

                THEN( "nothing landed outside the folder" )
                {
                    REQUIRE( entriesBelow( outside ) == QStringList{ "secret.log" } );
                    REQUIRE( readFile( secret ) == Secret );
                    REQUIRE_FALSE( QFileInfo::exists( sibling ) );
                    REQUIRE( entriesBelow( scratch.path() ).filter( "escaped" ).isEmpty() );
                }
            }
        }
    }

    GIVEN( "an extraction that was interrupted" )
    {
        const auto archivePath = scratch.filePath( "plain.zip" );
        KZip zip{ archivePath };
        writeArchive( zip, [ & ]( KArchive& archive ) {
            REQUIRE( archive.writeFile( "inside.log", LogLines ) );
        } );

        THEN( "it reports that it did not finish" )
        {
            KZip reading{ archivePath };
            REQUIRE( reading.open( QIODevice::ReadOnly ) );
            AtomicFlag interrupt{ true };
            REQUIRE_FALSE( extractArchiveDirectory( *reading.directory(), folder, interrupt ) );
        }
    }
}

SCENARIO( "The member chosen from an extraction folder is a file of that folder",
          "[compression][archive]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );

    const auto folder = scratch.filePath( "dest" );
    const auto outside = scratch.filePath( "outside" );
    REQUIRE( QDir().mkpath( QDir{ folder }.filePath( "a" ) ) );
    REQUIRE( QDir().mkpath( outside ) );
    const auto secret = QDir{ outside }.filePath( "secret.log" );
    REQUIRE( writeFile( secret, Secret ) );
    REQUIRE( writeFile( QDir{ folder }.filePath( "a/app.log" ), LogLines ) );

    // A link to a file outside and a link to a folder outside, planted in the
    // folder. On Windows a link is a shortcut, which needs its suffix.
#ifdef Q_OS_WIN
    const auto fileLink = QStringLiteral( "link.log.lnk" );
    const auto folderLink = QStringLiteral( "linkdir.lnk" );
#else
    const auto fileLink = QStringLiteral( "link.log" );
    const auto folderLink = QStringLiteral( "linkdir" );
#endif
    REQUIRE( QFile::link( secret, QDir{ folder }.filePath( fileLink ) ) );
    REQUIRE( QFile::link( outside, QDir{ folder }.filePath( folderLink ) ) );

    THEN( "a plain file of the folder is named by its path inside it" )
    {
        const auto path = extractedMemberPath( folder, "a/app.log" );
        REQUIRE( path == QDir{ folder }.filePath( "a/app.log" ) );
        REQUIRE( extractedMemberPath( folder, "./a/../a/app.log" ) == path );
    }

    THEN( "a member that is a link, or lies behind one, is refused" )
    {
        REQUIRE( extractedMemberPath( folder, fileLink ).isEmpty() );
        REQUIRE( extractedMemberPath( folder, folderLink + "/secret.log" ).isEmpty() );
    }

    THEN( "a member outside the folder, a directory or a missing one is refused" )
    {
        for ( const auto* member :
              { "../outside/secret.log", "a/../../outside/secret.log", "a", "missing.log", "" } ) {
            INFO( member );
            REQUIRE( extractedMemberPath( folder, member ).isEmpty() );
        }
        REQUIRE( extractedMemberPath( folder, secret ).isEmpty() );
    }
}
