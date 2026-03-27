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

#include "pluginrepository.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <kzip.h>

using logsquirl::plugins::PluginRepository;

namespace {

// Helper: create a minimal ZIP archive containing a plugin.json and a dummy library file.
// Returns the path to the created archive on success, empty string on failure.
QString createTestPluginZip( const QString& archivePath,
                             const QString& pluginJsonContent,
                             const QString& libraryName )
{
    KZip zip( archivePath );
    if ( !zip.open( QIODevice::WriteOnly ) ) {
        return {};
    }

    const auto jsonBytes = pluginJsonContent.toUtf8();
    zip.writeFile( "plugin.json", jsonBytes );

    // Write a small dummy binary to simulate a shared library
    const QByteArray dummyLib( "FAKE_SHARED_LIBRARY_CONTENT" );
    zip.writeFile( libraryName, dummyLib );

    zip.close();
    return archivePath;
}

// Helper: build a minimal plugin.json string
QString makePluginJson( const QString& id, const QString& name )
{
    QJsonObject obj;
    obj[ "id" ] = id;
    obj[ "name" ] = name;
    obj[ "version" ] = "1.0.0";
    obj[ "type" ] = "ui";
    obj[ "library" ] = "logsquirl_test";
    obj[ "api_version" ] = 1;
    obj[ "description" ] = "Test plugin";
    obj[ "author" ] = "Tests";
    obj[ "license" ] = "MIT";
    return QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
}

} // namespace

SCENARIO( "extractPluginArchive extracts a valid ZIP to the destination",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A temporary directory and a valid plugin ZIP archive" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto archivePath = tmpDir.filePath( "test-plugin.zip" );
        const auto json = makePluginJson( "io.test.plugin", "Test Plugin" );
        REQUIRE_FALSE(
            createTestPluginZip( archivePath, json, "liblogsquirl_test.dylib" ).isEmpty() );

        WHEN( "extractPluginArchive is called with a new destination directory" )
        {
            const auto destDir = tmpDir.filePath( "io.test.plugin" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( archivePath, destDir, &errorMessage );

            THEN( "it returns true and the files exist in the destination" )
            {
                REQUIRE( result );
                REQUIRE( errorMessage.isEmpty() );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "plugin.json" ) ) );
                REQUIRE(
                    QFile::exists( QDir( destDir ).filePath( "liblogsquirl_test.dylib" ) ) );
            }

            THEN( "the extracted plugin.json has the expected content" )
            {
                QFile f( QDir( destDir ).filePath( "plugin.json" ) );
                REQUIRE( f.open( QIODevice::ReadOnly ) );
                const auto doc = QJsonDocument::fromJson( f.readAll() );
                REQUIRE( doc.isObject() );
                REQUIRE( doc.object().value( "id" ).toString()
                         == QString( "io.test.plugin" ) );
            }
        }
    }
}

SCENARIO( "extractPluginArchive creates the destination directory if it does not exist",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A valid plugin ZIP and a non-existent nested destination path" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto archivePath = tmpDir.filePath( "nested-test.zip" );
        const auto json = makePluginJson( "io.test.nested", "Nested" );
        REQUIRE_FALSE( createTestPluginZip( archivePath, json, "lib.so" ).isEmpty() );

        const auto destDir = tmpDir.filePath( "a/b/c/io.test.nested" );
        REQUIRE_FALSE( QDir( destDir ).exists() );

        WHEN( "extractPluginArchive is called" )
        {
            const auto result
                = PluginRepository::extractPluginArchive( archivePath, destDir );

            THEN( "the directory is created and files are extracted" )
            {
                REQUIRE( result );
                REQUIRE( QDir( destDir ).exists() );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "plugin.json" ) ) );
            }
        }
    }
}

SCENARIO( "extractPluginArchive fails gracefully on an invalid archive",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A file that is not a valid ZIP" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto fakePath = tmpDir.filePath( "not-a-zip.zip" );
        {
            QFile f( fakePath );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "This is not a ZIP file" );
        }

        WHEN( "extractPluginArchive is called" )
        {
            const auto destDir = tmpDir.filePath( "output" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( fakePath, destDir, &errorMessage );

            THEN( "it returns false and provides an error message" )
            {
                REQUIRE_FALSE( result );
                REQUIRE_FALSE( errorMessage.isEmpty() );
            }
        }
    }
}

SCENARIO( "extractPluginArchive fails gracefully for a non-existent file",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A path to a file that does not exist" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto missingPath = tmpDir.filePath( "missing.zip" );

        WHEN( "extractPluginArchive is called" )
        {
            const auto destDir = tmpDir.filePath( "output" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( missingPath, destDir, &errorMessage );

            THEN( "it returns false with an error message" )
            {
                REQUIRE_FALSE( result );
                REQUIRE_FALSE( errorMessage.isEmpty() );
            }
        }
    }
}

SCENARIO( "extractPluginArchive overwrites existing files in the destination",
          "[pluginrepository][plugins]" )
{
    GIVEN( "An existing plugin directory with old content" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto destDir = tmpDir.filePath( "io.test.overwrite" );
        QDir().mkpath( destDir );

        // Write an old plugin.json with version 0.1.0
        {
            QFile f( QDir( destDir ).filePath( "plugin.json" ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( makePluginJson( "io.test.overwrite", "Old" ).toUtf8() );
        }

        const auto archivePath = tmpDir.filePath( "update.zip" );
        QJsonObject obj;
        obj[ "id" ] = "io.test.overwrite";
        obj[ "name" ] = "Updated";
        obj[ "version" ] = "2.0.0";
        obj[ "type" ] = "ui";
        obj[ "library" ] = "logsquirl_test";
        obj[ "api_version" ] = 1;
        obj[ "description" ] = "Updated plugin";
        obj[ "author" ] = "Tests";
        obj[ "license" ] = "MIT";
        const auto json
            = QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
        REQUIRE_FALSE( createTestPluginZip( archivePath, json, "lib.so" ).isEmpty() );

        WHEN( "extractPluginArchive is called on the existing directory" )
        {
            const auto result
                = PluginRepository::extractPluginArchive( archivePath, destDir );

            THEN( "files are overwritten with the new content" )
            {
                REQUIRE( result );
                QFile f( QDir( destDir ).filePath( "plugin.json" ) );
                REQUIRE( f.open( QIODevice::ReadOnly ) );
                const auto doc = QJsonDocument::fromJson( f.readAll() );
                REQUIRE( doc.object().value( "version" ).toString() == QString( "2.0.0" ) );
                REQUIRE( doc.object().value( "name" ).toString() == QString( "Updated" ) );
            }
        }
    }
}
